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
        if (named_params.find("PARTITIONS") != named_params.end()) {
            bind_data->SetPartitionCount((idx_t)named_params["PARTITIONS"].GetValue<unsigned int>());
        }
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
        // The serial path computes this inside Step(); a partitioned scan needs it
        // before any worker starts, and it must not be written afterwards.
        bind_data.ResolveEffectiveMaxBatchSize();

        // Partitioning is opt-in.  With it off this returns a state whose MaxThreads()
        // is 1 and whose scheduler is null, so DuckDB creates one worker and the scan
        // runs the column-parallel path exactly as it always has.
        auto partitions = bind_data.GetPartitionCount();
        if (partitions <= 1) {
            return make_uniq<RfcReadTableGlobalState>(1, nullptr);
        }

        // Use the same batch size the unpartitioned path warms up to, not
        // STANDARD_VECTOR_SIZE.  A partitioned window cannot let the batch double --
        // that would break ROWSKIPS alignment -- so whatever is chosen here is the size
        // for the whole scan.  Pinning it at 2048 made a 131k-row window cost 64 RFC
        // round-trips where the serial path's warm-up reaches 32768 and needs a
        // handful, and measured ~2x SLOWER than not partitioning at all.
        auto batch_size = (idx_t)bind_data.GetEffectiveMaxBatchSize();
        if (batch_size == 0) {
            batch_size = (idx_t)STANDARD_VECTOR_SIZE;
        }

        // One batch per window by default: the finest granularity that still issues
        // full-size RFC calls, so workers share the table evenly instead of one worker
        // taking a huge window while the rest idle.
        auto window = bind_data.GetPartitionWindowRows();
        if (window == 0) {
            window = batch_size;
        }
        auto scheduler = make_uniq<RfcRowWindowScheduler>(window, batch_size,
                                                          (idx_t)bind_data.GetLimit());
        return make_uniq<RfcReadTableGlobalState>(partitions, std::move(scheduler));
    }

    static unique_ptr<LocalTableFunctionState> RfcReadTableInitLocalState(ExecutionContext &context,
                                                                          TableFunctionInitInput &input,
                                                                          GlobalTableFunctionState *global_state)
    {
        auto &gstate = global_state->Cast<RfcReadTableGlobalState>();
        if (! gstate.IsPartitioned()) {
            return nullptr;
        }

        auto &bind_data = input.bind_data->CastNoConst<RfcReadTableBindData>();
        auto local = make_uniq<RfcReadTableWindowState>();
        local->machines = bind_data.CreateWindowStateMachines();
        return std::move(local);
    }

    // Drive one worker of a partitioned scan.
    static void RfcReadTableScanPartitioned(ClientContext &context,
                                            RfcReadTableBindData &bind_data,
                                            RfcReadTableGlobalState &gstate,
                                            RfcReadTableWindowState &lstate,
                                            DataChunk &output)
    {
        while (true) {
            if (! lstate.holds_window) {
                idx_t offset = 0, rows = 0;
                if (! gstate.scheduler->Claim(offset, rows)) {
                    // Nothing left to claim; an empty chunk retires this worker.
                    return;
                }
                for (auto &sm : lstate.machines) {
                    if (sm.Active()) {
                        sm.SetWindow(offset, (unsigned int)gstate.scheduler->BatchSize(),
                                     (unsigned int)rows);
                    }
                }
                lstate.holds_window = true;
                lstate.window_rows = rows;
            }

            bool window_finished = true;
            for (auto &sm : lstate.machines) {
                if (sm.Active() && ! sm.Finished()) {
                    window_finished = false;
                    break;
                }
            }

            if (! window_finished) {
                bind_data.StepWindow(context, output, lstate.machines);
                bind_data.ApplyResidualFilters(output);
                if (output.size() > 0) {
                    return;
                }
                // Everything in this batch was filtered out.  Reset before the next
                // step: ApplyResidualFilters sliced the chunk, and Step would otherwise
                // write through dictionary vectors as if they were flat.
                output.Reset();
                continue;
            }

            // The window is done.  If it yielded fewer rows than it asked for, the
            // table ended inside it -- tell the scheduler to stop handing out new
            // windows.  Workers already holding one still finish it.
            idx_t produced = 0;
            for (auto &sm : lstate.machines) {
                if (sm.Active()) {
                    produced = sm.GetTotalRows();
                    break;
                }
            }
            if (produced < lstate.window_rows) {
                gstate.scheduler->ReportExhausted();
            }
            lstate.holds_window = false;
            output.Reset();
        }
    }

    static void RfcReadTableScan(ClientContext &context, 
                                 TableFunctionInput &data, 
                                 DataChunk &output) 
    {
        auto &bind_data = data.bind_data->CastNoConst<RfcReadTableBindData>();

        if (data.global_state) {
            auto &gstate = data.global_state->Cast<RfcReadTableGlobalState>();
            if (gstate.IsPartitioned()) {
                if (data.local_state == nullptr) {
                    return;
                }
                auto &lstate = data.local_state->Cast<RfcReadTableWindowState>();
                RfcReadTableScanPartitioned(context, bind_data, gstate, lstate, output);
                return;
            }
        }

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
                                 RfcReadTableInitGlobalState,
                                 RfcReadTableInitLocalState);
        fun.named_parameters["THREADS"] = LogicalType::UINTEGER;
        fun.named_parameters["FETCH_SIZE"] = LogicalType::UINTEGER;
        fun.named_parameters["PARTITIONS"] = LogicalType::UINTEGER;
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
