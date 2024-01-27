#include <regex>

#include "duckdb/parallel/pipeline.hpp"
#include "duckdb/parallel/event.hpp"

#include "scanner_show_tables.hpp"
#include "duckdb_argument_helper.hpp"
#include "sap_rfc.hpp"
#include "telemetry.hpp"

namespace duckdb 
{
    
static std::string GetSearchString(const std::string param_name, 
                                    const TableFunctionBindInput &input) 
{
    auto &named_params = input.named_parameters;
    auto search_string =  named_params.find(param_name) != named_params.end() 
        ? named_params[param_name].ToString() : "%";
    search_string = std::regex_replace(search_string, std::regex("\\*"), "%");
    return search_string;
}

static unique_ptr<FunctionData> RfcShowTablesBind(ClientContext &context, 
                                                    TableFunctionBindInput &input, 
                                                    vector<LogicalType> &return_types, 
                                                    vector<string> &names) 
{
    PostHogTelemetry::Instance().CaptureFunctionExecution("sap_show_tables");

    auto &named_params = input.named_parameters;
    auto table_search_str = GetSearchString("TABLENAME", input);
    auto text_search_str = GetSearchString("TEXT", input);
    auto max_read_threads = named_params.find("THREADS") != named_params.end() 
                                ? named_params["THREADS"].GetValue<unsigned int>()
                                : 0;

    auto where_clause = StringUtil::Format(
        "DDLANGUAGE = 'E' AND "
        "TABNAME LIKE '%s' AND ( TABCLASS = 'VIEW' OR TABCLASS = 'TRANSP' OR "
        "TABCLASS = 'POOL' OR TABCLASS = 'CLUSTER' ) AND DDTEXT LIKE '%s'", 
        table_search_str, text_search_str
    );

    auto fields =  std::vector<std::string>({ "TABNAME", "DDTEXT", "TABCLASS" });
    auto result = make_uniq<RfcReadTableBindData>("DD02V", max_read_threads, 0, &DefaultRfcConnectionFactory, context);
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

    return make_uniq<GlobalTableFunctionState>();
}

static void RfcShowTablesScan(ClientContext &context, 
                                TableFunctionInput &data, 
                                DataChunk &output) 
{
    auto &bind_data = data.bind_data->CastNoConst<RfcReadTableBindData>();

    if (! bind_data.HasMoreResults()) {
        return;
    }

    bind_data.Step(context, output);
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
    fun.projection_pushdown = true;

    return TableFunction(fun);
}

} // namespace duckdb