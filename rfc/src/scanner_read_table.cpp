#include <regex>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "duckdb/parallel/pipeline.hpp"
#include "duckdb/parallel/event.hpp"

#include "scanner_read_table.hpp"
#include "duckdb_argument_helper.hpp"
#include "sap_rfc.hpp"
#include "telemetry.hpp"
#include "erpl_telemetry.hpp"

namespace duckdb 
{
    static unique_ptr<FunctionData> RfcReadTableBind(ClientContext &context, 
                                                     TableFunctionBindInput &input, 
                                                     vector<LogicalType> &return_types, 
                                                     vector<string> &names) 
    {
        PostHogTelemetry::Instance().RecordFunctionCall("sap_read_table");

        // Telemetry: rfc_table_read fires once per statement (bind), timing the
        // SAP metadata round-trip. Emits feature_used {feature, duration_ms} on
        // success; a failure below emits an enumerated $exception. No table name,
        // column list, FILTER/where clause or SQL is ever sent.
        erpl_telemetry::ScopedFeature feat_timer(erpl_telemetry::feature::kRfcTableRead);

        auto table_name = input.inputs[0].ToString();
        auto &named_params = input.named_parameters;
        // Per-query `threads` wins; otherwise the erpl_rfc_max_threads session default;
        // otherwise 0, which means erpl decides (one RFC call per projected column).
        auto max_read_threads = named_params.find("THREADS") != named_params.end() 
                                    ? named_params["THREADS"].GetValue<unsigned int>()
                                    : GetRfcMaxThreads();
        auto limit = named_params.find("MAX_ROWS") != named_params.end() 
                                    ? named_params["MAX_ROWS"].GetValue<unsigned int>()
                                    : 0;
        auto where_clause = named_params.find("FILTER") != named_params.end() 
                                ? named_params["FILTER"].ToString()
                                : "";
        auto read_table_function = named_params.find("READ_TABLE_FUNCTION") != named_params.end()
                                ? named_params["READ_TABLE_FUNCTION"].ToString()
                                : "RFC_READ_TABLE";
        auto read_table_delimiter = named_params.find("READ_TABLE_DELIMITER") != named_params.end()
                                ? named_params["READ_TABLE_DELIMITER"].ToString()
                                : "";
        auto read_table_function_user_set = named_params.find("READ_TABLE_FUNCTION") != named_params.end();
        
        auto fields = named_params.find("COLUMNS") != named_params.end() 
                            ? ConvertListValueToVector<std::string>(named_params["COLUMNS"])
                            : std::vector<std::string>();

        auto secret_name = named_params.find("SECRET") != named_params.end()
                                ? named_params["SECRET"].ToString()
                                : "";

        // `fetch_size` is the shared name across sap_read_table, sap_odp_read_* and the
        // BICS scanners.  Each protocol keeps its own natural unit -- here it is
        // concurrent result rows, which is what bounds the SAP SDK's own buffer.
        auto fetch_size = named_params.find("FETCH_SIZE") != named_params.end()
                                ? named_params["FETCH_SIZE"].GetValue<unsigned int>()
                                : 0;

        auto bind_data = make_uniq<RfcReadTableBindData>(table_name, max_read_threads, limit,
                                                         read_table_function, read_table_delimiter, read_table_function_user_set,
                                                         &DefaultRfcConnectionFactory, context);
        if (!secret_name.empty()) {
            bind_data->SetSecretName(secret_name);
        }
        bind_data->SetFetchSize(fetch_size);
        bind_data->InitOptionsFromWhereClause(where_clause);
        try {
            bind_data->InitAndVerifyFields(fields);
        } catch (...) {
            feat_timer.Cancel();
            erpl_telemetry::CaptureError(erpl_telemetry::error_class::kRfcError,
                                         erpl_telemetry::feature::kRfcTableRead,
                                         erpl_telemetry::phase::kRead);
            throw;
        }

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

        // Loop, because an empty chunk is how a table function says "scan finished".
        // Residual filtering can legitimately reject every row of a batch, and returning
        // that empty chunk would end the scan and silently discard everything still
        // unread.  Observed on DD03L: the scan stopped at the first fully-rejected batch
        // and returned 25,578 rows instead of 114,566.  Keep pulling until a batch has a
        // surviving row or the table is genuinely exhausted.
        while (true) {
            if (! bind_data.HasMoreResults()) {
#ifdef __GLIBC__
                // Scan finished: per-column SDK handles were released at FINISHED
                // and the streaming reader holds no whole-batch buffers, so hand
                // the emptied allocator arenas back to the OS instead of letting
                // RSS linger as glibc free-list fragmentation (issue #69).
                malloc_trim(0);
#endif
                return;
            }

            bind_data.Step(context, output);
            if (! bind_data.HasResidualFilters()) {
                return;
            }

            // DuckDB removed these from the plan when it handed them to us, so if we do
            // not apply them nobody does.  See ApplyResidualFilters.
            bind_data.ApplyResidualFilters(output);
            if (output.size() > 0) {
                return;
            }

            // Nothing survived.  Reset before the next Step: ApplyResidualFilters sliced
            // the chunk, leaving dictionary vectors that Step would otherwise write
            // through as if they were flat.
            output.Reset();
        }
    }

    double RfcReadTableProgress(ClientContext &, const FunctionData *func_data, const GlobalTableFunctionState *)
    {
        
        auto &bind_data = func_data->CastNoConst<RfcReadTableBindData>();
        auto progress = bind_data.GetProgress();

        //printf(">> RfcReadTableProgress %f\n", progress);
        return progress;
    }

    TableFunction CreateRfcReadTableScanFunction() 
    {
        auto fun = TableFunction("sap_read_table", { LogicalType::VARCHAR }, 
                                 RfcReadTableScan, 
                                 RfcReadTableBind, 
                                 RfcReadTableInitGlobalState);
        fun.named_parameters["THREADS"] = LogicalType::UINTEGER;
        fun.named_parameters["FETCH_SIZE"] = LogicalType::UINTEGER;
        fun.named_parameters["COLUMNS"] = LogicalType::LIST(LogicalType::VARCHAR);
        fun.named_parameters["FILTER"] = LogicalType::VARCHAR;
        fun.named_parameters["MAX_ROWS"] = LogicalType::UINTEGER;
        fun.named_parameters["READ_TABLE_FUNCTION"] = LogicalType::VARCHAR;
        fun.named_parameters["READ_TABLE_DELIMITER"] = LogicalType::VARCHAR;
        fun.named_parameters["SECRET"] = LogicalType::VARCHAR;
        fun.table_scan_progress = RfcReadTableProgress;
        fun.projection_pushdown = true;
        fun.filter_pushdown = true;

        return fun;
    }

} // namespace duckdb
