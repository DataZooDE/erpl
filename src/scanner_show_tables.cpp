#include "scanner_show_tables.hpp"
#include "duckdb_argument_helper.hpp"
#include "sap_rfc.hpp"

namespace duckdb 
{
    static string RfcShowTablesToString(const FunctionData *bind_data_p) 
    {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static unique_ptr<FunctionData> RfcShowTablesBind(ClientContext &context, 
                                                      TableFunctionBindInput &input, 
                                                      vector<LogicalType> &return_types, 
                                                      vector<string> &names) 
    {
        auto &named_params = input.named_parameters;
        auto search_str = named_params.find("TABLENAME") != named_params.end() 
                            ? named_params["TABLENAME"].ToString() : "%";

        auto where_clause = StringUtil::Format(
            "TABNAME LIKE '%s' AND ( TABCLASS = 'VIEW' OR TABCLASS = 'TRANSP' OR "
            "TABCLASS = 'POOL' OR TABCLASS = 'CLUSTER' ) AND DDTEXT LIKE '%s'", 
            search_str, search_str
        );

        auto connection = RfcAuthParams::FromContext(context).Connect();

        auto fields =  std::vector<std::string>({ "TABNAME", "DDTEXT", "DDLANGUAGE", "TABCLASS" });
        auto result = make_uniq<RfcReadTableBindData>("DD02V");
        result->SetOptionsFromWhereClause(where_clause);
        result->SetAndVerifyFields(connection, fields);

        names = result->GetColumnNames();
        return_types = result->GetReturnTypes();

        return std::move(result);
    }

    static unique_ptr<GlobalTableFunctionState> RfcShowTablesInitGlobalState(ClientContext &context,
                                                                             TableFunctionInitInput &input) 
    {
        auto bind_data = input.bind_data->Cast<RfcReadTableBindData>();
        auto result = bind_data.InitializeGlobalState(context);
        return result;
    }

    static unique_ptr<LocalTableFunctionState> RfcShowTablesInitLocalState(ExecutionContext &context, 
                                                                           TableFunctionInitInput &input, 
                                                                           GlobalTableFunctionState *global_state) 
    {
        auto &g_state = global_state->Cast<RfcReadTableGlobalState>();
        auto result = g_state.InitializeLocalState();

        printf("RfcShowTablesInitLocalState for column: %d\n", result->column_idx);

        return result;
    }

    static void RfcShowTablesScan(ClientContext &context, 
                                  TableFunctionInput &data, 
                                  DataChunk &output) 
    {
        auto bind_data = data.bind_data->Cast<RfcReadTableBindData>();
        //auto &gstate = data.global_state->Cast<RfcReadTableGlobalState>();
	    auto state = data.local_state->Cast<RfcReadTableLocalState>();

        printf("RfcShowTablesScan for column: %d\n", state.column_idx);

        if (state.Finished()) {
            return;
        }

        auto connection = RfcAuthParams::FromContext(context).Connect();
        state.Step(bind_data, connection, output);

        output.SetCardinality(0);
    }

    CreateTableFunctionInfo CreateRfcShowTablesScanFunction() 
    {
        auto fun = TableFunction("sap_show_tables", { }, 
                                 RfcShowTablesScan, 
                                 RfcShowTablesBind, 
                                 RfcShowTablesInitGlobalState, 
                                 RfcShowTablesInitLocalState);
        fun.named_parameters["TABLENAME"] = LogicalType::VARCHAR;
        fun.to_string = RfcShowTablesToString;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb