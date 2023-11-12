#include <regex>

#include "duckdb/parallel/pipeline.hpp"
#include "duckdb/parallel/event.hpp"

#include "scanner_read_table.hpp"
#include "duckdb_argument_helper.hpp"
#include "sap_rfc.hpp"
#include "telemetry.hpp"

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
        PostHogTelemetry::Instance().CaptureFunctionExecution("sap_read_table");

        auto table_name = input.inputs[0].ToString();
        auto &named_params = input.named_parameters;
        auto max_read_threads = named_params.find("THREADS") != named_params.end() 
                                    ? named_params["THREADS"].GetValue<unsigned int>()
                                    : 0;
        auto limit = named_params.find("MAX_ROWS") != named_params.end() 
                                    ? named_params["MAX_ROWS"].GetValue<unsigned int>()
                                    : 0;
        auto where_clause = named_params.find("FILTER") != named_params.end() 
                                ? named_params["FILTER"].ToString()
                                : "";
        
        auto fields = named_params.find("COLUMNS") != named_params.end() 
                            ? ConvertListValueToVector<std::string>(named_params["COLUMNS"])
                            : std::vector<std::string>();

        auto bind_data = make_uniq<RfcReadTableBindData>(table_name, max_read_threads, limit, &DefaultRfcConnectionFactory, context);
        bind_data->InitOptionsFromWhereClause(where_clause);
        bind_data->InitAndVerifyFields(fields);

        names = bind_data->GetRfcColumnNames();
        return_types = bind_data->GetReturnTypes();

        return std::move(bind_data);
    }

    static unique_ptr<GlobalTableFunctionState> RfcReadTableInitGlobalState(ClientContext &context,
                                                                            TableFunctionInitInput &input) 
    {
        auto &bind_data = input.bind_data->CastNoConst<RfcReadTableBindData>();
        auto column_ids = input.column_ids;

        bind_data.ActivateColumns(column_ids);
        bind_data.AddOptionsFromFilters(input.filters);

        return make_uniq<GlobalTableFunctionState>();
    }

    static void RfcReadTableScan(ClientContext &context, 
                                 TableFunctionInput &data, 
                                 DataChunk &output) 
    {
        auto &bind_data = data.bind_data->CastNoConst<RfcReadTableBindData>();
        if (! bind_data.HasMoreResults()) {
            return;
        }

        //printf(">> RfcReadTableScan\n");
        bind_data.Step(context, output);
    }

    double RfcReadTableProgress(ClientContext &, const FunctionData *func_data, const GlobalTableFunctionState *)
    {
        
        auto &bind_data = func_data->CastNoConst<RfcReadTableBindData>();
        auto progress = bind_data.GetProgress();

        //printf(">> RfcReadTableProgress %f\n", progress);
        return progress;
    }

    CreateTableFunctionInfo CreateRfcReadTableScanFunction() 
    {
        auto fun = TableFunction("sap_read_table", { LogicalType::VARCHAR }, 
                                 RfcReadTableScan, 
                                 RfcReadTableBind, 
                                 RfcReadTableInitGlobalState);
        fun.named_parameters["THREADS"] = LogicalType::UINTEGER;
        fun.named_parameters["COLUMNS"] = LogicalType::LIST(LogicalType::VARCHAR);
        fun.named_parameters["FILTER"] = LogicalType::VARCHAR;
        fun.named_parameters["MAX_ROWS"] = LogicalType::UINTEGER;
        fun.table_scan_progress = RfcReadTableProgress;
        fun.to_string = RfcReadTableToString;
        fun.projection_pushdown = true;
        fun.filter_pushdown = true;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb