#include <regex>

#include "duckdb/parallel/pipeline.hpp"
#include "duckdb/parallel/event.hpp"

#include "scanner_show_tables.hpp"
#include "duckdb_argument_helper.hpp"
#include "sap_rfc.hpp"
#include "telemetry.hpp"

namespace duckdb 
{
    
static std::string GetEscapedSearchPattern(const std::string &param_name, 
                                           const TableFunctionBindInput &input) 
{
    auto &named_params = input.named_parameters;
    auto search_string = named_params.find(param_name) != named_params.end() 
        ? named_params.at(param_name).ToString() : "%";
    search_string = std::regex_replace(search_string, std::regex("\\*"), "%");
    return StringUtil::Replace(search_string, "'", "''");
}

static unique_ptr<FunctionData> RfcShowTablesBind(ClientContext &context, 
                                                    TableFunctionBindInput &input, 
                                                    vector<LogicalType> &return_types, 
                                                    vector<string> &names) 
{
    PostHogTelemetry::Instance().RecordFunctionCall("sap_show_tables");

    auto &named_params = input.named_parameters;
    auto table_search_str = GetEscapedSearchPattern("TABLENAME", input);
    auto text_search_str = GetEscapedSearchPattern("TEXT", input);
    auto max_read_threads = named_params.find("THREADS") != named_params.end() 
                                ? named_params["THREADS"].GetValue<unsigned int>()
                                : 0;

    auto where_clause = StringUtil::Format(
        "DDLANGUAGE = 'E' AND "
        "TABNAME LIKE '%s' AND ( TABCLASS = 'VIEW' OR TABCLASS = 'TRANSP' OR "
        "TABCLASS = 'POOL' OR TABCLASS = 'CLUSTER' ) AND DDTEXT LIKE '%s'", 
        table_search_str, text_search_str
    );

    auto secret_name = named_params.find("SECRET") != named_params.end()
                            ? named_params["SECRET"].ToString()
                            : "";
    auto rtf_opts = ResolveReadTableFunctionOptions(context, &named_params, secret_name);

    auto fields =  std::vector<std::string>({ "TABNAME", "DDTEXT", "TABCLASS" });
    auto result = make_uniq<RfcReadTableBindData>("DD02V", max_read_threads, 0,
                                                  rtf_opts,
                                                  &DefaultRfcConnectionFactory, context);
    if (!secret_name.empty()) {
        result->SetSecretName(secret_name);
    }
    result->InitOptionsFromWhereClause(where_clause);
    result->InitAndVerifyFields(fields);

    names = { "table_name", "text", "class" };
    return_types = result->GetReturnTypes();

    return std::move(result);
}

static unique_ptr<GlobalTableFunctionState> RfcShowTablesInitGlobalState(ClientContext &context,
                                                                            TableFunctionInitInput &input) 
{
    auto &bind_data = input.bind_data->CastNoConst<RfcReadTableBindData>();
    auto column_ids = input.column_ids;

    bind_data.ActivateColumns(column_ids);
    bind_data.PrepareForExecution(context);

    // Own the state machines per EXECUTION, not per bind: DuckDB reuses bind data
    // across executions of a bound plan, so bind-owned machines make a re-scan resume
    // from an exhausted cursor and return nothing.
    auto gstate = make_uniq<RfcReadTableGlobalState>(1, nullptr);
    gstate->serial_machines = bind_data.CreateWindowStateMachines();
    return std::move(gstate);
}

static void RfcShowTablesScan(ClientContext &context, 
                              TableFunctionInput &data, 
                              DataChunk &output) 
{
    auto &bind_data = data.bind_data->CastNoConst<RfcReadTableBindData>();
    auto &machines = data.global_state->Cast<RfcReadTableGlobalState>().serial_machines;

    if (! bind_data.HasMoreResults(machines)) {
        return;
    }

    bind_data.Step(context, output, machines);
}

TableFunction CreateRfcShowTablesScanFunction() 
{
    auto fun = TableFunction("sap_show_tables", { }, 
                                RfcShowTablesScan, 
                                RfcShowTablesBind,
                                RfcShowTablesInitGlobalState);
    fun.named_parameters["TABLENAME"] = LogicalType::VARCHAR;
    fun.named_parameters["TEXT"] = LogicalType::VARCHAR;
    fun.named_parameters["THREADS"] = LogicalType::UINTEGER;
    fun.named_parameters["SECRET"] = LogicalType::VARCHAR;
    fun.named_parameters["READ_TABLE_FUNCTION"] = LogicalType::VARCHAR;
    fun.named_parameters["READ_TABLE_DELIMITER"] = LogicalType::VARCHAR;
    fun.projection_pushdown = true;

    return TableFunction(fun);
}

} // namespace duckdb
