#include <regex>

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
        auto &named_params = input.named_parameters;
        auto table_search_str = GetSearchString("TABLENAME", input);
        auto text_search_str = GetSearchString("TEXT", input);
        auto max_read_threads = named_params.find("THREADS") != named_params.end() 
                                    ? named_params["THREADS"].GetValue<u_int32_t>()
                                    : 0;

        auto where_clause = StringUtil::Format(
            "TABNAME LIKE '%s' AND ( TABCLASS = 'VIEW' OR TABCLASS = 'TRANSP' OR "
            "TABCLASS = 'POOL' OR TABCLASS = 'CLUSTER' ) AND DDTEXT LIKE '%s'", 
            table_search_str, text_search_str
        );

        auto fields =  std::vector<std::string>({ "TABNAME", "DDTEXT", "DDLANGUAGE", "TABCLASS" });
        auto result = make_uniq<RfcReadTableBindData>("DD02V", max_read_threads, 0, &DefaultRfcConnectionFactory, context);
        result->InitOptionsFromWhereClause(where_clause);
        result->InitAndVerifyFields(fields);

        names = { "table_name", "text", "language", "class" };
        return_types = result->GetReturnTypes();

        return std::move(result);
    }

    static void RfcShowTablesScan(ClientContext &context, 
                                  TableFunctionInput &data, 
                                  DataChunk &output) 
    {
        auto &bind_data = data.bind_data->CastNoConst<RfcReadTableBindData>();

        if (bind_data.HasMoreResults()) {
            return;
        }

        bind_data.Step(context, output);
    }

    CreateTableFunctionInfo CreateRfcShowTablesScanFunction() 
    {
        auto fun = TableFunction("sap_show_tables", { }, 
                                 RfcShowTablesScan, 
                                 RfcShowTablesBind);
        fun.named_parameters["TABLENAME"] = LogicalType::VARCHAR;
        fun.named_parameters["TEXT"] = LogicalType::VARCHAR;
        fun.named_parameters["THREADS"] = LogicalType::UINTEGER;
        fun.to_string = RfcShowTablesToString;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb