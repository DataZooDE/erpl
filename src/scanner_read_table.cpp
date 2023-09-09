#include <regex>

#include "duckdb/parallel/pipeline.hpp"
#include "duckdb/parallel/event.hpp"

#include "scanner_read_table.hpp"
#include "duckdb_argument_helper.hpp"
#include "sap_rfc.hpp"

namespace duckdb 
{
    static string RfcReadTableToString(const FunctionData *bind_data_p) 
    {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static unique_ptr<FunctionData> RfcReadTableBind(ClientContext &context, 
                                                     TableFunctionBindInput &input, 
                                                     vector<LogicalType> &return_types, 
                                                     vector<string> &names) 
    {
        RfcConnectionFactory_t connection_factory { [](ClientContext &context) { 
            return RfcAuthParams::FromContext(context).Connect(); 
        }};

        auto table_name = input.inputs[0].ToString();
        auto &named_params = input.named_parameters;
        auto max_read_threads = named_params.find("THREADS") != named_params.end() 
                                    ? named_params["THREADS"].GetValue<u_int32_t>()
                                    : 0;

        auto bind_data = make_uniq<RfcReadTableBindData>(table_name, max_read_threads, connection_factory, context);
        
        if (named_params.find("MAX_ROWS") != named_params.end()) {
            bind_data->limit = named_params["MAX_ROWS"].GetValue<u_int32_t>();
        }

        auto where_clause = named_params.find("FILTER") != named_params.end() 
                                ? named_params["FILTER"].ToString()
                                : "";
        bind_data->InitOptionsFromWhereClause(where_clause);

        auto fields = named_params.find("COLUMNS") != named_params.end() 
                            ? ConvertListValueToVector<std::string>(named_params["COLUMNS"])
                            : std::vector<std::string>();
        bind_data->InitAndVerifyFields(fields);

        names = bind_data->GetColumnNames();
        return_types = bind_data->GetReturnTypes();

        return std::move(bind_data);
    }

    static void RfcReadTableScan(ClientContext &context, 
                                 TableFunctionInput &data, 
                                 DataChunk &output) 
    {
        auto &bind_data = data.bind_data->CastNoConst<RfcReadTableBindData>();
        if (bind_data.HasMoreResults()) {
            return;
        }

        bind_data.Step(context, output);
    }

    CreateTableFunctionInfo CreateRfcReadTableScanFunction() 
    {
        auto fun = TableFunction("sap_read_table", { LogicalType::VARCHAR }, RfcReadTableScan, RfcReadTableBind);
        fun.named_parameters["THREADS"] = LogicalType::UINTEGER;
        fun.named_parameters["COLUMNS"] = LogicalType::LIST(LogicalType::VARCHAR);
        fun.named_parameters["FILTER"] = LogicalType::VARCHAR;
        fun.named_parameters["MAX_ROWS"] = LogicalType::UINTEGER;
        fun.to_string = RfcReadTableToString;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb