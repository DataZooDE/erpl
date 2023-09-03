#include "duckdb/parallel/pipeline.hpp"
#include "duckdb/parallel/event.hpp"

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

        RfcConnectionFactory_t connection_factory { [](ClientContext &context) { 
            return RfcAuthParams::FromContext(context).Connect(); 
        }};

        auto fields =  std::vector<std::string>({ "TABNAME", "DDTEXT", "DDLANGUAGE", "TABCLASS" });
        auto result = make_uniq<RfcReadTableBindData>("DD02V", connection_factory, context);
        result->InitOptionsFromWhereClause(where_clause);
        result->InitAndVerifyFields(fields);

        names = result->GetColumnNames();
        return_types = result->GetReturnTypes();

        return std::move(result);
    }

    static void RfcShowTablesScan(ClientContext &context, 
                                  TableFunctionInput &data, 
                                  DataChunk &output) 
    {
        auto &bind_data = data.bind_data->CastNoConst<RfcReadTableBindData>();

        printf("\n>>> Scan\n");

        if (bind_data.Finished()) {
            return;
        }

        bind_data.Step(context, output);
        printf("<<< Scan\n");
    }

    CreateTableFunctionInfo CreateRfcShowTablesScanFunction() 
    {
        auto fun = TableFunction("sap_show_tables", { }, 
                                 RfcShowTablesScan, 
                                 RfcShowTablesBind);
        fun.named_parameters["TABLENAME"] = LogicalType::VARCHAR;
        fun.to_string = RfcShowTablesToString;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb