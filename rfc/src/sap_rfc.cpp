#include "duckdb.hpp"
#include <cmath>
#include <iostream>
#include <numeric>
#include <set>
#include <thread>  

#include <stdint.h>
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/storage/table/row_group.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/filter/dynamic_filter.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/planner/filter/struct_filter.hpp"

#include "sap_rfc.hpp"
#include "sap_function.hpp"
#include "duckdb_argument_helper.hpp"
#include "erpl_tracing.hpp"

namespace duckdb
{
    // Persistent-connection toggle.  Default true: the scan path caches one
    // RFC connection + function descriptor per RfcReadColumnStateMachine for
    // the lifetime of the scan instead of opening + RfcGetFunctionDesc'ing on
    // every batch.  Toggle via `SET erpl_rfc_persistent_connections=false`
    // to fall back to per-batch open/close (useful for A/B benchmarking or
    // working around SDK regressions).
    static std::atomic<bool> g_rfc_persistent_connections{true};
    void SetRfcPersistentConnections(bool enabled) { g_rfc_persistent_connections.store(enabled, std::memory_order_relaxed); }
    bool GetRfcPersistentConnections()             { return g_rfc_persistent_connections.load(std::memory_order_relaxed); }

    // Hard ceiling on persistent connections per scan.  Wide tables (BSEG,
    // ACDOCA) have more columns than realistic SAP-side resources can
    // serve in parallel (CPIC default 200, gateway max_conn ~500, DWP pool,
    // HANA worker limits).  State machines past the cap fall back to
    // per-batch open/close.  See [[project_rfc_intra_process_throughput_cap]]
    // — effective concurrency against the SAP gateway tops out around 3-4
    // even with unlimited connections, so 16 leaves ample headroom while
    // staying well under any plausible SAP-side limit.
    static std::atomic<unsigned int> g_rfc_max_persistent_connections{16};
    void SetRfcMaxPersistentConnections(unsigned int n) { g_rfc_max_persistent_connections.store(n, std::memory_order_relaxed); }
    unsigned int GetRfcMaxPersistentConnections()       { return g_rfc_max_persistent_connections.load(std::memory_order_relaxed); }

    // Whether SQL predicates are translated into RFC_READ_TABLE's OPTIONS table.
    // Pushdown is a pure optimisation -- DuckDB re-evaluates every filter regardless --
    // so turning this off can only cost throughput, never change results.  That is
    // exactly what makes it the test suite's oracle: the same query with it on and off
    // must return the same rows.  It is also the escape hatch if a particular SAP
    // release rejects the generated syntax.
    static std::atomic<bool> g_rfc_pushdown_filters{true};
    void SetRfcPushdownFilters(bool enabled) { g_rfc_pushdown_filters.store(enabled, std::memory_order_relaxed); }
    bool GetRfcPushdownFilters()             { return g_rfc_pushdown_filters.load(std::memory_order_relaxed); }

    // Session default for the `threads` named parameter.  0 means "erpl decides",
    // which today is one RFC call per projected column.
    static std::atomic<unsigned int> g_rfc_max_threads{0};
    void SetRfcMaxThreads(unsigned int n) { g_rfc_max_threads.store(n, std::memory_order_relaxed); }
    unsigned int GetRfcMaxThreads()       { return g_rfc_max_threads.load(std::memory_order_relaxed); }

    // Concurrent-row budget that bounds the SAP SDK result buffer on wide
    // sap_read_table scans (issue #69).  See MaxBatchSizeForColumnCount.
    static std::atomic<unsigned int> g_rfc_read_table_batch_budget{RfcReadColumnStateMachine::DEFAULT_READ_TABLE_BATCH_BUDGET};
    void SetRfcReadTableBatchBudget(unsigned int n) { g_rfc_read_table_batch_budget.store(n, std::memory_order_relaxed); }
    unsigned int GetRfcReadTableBatchBudget()       { return g_rfc_read_table_batch_budget.load(std::memory_order_relaxed); }

    string RfcFunctionDesc(ClientContext &context, const FunctionParameters &parameters)
    {
        auto auth_params = RfcAuthParams::FromContext(context);
        auto connection = auth_params.Connect();
        auto func_name = StringValue::Get(parameters.values[0]);
        auto func = make_uniq<RfcFunction>(connection, func_name);

        auto params = func->GetParameterInfos();

        std::stringstream ss;
        // Iterate over the params vector and include parameter names as literals
        for (auto it = params.begin(); it != params.end(); ++it)
        {
            // Start building the SQL SELECT statement for each parameter
            ss << "SELECT '" << it->GetName() << "' AS parameter_name";
            ss << ", '" << it->GetRfcTypeAsString() << "' AS parameter_type";
            ss << ", '" << it->GetLength() << "' AS parameter_length";
            ss << ", '" << it->GetDirectionAsString() << "' AS parameter_direction";
            ss << ", '" << it->IsOptional() << "' AS parameter_is_optional";
            ss << ", '" << it->GetDescription() << "' AS parameter_description";
            ss << ", " << it->GetRfcType()->ToSqlLiteral() << " AS parameter_type_declaration";

            // Add a UNION ALL clause between SELECT statements, except for the last one
            if (std::next(it) != params.end())
            {
                ss << " UNION ALL ";
            }
        }

        auto pragma_query = ss.str();
        return pragma_query;
    }

    // --------------------------------------------------------------------------------------------

    std::shared_ptr<RfcConnection> DefaultRfcConnectionFactory(ClientContext &context)
    {
        return RfcAuthParams::FromContext(context).Connect();
    }

    RfcReadTableBindData::RfcReadTableBindData(std::string table_name, 
                                               int max_read_threads, 
                                               unsigned int limit,
                                               std::string read_table_function,
                                               std::string read_table_delimiter,
                                               bool read_table_function_user_set,
                                               RfcConnectionFactory_t connection_factory, 
                                               ClientContext &client_context)
        : table_name(table_name),
          limit(limit),
          max_threads(max_read_threads),
          read_table_function(std::move(read_table_function)),
          read_table_delimiter(std::move(read_table_delimiter)),
          read_table_function_user_set(read_table_function_user_set),
          connection_factory(connection_factory),
          client_context(client_context)
    { }

    RfcReadTableBindData::RfcReadTableBindData(std::string table_name,
                                               int max_read_threads,
                                               unsigned int limit,
                                               RfcConnectionFactory_t connection_factory,
                                               ClientContext &client_context)
        : RfcReadTableBindData(std::move(table_name), max_read_threads, limit,
                               "RFC_READ_TABLE", "", false, connection_factory, client_context)
    { }

    std::vector<std::string> RfcReadTableBindData::GetRfcColumnNames()
    {
        return column_names;
    }

    duckdb::vector<Value> RfcReadTableBindData::GetRfcColumnName(unsigned int column_idx) 
    {
        if (column_idx >= column_names.size()) {
            throw std::runtime_error(StringUtil::Format("Column index %d out of bounds", column_idx));
        }
        auto arg_builder = ArgBuilder().Add("FIELDNAME", Value(column_names[column_idx]));
        auto ret = duckdb::vector<Value>();
        ret.push_back(arg_builder.Build());
        return ret;
    }

    std::string RfcReadTableBindData::GetProjectedColumnName(unsigned int projected_column_idx) 
    {
        auto rfc_column_idx = DConstants::INVALID_INDEX;
        for (auto &sm : column_state_machines) {
            if (sm.GetProjectedColumnIndex() == projected_column_idx) {
                rfc_column_idx = sm.GetRfcColumnIndex();
                break;
            }
        }

        if (rfc_column_idx == DConstants::INVALID_INDEX) {
            throw std::runtime_error(StringUtil::Format("Could not find column with projected index %d", projected_column_idx));
        }

        return column_names[rfc_column_idx];
    }

    std::vector<LogicalType> RfcReadTableBindData::GetReturnTypes()
    {
        std::vector<LogicalType> ret(column_types.size());
        std::transform(column_types.begin(), column_types.end(), ret.begin(), [](auto &t) {
            return t.CreateDuckDbType();
        });
        return ret;
    }

    RfcType RfcReadTableBindData::GetColumnType(unsigned int column_idx)
    {
        return column_types[column_idx];
    }

    duckdb::vector<Value> RfcReadTableBindData::GetOptions()
    {
        auto ret = duckdb::vector<Value>();
        for (auto &o : options) {
            auto arg_builder = ArgBuilder().Add("TEXT", Value(o));
            ret.push_back(arg_builder.Build());
        }

        return ret;
    }

    std::shared_ptr<RfcConnection> RfcReadTableBindData::OpenNewConnection()
    {
        if (!secret_name.empty()) {
            return RfcAuthParams::FromContext(client_context, secret_name).Connect();
        }
        return connection_factory(client_context);
    }

    std::string RfcReadTableBindData::GetReadTableFunctionName()
    {
        return read_table_function;
    }

    std::string RfcReadTableBindData::GetReadTableDelimiter()
    {
        return read_table_delimiter;
    }

    bool RfcReadTableBindData::IsReadTableFunctionUserSet()
    {
        return read_table_function_user_set;
    }

    bool RfcReadTableBindData::TryReservePersistentSlot()
    {
        // Lock-free fetch-and-cap.  Each caller atomically increments the
        // counter; the first MAX callers see a previous value below the
        // cap and win the slot, the rest see a value at-or-above the cap
        // and lose.  We deliberately do not decrement on failure (so the
        // global counter remains monotonically tracking attempted slots
        // and stays a clean ceiling without an ABA window).
        auto cap = GetRfcMaxPersistentConnections();
        if (cap == 0) {
            return false;
        }
        auto previous = persistent_slots_used.fetch_add(1, std::memory_order_relaxed);
        if (previous >= cap) {
            return false;
        }
        return true;
    }

    void RfcReadTableBindData::ValidateReadTableFunctionName()
    {
        static const std::set<std::string> allowed_functions = {
            "RFC_READ_TABLE",
            "/BODS/RFC_READ_TABLE",
            "/SAPDS/RFC_READ_TABLE",
            "/BODS/RFC_READ_TABLE2",
            "/SAPDS/RFC_READ_TABLE2"
        };

        if (allowed_functions.find(read_table_function) == allowed_functions.end()) {
            throw std::runtime_error(StringUtil::Format(
                "Unsupported READ_TABLE_FUNCTION '%s'. Supported values: RFC_READ_TABLE, /BODS/RFC_READ_TABLE, "
                "/SAPDS/RFC_READ_TABLE, /BODS/RFC_READ_TABLE2, /SAPDS/RFC_READ_TABLE2.",
                read_table_function));
        }
    }

    bool RfcReadTableBindData::ReadTableFunctionSupportsEtData(std::shared_ptr<RfcConnection> connection, const std::string &function_name)
    {
        auto func = std::make_shared<RfcFunction>(connection, function_name);
        auto result_infos = func->GetResultInfos();
        auto it = std::find_if(result_infos.begin(), result_infos.end(), [](auto &param) {
            return param.GetName() == "ET_DATA";
        });
        return it != result_infos.end();
    }

    bool RfcReadTableBindData::ReadTableSupportsEtDataSwitch(std::shared_ptr<RfcConnection> connection, const std::string &function_name)
    {
        auto func = std::make_shared<RfcFunction>(connection, function_name);
        bool has_param = false;
        for (auto &param : func->GetParameterInfos()) {
            if (param.GetName() == "USE_ET_DATA_4_RETURN") {
                has_param = true;
                break;
            }
        }
        if (!has_param) {
            return false;
        }
        return ReadTableFunctionSupportsEtData(connection, function_name);
    }

    void RfcReadTableBindData::ResolveReadTableFunctionForStringTypes(std::shared_ptr<RfcConnection> connection)
    {
        if (read_table_supports_et_data.has_value()) {
            return;
        }

        if (read_table_function.empty()) {
            read_table_function = "RFC_READ_TABLE";
        }

        ValidateReadTableFunctionName();

        bool supports_et_data = false;
        try {
            supports_et_data = ReadTableFunctionSupportsEtData(connection, read_table_function);
        } catch (std::exception &) {
            supports_et_data = false;
        }

        read_table_supports_et_data = supports_et_data;
    }

    bool RfcReadTableBindData::ReadTableSupportsEtData()
    {
        return read_table_supports_et_data.value_or(false);
    }

    bool RfcReadTableBindData::TrySelectFallbackReadTableFunction(std::shared_ptr<RfcConnection> connection)
    {
        if (read_table_function_user_set) {
            return false;
        }

        static const std::vector<std::string> fallback_functions = {
            "/SAPDS/RFC_READ_TABLE2",
            "/BODS/RFC_READ_TABLE2",
            "/SAPDS/RFC_READ_TABLE",
            "/BODS/RFC_READ_TABLE",
            "RFC_READ_TABLE"
        };

        for (auto &candidate : fallback_functions) {
            if (candidate == read_table_function) {
                continue;
            }
            try {
                if (ReadTableFunctionSupportsEtData(connection, candidate)) {
                    read_table_function = candidate;
                    read_table_supports_et_data = true;
                    ERPL_TRACE_INFO_DATA("sap_rfc", "Using RFC_READ_TABLE fallback", candidate);
                    return true;
                }
            } catch (std::exception &) {
                continue;
            }
        }

        return false;
    }

    void RfcReadTableBindData::ResolveReadTableResultPath(std::shared_ptr<RfcConnection> connection)
    {
        if (!read_table_result_path.empty()) {
            return;
        }

        if (read_table_function == "RFC_READ_TABLE") {
            read_table_result_path = "/DATA";
            return;
        }

        auto func = std::make_shared<RfcFunction>(connection, read_table_function);
        auto result_infos = func->GetResultInfos();
        static const std::vector<std::string> table_candidates = {
            "TBLOUT30000",
            "TBLOUT8192",
            "TBLOUT2048",
            "TBLOUT512",
            "TBLOUT128"
        };

        for (auto &candidate : table_candidates) {
            auto it = std::find_if(result_infos.begin(), result_infos.end(), [&](auto &param) {
                return param.GetName() == candidate;
            });
            if (it != result_infos.end()) {
                read_table_result_path = "/" + candidate;
                return;
            }
        }

        // fallback to DATA if present
        auto it = std::find_if(result_infos.begin(), result_infos.end(), [&](auto &param) {
            return param.GetName() == "DATA";
        });
        if (it != result_infos.end()) {
            read_table_result_path = "/DATA";
        }
    }

    void RfcReadTableBindData::ResolveReadTableImportParams(std::shared_ptr<RfcConnection> connection)
    {
        if (!read_table_import_params.empty()) {
            return;
        }

        auto func = std::make_shared<RfcFunction>(connection, read_table_function);
        for (auto &param : func->GetParameterInfos()) {
            auto direction = param.GetDirection();
            if (direction == RFC_EXPORT) {
                continue;
            }
            read_table_import_params.insert(param.GetName());
        }
    }

    bool RfcReadTableBindData::ReadTableHasParam(const std::string &param_name)
    {
        return read_table_import_params.find(param_name) != read_table_import_params.end();
    }

    void RfcReadTableBindData::InitOptionsFromWhereClause(std::string &where_clause)
    {
        options.clear();
        AddOptionsFromWhereClause(where_clause);
    }

    std::vector<std::string> RfcReadTableBindData::ChunkWhereClause(const std::string &where_clause,
                                                                    idx_t max_len)
    {
        std::vector<std::string> parts;
        if (where_clause.empty()) {
            return parts;
        }

        // Mark every position that sits inside a quoted literal.  ABAP escapes an
        // apostrophe by doubling it, and a naive flip-on-every-quote scan handles that
        // for free: the pair flips the state twice and nets out.
        std::vector<bool> in_literal(where_clause.size(), false);
        bool inside = false;
        for (idx_t i = 0; i < where_clause.size(); i++) {
            if (where_clause[i] == '\'') {
                inside = ! inside;
                in_literal[i] = true;
            } else {
                in_literal[i] = inside;
            }
        }

        idx_t start = 0;
        while (start < where_clause.size()) {
            if (where_clause.size() - start <= max_len) {
                parts.push_back(where_clause.substr(start));
                break;
            }

            // Walk back from the hard limit to the last whitespace that is NOT inside a
            // literal.  Breaking inside one -- which the previous whitespace-only rule
            // did whenever a literal contained a space -- leaves an unbalanced
            // apostrophe on both lines and a syntax error on the ABAP side.
            idx_t split = start + max_len;
            while (split > start &&
                   ! (std::isspace(static_cast<unsigned char>(where_clause[split])) && ! in_literal[split])) {
                --split;
            }

            if (split == start) {
                throw std::runtime_error("Could not split WHERE clause into options, "
                                         "the maximal length of a single part of the "
                                         "clause is 70 characters.");
            }

            parts.push_back(where_clause.substr(start, split - start));
            start = split;
        }

        return parts;
    }

    void RfcReadTableBindData::AddOptionsFromWhereClause(std::string &where_clause)
    {
        for (auto &part : ChunkWhereClause(where_clause, MAX_OPTION_LEN)) {
            options.push_back(part);
        }
    }

    void RfcReadTableBindData::InitAndVerifyFields(std::vector<std::string> req_fields)
    {
        if (read_table_function.empty()) {
            read_table_function = "RFC_READ_TABLE";
        }
        ValidateReadTableFunctionName();

        auto connection = OpenNewConnection();
        auto available_fields = GetTableFieldMetas(connection, table_name);
        auto req_field_metas = std::map<std::string, Value>();

        if (req_fields.empty()) {
            req_fields = std::vector<std::string>();
            
            std::transform(available_fields.begin(), available_fields.end(), std::back_inserter(req_fields),
                           [](auto& fm) { return ValueHelper(fm)["FIELDNAME"].ToString(); });
        }

        for (auto &req_field : req_fields) 
        {
            auto req_field_it = std::find_if(available_fields.begin(), available_fields.end(), 
                                             [&req_field](auto &fm) { 
                                                 auto fm_helper = ValueHelper(fm);
                                                 auto field_name = fm_helper["FIELDNAME"].ToString();
                                                 return field_name == req_field;
                                             });
            if (req_field_it == available_fields.end()) {
                throw std::runtime_error(StringUtil::Format("Could not find field %s in table %s", req_field, table_name));
            }

            req_field_metas[req_field] = *req_field_it;
        }

        column_names.clear();
        column_names = req_fields;
        
        column_types.clear();
        client_columns.clear();
        for (auto &req_field : req_fields) {
            auto fm = req_field_metas[req_field];
            auto rfc_type = GetRfcTypeForFieldMeta(fm);
            column_types.push_back(rfc_type);

            // The client is implicit in the RFC call itself, and RFC_READ_TABLE's parser
            // rejects any OPTIONS clause that names the client field ("The client field
            // ... "). A join on MANDT produces exactly such a predicate, so remember
            // which columns are CLNT and keep them out of the generated WHERE.
            // DATATYPE is the only place this is visible -- RfcType maps CLNT onto
            // RFCTYPE_CHAR, so by then it is indistinguishable from an ordinary CHAR.
            if (ValueHelper(fm)["DATATYPE"].ToString() == "CLNT") {
                client_columns.insert(req_field);
            }
        }

        auto needs_string_support = std::any_of(column_types.begin(), column_types.end(), [](auto &t) {
            return t.IsStringType();
        });
        if (needs_string_support) {
            ResolveReadTableFunctionForStringTypes(connection);
            // Eagerly resolve the et_data switch availability on this function
            // module so parallel column tasks don't race on it at execute
            // time. The function signature is stable across the session, so
            // probing once during bind is equivalent to the lazy path.
            if (read_table_function == "RFC_READ_TABLE" && !read_table_supports_et_data_switch.has_value()) {
                try {
                    read_table_supports_et_data_switch = ReadTableSupportsEtDataSwitch(connection, read_table_function);
                } catch (std::exception &) {
                    // Probe failed (e.g. transient metadata fetch error).  Store
                    // a definite `false` so parallel column tasks at execute time
                    // never race to write this optional; the existing
                    // !value() branch below will surface the capability error.
                    read_table_supports_et_data_switch = false;
                }
            }
        }

        ResolveReadTableImportParams(connection);
        ResolveReadTableResultPath(connection);

        column_state_machines = CreateReadColumnStateMachines();
    }

    std::vector<RfcReadColumnStateMachine> RfcReadTableBindData::CreateReadColumnStateMachines() 
    {
        auto ret = std::vector<RfcReadColumnStateMachine>();
        for (idx_t i = 0; i < column_names.size(); i++) {
            ret.push_back(RfcReadColumnStateMachine(this, i, limit));
        }
        return ret;
    }

    void RfcReadTableBindData::ActivateColumns(vector<column_t> &column_ids) 
    {
        for (auto &sm : column_state_machines) {
            sm.SetInactive();
        }

        for (idx_t i = 0; i < column_ids.size(); i++) {
            auto is_row_id = IsRowIdColumnId(column_ids[i]);
            auto column_id = is_row_id ? 0 : column_ids[i];
            column_state_machines[column_id].SetActive(i);
            if (is_row_id) {
                column_state_machines[column_id].SetRowIdColumnId();
            }
        }
    }

    // Evaluate one filter against one already-materialised value.
    //
    // Only ever used for predicates that could NOT be handed to SAP.  Row-at-a-time is
    // slow, but this path is the difference between right and wrong answers, and it
    // only runs for the residue.
    static bool FilterMatchesValue(const TableFilter &filter, const Value &val)
    {
        switch (filter.filter_type) {
            case TableFilterType::CONSTANT_COMPARISON: {
                auto &const_filter = filter.Cast<ConstantFilter>();
                // SQL three-valued logic: a comparison against NULL is never true.
                return val.IsNull() ? false : const_filter.Compare(val);
            }
            case TableFilterType::IS_NULL:
                return val.IsNull();
            case TableFilterType::IS_NOT_NULL:
                return ! val.IsNull();
            case TableFilterType::CONJUNCTION_AND: {
                auto &conj = filter.Cast<ConjunctionAndFilter>();
                for (auto &child : conj.child_filters) {
                    if (! FilterMatchesValue(*child, val)) {
                        return false;
                    }
                }
                return true;
            }
            case TableFilterType::CONJUNCTION_OR: {
                auto &conj = filter.Cast<ConjunctionOrFilter>();
                for (auto &child : conj.child_filters) {
                    if (FilterMatchesValue(*child, val)) {
                        return true;
                    }
                }
                return false;
            }
            case TableFilterType::IN_FILTER: {
                auto &in_filter = filter.Cast<InFilter>();
                if (val.IsNull()) {
                    return false;
                }
                for (auto &v : in_filter.values) {
                    if (! v.IsNull() && v.type() == val.type() && v == val) {
                        return true;
                    }
                }
                return false;
            }
            case TableFilterType::OPTIONAL_FILTER: {
                // Enforced, not skipped.  DuckDB's own OptionalFilter::FilterSelection is
                // a no-op, but that is a performance choice rather than a statement that
                // the predicate may be violated: CheckStatistics delegates to the child
                // for zone-map pruning, and table_scan.cpp feeds the child into index-scan
                // comparison extraction.  Both eliminate rows, so the predicate is implied
                // by the query.  DuckDB can afford to skip the exact check because
                // something downstream re-applies it; here the filter was removed from the
                // plan, so nothing would.
                auto &optional_filter = filter.Cast<OptionalFilter>();
                return optional_filter.child_filter == nullptr
                     ? true
                     : FilterMatchesValue(*optional_filter.child_filter, val);
            }
            case TableFilterType::STRUCT_EXTRACT: {
                auto &struct_filter = filter.Cast<StructFilter>();
                if (val.IsNull()) {
                    return false;
                }
                auto &children = StructValue::GetChildren(val);
                if (struct_filter.child_idx >= children.size()) {
                    return true;
                }
                return FilterMatchesValue(*struct_filter.child_filter, children[struct_filter.child_idx]);
            }
            default:
                // An unrecognised filter kind must not silently delete rows.  Keeping the
                // row over-produces, which is the same thing that happened before this
                // function existed; dropping it would be a new way to be wrong.
                return true;
        }
    }

    // DuckDB removes a pushed-down filter from the plan entirely -- TableFunction's
    // `filter_pushdown` is documented as "if NOT supported a filter will be added",
    // so setting it true makes the scan solely responsible for every predicate it is
    // handed.  erpl pushed what RFC_READ_TABLE could express and silently ignored the
    // rest, so a predicate that did not translate returned unfiltered rows.  Confirmed
    // against the trial: `WHERE SEATS_MAX > 350` returned all 40 rows of /DMO/FLIGHT
    // instead of 14, and EXPLAIN shows no FILTER operator above SAP_READ_TABLE.
    //
    // Everything that could not be pushed is therefore applied here instead.
    void RfcReadTableBindData::ApplyResidualFilters(DataChunk &output)
    {
        if (residual_filters.empty() || output.size() == 0) {
            return;
        }

        SelectionVector sel(STANDARD_VECTOR_SIZE);
        idx_t kept = 0;
        for (idx_t row = 0; row < output.size(); row++) {
            bool matches = true;
            for (auto &entry : residual_filters) {
                auto val = output.GetValue(entry.first, row);
                if (! FilterMatchesValue(*entry.second, val)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                sel.set_index(kept++, row);
            }
        }

        if (kept != output.size()) {
            output.Slice(sel, kept);
        }
    }

    void RfcReadTableBindData::AddOptionsFromFilters(duckdb::optional_ptr<duckdb::TableFilterSet> filter_set) 
    {
        residual_filters.clear();
        if (filter_set == nullptr || filter_set->filters.empty()) {
            return;
        }

        // The kill switch only stops predicates reaching SAP; it must not stop them being
        // applied, or turning it off would change the answer rather than just the speed.
        if (! GetRfcPushdownFilters()) {
            ERPL_TRACE_DEBUG("sap_rfc", "Filter pushdown disabled by erpl_rfc_pushdown_filters");
            for (auto &[projected_column_idx, filter] : filter_set->filters) {
                residual_filters.emplace_back(projected_column_idx, filter->Copy());
            }
            return;
        }

        vector<std::string> filter_entries;
        bool pushed_filters = false;
        bool skipped_filters = false;
        for (auto &[projected_column_idx, filter] : filter_set->filters)
        {
            auto column_name = GetProjectedColumnName(projected_column_idx);
            // Never generate a predicate on the client field; SAP refuses the whole
            // clause if one appears.  DuckDB still needs the filter applied, so it goes
            // to the residual path rather than being dropped.
            auto transformed = client_columns.count(column_name) > 0
                             ? std::string()
                             : TransformFilter(column_name, *filter);
            if (!transformed.empty()) {
                filter_entries.push_back(transformed);
                pushed_filters = true;
            } else {
                skipped_filters = true;
                // Not expressible in ABAP -- so it has to be applied on this side.
                residual_filters.emplace_back(projected_column_idx, filter->Copy());
            }
        }

        if (!pushed_filters) {
            ERPL_TRACE_DEBUG("sap_rfc", "Skipping filter pushdown; RFC_READ_TABLE cannot represent any conditions");
            // residual_filters already holds every one of them.
            return;
        }

        // Log if we skipped some filters but still have some to push down
        if (skipped_filters) {
            ERPL_TRACE_INFO("sap_rfc", "Partial filter pushdown: some predicates not supported by RFC_READ_TABLE");
        }

        auto filter_string = StringUtil::Join(filter_entries, " AND ");

        if (! options.empty()) {
            filter_string = " AND " + filter_string;
        }   

        AddOptionsFromWhereClause(filter_string);
        ERPL_TRACE_DEBUG_DATA("sap_rfc", "Filter pushdown applied", filter_string);
    }

    std::string RfcReadTableBindData::TransformFilter(std::string &column_name, TableFilter &filter)
    {
        switch(filter.filter_type)
        {
            // DuckDB expresses `BETWEEN a AND b` on one column as a CONJUNCTION_AND of
            // two ConstantFilters, so without this a range window reaches the server as
            // nothing at all and the full column crosses the wire.
            //
            // All-or-nothing on purpose.  Dropping one arm of an AND widens the
            // predicate (harmless -- DuckDB re-filters), but dropping one arm of an OR
            // *narrows* it and would silently lose rows.  Rather than encode two
            // different rules, neither is pushed partially.
            case TableFilterType::CONJUNCTION_AND: {
                auto &conj = filter.Cast<ConjunctionAndFilter>();
                return TransformConjunction(column_name, conj.child_filters, "AND");
            }
            case TableFilterType::CONJUNCTION_OR: {
                auto &conj = filter.Cast<ConjunctionOrFilter>();
                return TransformConjunction(column_name, conj.child_filters, "OR");
            }
            case TableFilterType::CONSTANT_COMPARISON: {
                auto &const_filter = filter.Cast<ConstantFilter>();

                // TransformComparision maps all six comparison operators; ranges are
                // exactly the shape a date-window extract uses, and leaving them to
                // DuckDB means transferring the whole column first.
                std::string operator_string;
                switch (const_filter.comparison_type) {
                    case ExpressionType::COMPARE_EQUAL:
                    case ExpressionType::COMPARE_NOTEQUAL:
                    case ExpressionType::COMPARE_LESSTHAN:
                    case ExpressionType::COMPARE_GREATERTHAN:
                    case ExpressionType::COMPARE_LESSTHANOREQUALTO:
                    case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
                        operator_string = TransformComparision(const_filter.comparison_type);
                        break;
                    default:
                        return std::string();
                }

                // TransformLiteral doubles embedded apostrophes.  The previous
                // StringUtil::Format("'%s'", ...) did not, so a value like O'Brien
                // closed the literal early and changed what the predicate meant.
                auto constant_string = TransformLiteral(const_filter.constant);
                if (constant_string.empty()) {
                    return std::string();
                }

                // A literal that cannot fit on one 70-char OPTIONS line cannot be sent
                // at all, and the chunker would have to fail on it later.  Decline here,
                // where declining is still free.
                if (constant_string.size() > MAX_OPTION_LEN) {
                    return std::string();
                }

                return StringUtil::Format("%s %s %s", column_name, operator_string, constant_string);
            }
            case TableFilterType::OPTIONAL_FILTER: {
                auto &optional_filter = filter.Cast<OptionalFilter>();
		        return TransformFilter(column_name, *optional_filter.child_filter);
            }
            case TableFilterType::STRUCT_EXTRACT: {
                auto &struct_filter = filter.Cast<StructFilter>();
                auto child_name = KeywordHelper::WriteQuoted(struct_filter.child_name, '\"');
                auto new_name = "(" + column_name + ")." + child_name;
                return TransformFilter(new_name, *struct_filter.child_filter);
	        }
            case TableFilterType::IN_FILTER: {
                auto &in_filter = filter.Cast<InFilter>();
                if (in_filter.values.empty()) {
                    return std::string();
                }

                // The old cap was an arbitrary five values, which sent anything wider
                // across the wire in full.  The real constraint is the length of the
                // generated clause, so that is what is checked -- and when it is
                // exceeded the whole list is dropped rather than truncated, because a
                // truncated IN list is a different, narrower predicate.
                string in_list;
                for (auto &val : in_filter.values) {
                    auto literal = TransformLiteral(val);
                    if (literal.empty() || literal.size() > MAX_OPTION_LEN) {
                        return std::string();
                    }
                    if (! in_list.empty()) {
                        in_list += ", ";
                    }
                    in_list += literal;
                    if (in_list.size() > MAX_PUSHDOWN_CLAUSE_LEN) {
                        return std::string();
                    }
                }

                // Spaces inside the parentheses are deliberate: they give the 70-char
                // chunker somewhere legal to break a long list.
                return column_name + " IN ( " + in_list + " )";
            }
            case TableFilterType::DYNAMIC_FILTER: {
                return std::string();
            }
            case TableFilterType::IS_NOT_NULL: {
                // Not supported in RFC_READ_TABLE OPTIONS; skip pushdown
                return std::string();
            }
            case TableFilterType::IS_NULL: {
                // Not supported in RFC_READ_TABLE OPTIONS; skip pushdown
                return std::string();
            }
            default: {
                // For any other filter types, don't push down - let DuckDB handle them
                return std::string();
            }
        }
    }

    // Render a constant the way ABAP's dynamic WHERE expects it, or return "" to
    // decline pushing the predicate at all.
    //
    // The DDIC validates every literal against the field's own type, so a value
    // rendered in DuckDB's spelling is rejected outright: a DATE arrives as
    // '2020-01-01' and the server answers "is not a valid value for D(8,0)" --
    // breaking a query that previously worked, merely because it was never pushed
    // before.  Verified against the trial: quoted CHAR, quoted integers and quoted
    // decimals are all accepted, and DATS wants YYYYMMDD.
    //
    // Anything whose ABAP spelling is not established here is declined rather than
    // guessed.  Declining costs throughput; guessing wrong costs correctness.
    std::string RfcReadTableBindData::TransformLiteral(const Value &val) {
        if (val.IsNull()) {
            return std::string();
        }

        switch (val.type().id()) {
            case LogicalTypeId::VARCHAR:
                return KeywordHelper::WriteQuoted(val.ToString());

            // Quoted numerics are accepted for NUMC/INT/CURR/QUAN/DEC alike, and
            // quoting sidesteps any difference in how ABAP parses a bare number.
            case LogicalTypeId::TINYINT:
            case LogicalTypeId::SMALLINT:
            case LogicalTypeId::INTEGER:
            case LogicalTypeId::BIGINT:
            case LogicalTypeId::HUGEINT:
            case LogicalTypeId::UTINYINT:
            case LogicalTypeId::USMALLINT:
            case LogicalTypeId::UINTEGER:
            case LogicalTypeId::UBIGINT:
            case LogicalTypeId::DECIMAL:
                return KeywordHelper::WriteQuoted(val.ToString());

            case LogicalTypeId::DATE: {
                int32_t year, month, day;
                Date::Convert(DateValue::Get(val), year, month, day);
                return StringUtil::Format("'%04d%02d%02d'", year, month, day);
            }

            case LogicalTypeId::TIME: {
                int32_t hour, minute, second, micros;
                Time::Convert(TimeValue::Get(val), hour, minute, second, micros);

                // SAP TIMS is second-precision; DuckDB TIME is not.  Truncating the
                // fraction changes the predicate rather than approximating it:
                // `t < TIME '09:05:03.500'` must keep a row stored as 09:05:03, but
                // `t < '090503'` rejects it.  `>=` fails the mirror-image way.  Rounding
                // correctly would have to depend on the comparison operator, which this
                // function cannot see -- so decline and let the residual path handle it.
                if (micros != 0) {
                    return std::string();
                }
                return StringUtil::Format("'%02d%02d%02d'", hour, minute, second);
            }

            // Deliberately not pushed:
            //  - TIMESTAMP: SAP splits date and time across two fields, and UTCLONG
            //    is not ISO, so there is no single correct spelling here.
            //  - BLOB: TransformBlob emits Postgres' '\xAB'::BYTEA, which is not ABAP.
            //  - FLOAT/DOUBLE: round-tripping a binary float through a decimal
            //    literal can select a different set of rows than DuckDB would.
            //  - BOOLEAN and everything else: no established ABAP spelling.
            default:
                return std::string();
        }
    }

    std::string RfcReadTableBindData::TransformBlob(const string &val) {
        char const HEX_DIGITS[] = "0123456789ABCDEF";

        string result = "'\\x";
        for(idx_t i = 0; i < val.size(); i++) {
            uint8_t byte_val = static_cast<uint8_t>(val[i]);
            result += HEX_DIGITS[(byte_val >> 4) & 0xf];
            result += HEX_DIGITS[byte_val & 0xf];
        }
        result += "'::BYTEA";
        return result;
    }

    std::string RfcReadTableBindData::TransformConjunction(std::string &column_name,
                                                          vector<unique_ptr<TableFilter>> &children,
                                                          const std::string &op)
    {
        if (children.empty()) {
            return std::string();
        }

        std::string result;
        for (auto &child : children) {
            auto transformed = TransformFilter(column_name, *child);
            // One unrepresentable arm poisons the whole conjunction -- see the
            // all-or-nothing note at the call site.
            if (transformed.empty()) {
                return std::string();
            }
            if (! result.empty()) {
                result += " " + op + " ";
            }
            result += transformed;
            if (result.size() > MAX_PUSHDOWN_CLAUSE_LEN) {
                return std::string();
            }
        }

        // Explicit parentheses: this subtree may itself be an arm of an enclosing
        // conjunction, and ABAP's precedence is not worth relying on.
        return "( " + result + " )";
    }

    std::string RfcReadTableBindData::CreateExpression(string &column_name, vector<unique_ptr<TableFilter>> &filters, string op) 
    {
        auto filter_strings = std::vector<std::string>();
        for (auto &filter : filters) {
            auto transformed = RfcReadTableBindData::TransformFilter(column_name, *filter);
            if (!transformed.empty()) {
                filter_strings.push_back(transformed);
            }
        }

        if (filter_strings.empty()) {
            return std::string();
        }

        return StringUtil::Join(filter_strings, filter_strings.size(), op, [](auto &s) { return s; });
    }

    std::string RfcReadTableBindData::TransformComparision(ExpressionType type) {
        switch (type) {
            case ExpressionType::COMPARE_EQUAL:
                return "=";
            case ExpressionType::COMPARE_NOTEQUAL:
                // ABAP's dynamic WHERE does not accept '!='.
                return "<>";
            case ExpressionType::COMPARE_LESSTHAN:
                return "<";
            case ExpressionType::COMPARE_GREATERTHAN:
                return ">";
            case ExpressionType::COMPARE_LESSTHANOREQUALTO:
                return "<=";
            case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
                return ">=";
            default:
                throw NotImplementedException("Unsupported expression type");
        }
    }

    std::vector<Value> RfcReadTableBindData::GetTableFieldMetas(std::shared_ptr<RfcConnection> connection, std::string table_name)
    {
        auto args = ArgBuilder().Add("TABNAME", Value(table_name));
        auto func = std::make_shared<RfcFunction>(connection, "DDIF_FIELDINFO_GET");
        auto func_args = args.BuildArgList();
        auto invocation = func->BeginInvocation(func_args);
        auto result_set = invocation->Invoke();

        auto all_field_metas = ListValue::GetChildren(result_set->GetResultValue("/DFIES_TAB"));

        // Filter out non-data fields (e.g., NODE entries from CDS view compositions)
        std::vector<Value> field_metas;
        for (auto &fm : all_field_metas) {
            auto type_name = ValueHelper(fm)["DATATYPE"].ToString();
            if (type_name == "NODE") {
                continue;
            }
            field_metas.push_back(fm);
        }

        return field_metas;
    }

    RfcType RfcReadTableBindData::GetRfcTypeForFieldMeta(Value &DFIES_entry) 
    {
        auto entry_helper = ValueHelper(DFIES_entry);
        auto type_name = entry_helper["DATATYPE"].ToString();
        auto length = entry_helper["LENG"].GetValue<unsigned int>();
        auto decimals = entry_helper["DECIMALS"].GetValue<unsigned int>();

        return RfcType::FromTypeName(type_name, length, decimals);
    }

    bool RfcReadTableBindData::HasMoreResults() 
    {
        for (auto &sm : column_state_machines) {
            if (sm.Active() && !sm.Finished()) {
                return true;
            }
        }

        return false;
    }

    void RfcReadTableBindData::Step(ClientContext &context, DataChunk &output)
    {
        auto &scheduler = TaskScheduler::GetScheduler(context);

        // Snapshot the active state machines so we can throttle scheduling
        // without iterating column_state_machines twice.
        std::vector<RfcReadColumnStateMachine *> active;
        active.reserve(column_state_machines.size());
        for (auto &sm : column_state_machines) {
            if (sm.Active()) {
                active.push_back(&sm);
            }
        }

        // Bound the SAP SDK result buffer on wide scans by capping the warm-up
        // batch size to the active column count (issue #69).  Computed here —
        // outside any per-state-machine lock — and read locklessly by the
        // tasks scheduled below; the active set is fixed for the whole scan.
        effective_max_batch_size = RfcReadColumnStateMachine::MaxBatchSizeForColumnCount(
            (unsigned int)active.size(), EffectiveFetchSize());

        // When max_threads > 0, the user wants at most that many concurrent
        // RFC calls. Enforce by scheduling tasks in batches. Each batch is
        // one full TaskExecutor cycle: schedule, WorkOnTasks (which blocks
        // until all batch tasks finish), repeat.
        //
        // We deliberately do NOT call scheduler.SetThreads(): that mutates
        // the global TaskScheduler::requested_thread_count, which gets
        // applied at end-of-query via ClientContext::CleanupInternal ->
        // RelaunchThreads, leaking the per-query setting into the rest of
        // the session.
        idx_t batch_size = active.size();
        if (max_threads > 0 && max_threads < batch_size) {
            batch_size = max_threads;
        }
        if (batch_size == 0) {
            batch_size = 1;
        }

        for (idx_t start = 0; start < active.size(); start += batch_size) {
            TaskExecutor executor(scheduler);
            idx_t end = std::min<idx_t>(start + batch_size, active.size());
            for (idx_t i = start; i < end; i++) {
                auto &sm = *active[i];
                auto task = sm.CreateTaskForNextStep(executor, output.data[sm.GetProjectedColumnIndex()]);
                executor.ScheduleTask(std::move(task));
            }
            executor.WorkOnTasks();
        }

        if (! AreActiveStateMachineCaridnalitiesEqual()) {
            throw std::runtime_error("Cardinality of column state machines is not the same. This should not happen.");
        }

        auto cardinality = FirstActiveStateMachineCardinality();
        output.SetCardinality(cardinality);
    }

    unsigned int RfcReadTableBindData::NActiveStateMachines() 
    {
        return std::count_if(column_state_machines.begin(), column_state_machines.end(), 
                             [](auto &sm) { return sm.Active(); });
    }


    unsigned int RfcReadTableBindData::FirstActiveStateMachineCardinality() 
    {
        for (auto &sm : column_state_machines) {
            if (sm.Active()) {
                return sm.GetCardinality();
            }
        }
        throw std::runtime_error("No active state machine found. This should not happen.");
    }

    
    bool RfcReadTableBindData::AreActiveStateMachineCaridnalitiesEqual() 
    {
        if (column_state_machines.empty()) {
            return false;
        }

        auto ref_state_machine = std::find_if(column_state_machines.begin(), column_state_machines.end(), 
                                                       [](auto &sm) { return sm.Active(); });
        if (ref_state_machine == column_state_machines.end()) {
            throw std::runtime_error("No active state machine found. This should not happen.");
        }

        for (auto &sm : column_state_machines) {
            if (! sm.Active()) {
                continue;
            }
            if (sm.GetCardinality() != ref_state_machine->GetCardinality()) {
                return false;
            }
        }

        return true;
    }
    
    std::string RfcReadTableBindData::ToString() 
    {
        return StringUtil::Format("BindData(\n\ttable_name=%s, \n\toptions=%s, \n\tcolumn_names=%s, \n\tcolumn_types=%s\n)\n", 
                                    table_name, StringUtil::Join(options, options.size(), ", ", [](auto &o) { return o; }),
                                    StringUtil::Join(column_names, column_names.size(), ", ", [](auto &o) { return o; }),
                                    StringUtil::Join(column_types, column_types.size(), ", ", [](auto &o) { return o.GetName(); }));
    }

    double RfcReadTableBindData::GetProgress()
    {
        auto batch_sum = std::accumulate(column_state_machines.begin(), column_state_machines.end(), 0, 
                                         [](auto &acc, auto &sm) { return acc + sm.GetBatchCount(); });
        auto progress = (int)(batch_sum / column_state_machines.size()) % 100;
        return progress * 1.0; 
    }

    // --------------------------------------------------------------------------------------------

    static bool IsRetryableRfcError(const std::string &error_message)
    {
        // Errors from RfcInvocation::Invoke() include the RFC_RC on the first line
        if (error_message.find("RFC_COMMUNICATION_FAILURE") != std::string::npos) {
            return true;
        }

        if (error_message.find("RFC_ABAP_EXCEPTION") != std::string::npos) {
            ERPL_TRACE_WARN_DATA("sap_rfc", "ABAP exception during sap_read_table", error_message);
        }

        return false;
    }

    RfcReadColumnStateMachine::RfcReadColumnStateMachine(RfcReadTableBindData* bind_data, idx_t column_idx, unsigned int limit)
        : limit(limit), column_idx(column_idx), bind_data(bind_data)
    {
        // When an explicit MAX_ROWS limit is supplied, fit it into a single
        // batch whenever the SAP-side cap allows — that keeps ROWSKIPS=0,
        // ROWCOUNT=limit on the only call, so the server-side
        // ROWSKIPS % ROWCOUNT == 0 invariant is satisfied trivially.  For
        // limits larger than MAX_BATCH_SIZE we keep the warm-up start and
        // rely on the divisibility-aware trim in CreateFunctionArguments.
        if (limit > 0 && limit <= RfcReadColumnStateMachine::MAX_BATCH_SIZE) {
            desired_batch_size = limit;
        }
    }

    RfcReadColumnStateMachine::RfcReadColumnStateMachine(const RfcReadColumnStateMachine& other)
        : active(other.active),
          row_id_column_id(other.row_id_column_id),
          desired_batch_size(other.desired_batch_size),
          pending_records(other.pending_records),
          cardinality(other.cardinality),
          batch_count(other.batch_count),
          duck_count(other.duck_count),
          total_rows(other.total_rows),
          limit(other.limit),
          column_idx(other.column_idx),
          projected_column_idx(other.projected_column_idx),
          bind_data(other.bind_data),
          current_state(other.current_state)
    {}

    RfcReadColumnStateMachine::~RfcReadColumnStateMachine() 
    {}

    bool RfcReadColumnStateMachine::Active() 
    {
        std::lock_guard<mutex> t(thread_lock);
        return active;
    }

    void RfcReadColumnStateMachine::SetInactive() 
    {
        std::lock_guard<mutex> t(thread_lock);
        active = false;
    }

    void RfcReadColumnStateMachine::SetActive(idx_t col_idx) 
    {
        std::lock_guard<mutex> t(thread_lock);
        active = true;
        projected_column_idx = col_idx;
    }

    bool RfcReadColumnStateMachine::Finished() 
    {
        std::lock_guard<mutex> t(thread_lock);
        return current_state == ReadTableStates::FINISHED;
    }

    duckdb::unique_ptr<RfcReadColumnTask> RfcReadColumnStateMachine::CreateTaskForNextStep(duckdb::TaskExecutor &executor, duckdb::Vector &column_output)
    {
        std::lock_guard<mutex> t(thread_lock);

        auto task = duckdb::make_uniq<RfcReadColumnTask>(this, executor, column_output);

        return task;
    }

    unsigned int RfcReadColumnStateMachine::GetRfcColumnIndex() 
    {
        std::lock_guard<mutex> t(thread_lock);
        return column_idx;
    }

    unsigned int RfcReadColumnStateMachine::GetProjectedColumnIndex() 
    {
        std::lock_guard<mutex> t(thread_lock);
        return projected_column_idx;
    }

    bool RfcReadColumnStateMachine::IsRowIdColumnId()
    {
        std::lock_guard<mutex> t(thread_lock);
        return row_id_column_id;
    }

    void RfcReadColumnStateMachine::SetRowIdColumnId() 
    {
        std::lock_guard<mutex> t(thread_lock);
        row_id_column_id = true;
    }

    unsigned int RfcReadColumnStateMachine::GetCardinality() 
    {
        std::lock_guard<mutex> t(thread_lock);
        return cardinality;
    }

    unsigned int RfcReadColumnStateMachine::GetBatchCount()
    {
        std::lock_guard<mutex> t(thread_lock);
        return batch_count;
    }

    unsigned int RfcReadColumnStateMachine::GetDesiredBatchSize()
    {
        std::lock_guard<mutex> t(thread_lock);
        return desired_batch_size;
    }

    unsigned int RfcReadColumnStateMachine::NextDesiredBatchSize(unsigned int current,
                                                                 unsigned int total_rows_after,
                                                                 unsigned int max_batch_size)
    {
        if (current >= max_batch_size) {
            return current;
        }
        unsigned int next_size = current * 2u;
        if (next_size > max_batch_size) {
            next_size = max_batch_size;
        }
        return (total_rows_after % next_size == 0) ? next_size : current;
    }

    unsigned int RfcReadColumnStateMachine::MaxBatchSizeForColumnCount(unsigned int num_columns,
                                                                       unsigned int concurrent_row_budget)
    {
        // Cap the SAP SDK-side RFC_READ_TABLE result buffer (issue #69).  Each
        // result row carries a fixed-width CHAR work area inside libsapnwrfc,
        // and every projected column reads in parallel and holds its batch
        // resident across all of its LOAD steps — so the peak SDK buffer
        // scales as (columns x batch_size).  Keep that product within the
        // concurrent-row budget, floored to a power of two in
        // [STANDARD_VECTOR_SIZE, MAX_BATCH_SIZE] so the doubling warm-up still
        // reaches the cap cleanly and ROWSKIPS % ROWCOUNT stays valid.
        if (num_columns <= 1 || concurrent_row_budget == 0) {
            return MAX_BATCH_SIZE;
        }
        unsigned int per_col = concurrent_row_budget / num_columns;
        unsigned int cap = STANDARD_VECTOR_SIZE;
        while (cap * 2u <= per_col && cap * 2u <= MAX_BATCH_SIZE) {
            cap *= 2u;
        }
        return cap;
    }

    unsigned int RfcReadColumnStateMachine::TrimmedActualBatchSize(unsigned int desired,
                                                                   unsigned int total_rows,
                                                                   unsigned int limit)
    {
        if (limit == 0 || total_rows + desired <= limit) {
            return desired;
        }
        unsigned int remaining = limit - total_rows;
        if (remaining > 0 && (total_rows == 0 || total_rows % remaining == 0)) {
            return remaining;
        }
        return desired;
    }

    std::shared_ptr<RfcConnection> RfcReadColumnStateMachine::AcquireConnection()
    {
        // Caller already holds thread_lock (we only get here from ExecuteTask).
        if (!GetRfcPersistentConnections()) {
            return bind_data->OpenNewConnection();
        }
        // On first use, ask the bind-data budget whether this state machine
        // is allowed to cache a connection.  Wide-table scans (BSEG, ACDOCA)
        // have more columns than realistic SAP-side resources can serve in
        // parallel — state machines past the cap fall back to per-batch
        // open/close, which is correct (just slower) for the overflow.
        if (persistent_decision == PersistentDecision::UNDECIDED) {
            persistent_decision = bind_data->TryReservePersistentSlot()
                                      ? PersistentDecision::APPROVED
                                      : PersistentDecision::DENIED;
        }
        if (persistent_decision == PersistentDecision::DENIED) {
            return bind_data->OpenNewConnection();
        }
        auto self = std::this_thread::get_id();
        if (cached_connection && cached_connection->handle != NULL &&
            cached_connection_thread.has_value() && *cached_connection_thread == self) {
            return cached_connection;
        }
        // Stale handle, different worker thread, or no cache yet — drop any
        // existing cache (re-using a connection across threads violates the
        // SDK contract) and open a fresh one for this thread.
        InvalidateCachedConnection();
        cached_connection = bind_data->OpenNewConnection();
        cached_connection_thread = self;
        return cached_connection;
    }

    std::shared_ptr<RfcFunction> RfcReadColumnStateMachine::AcquireFunction(
        std::shared_ptr<RfcConnection> connection, const std::string &function_name)
    {
        // Caller already holds thread_lock and has just called AcquireConnection
        // — `connection` is the live handle to use for this batch.  Only state
        // machines that won the persistent-slot reservation cache the
        // RfcFunction descriptor; the others build a transient one bound to
        // the per-batch connection the caller will close.
        const bool persistent = GetRfcPersistentConnections() &&
                                persistent_decision == PersistentDecision::APPROVED;
        if (!persistent) {
            return std::make_shared<RfcFunction>(connection, function_name);
        }
        if (cached_function && cached_function_name == function_name &&
            cached_connection && cached_connection->handle != NULL) {
            return cached_function;
        }
        cached_function = std::make_shared<RfcFunction>(connection, function_name);
        cached_function_name = function_name;
        return cached_function;
    }

    void RfcReadColumnStateMachine::InvalidateCachedConnection()
    {
        // Caller already holds thread_lock.
        cached_function.reset();
        cached_function_name.clear();
        if (cached_connection) {
            try {
                cached_connection->Close();
            } catch (...) {
                // Close on a broken connection may itself fail; best-effort.
            }
            cached_connection.reset();
        }
        cached_connection_thread.reset();
    }

    std::string RfcReadColumnStateMachine::ToString()
    {
        return StringUtil::Format("ReadColumn(\n\tcolumn_idx=%d, \n\tcurrent_state=%s, \n\tdesired_batch_size=%d, \n\tpending_records=%d, \n\tcardinality=%d, \n\tbatch_count=%d, \n\tduck_count=%d\n)\n", 
                                    column_idx, 
                                    ReadTableStatesToString(current_state).c_str(), 
                                    desired_batch_size, 
                                    pending_records,
                                    cardinality, 
                                    batch_count, 
                                    duck_count);
    }

    // --------------------------------------------------------------------------------------------

    // Note: PhysicalOperator constructor has changed in DuckDB v1.4.0
    // This dummy operator is no longer needed or should be handled differently
    // static const duckdb::PhysicalOperator DUMMY_OPERATOR(duckdb::PhysicalOperatorType::INVALID, vector<LogicalType>(), 0, 0);

    RfcReadColumnTask::RfcReadColumnTask(RfcReadColumnStateMachine *owning_state_machine, duckdb::TaskExecutor &executor, duckdb::Vector &current_column_output)
        :  duckdb::BaseExecutorTask(executor), owning_state_machine(owning_state_machine), current_column_output(current_column_output)
    { }

    void RfcReadColumnTask::ExecuteTask() 
    {
        std::lock_guard<mutex> t(owning_state_machine->thread_lock);

        auto &current_state = owning_state_machine->current_state;
        auto &desired_batch_size = owning_state_machine->desired_batch_size;
        auto &pending_records = owning_state_machine->pending_records;
        auto &cardinality = owning_state_machine->cardinality;
        auto &batch_count = owning_state_machine->batch_count;
        auto &duck_count = owning_state_machine->duck_count;
        auto &limit = owning_state_machine->limit;
        auto &total_rows = owning_state_machine->total_rows;

        cardinality = 0;
        bool return_control_to_duck = false;
        while (return_control_to_duck == false) {
            switch(current_state) 
            {
                case ReadTableStates::INIT: {
                    current_state = ReadTableStates::EXTRACT_FROM_SAP;
                    break;
                }
                case ReadTableStates::EXTRACT_FROM_SAP: {
                    auto extracted_from_sap = ExecuteNextTableReadForColumn();
                    batch_count += 1;
                    duck_count = 0;
                    pending_records += extracted_from_sap;

                    current_state = (extracted_from_sap < desired_batch_size) || (limit > 0 && (total_rows + extracted_from_sap) == limit)
                                        ? ReadTableStates::FINAL_LOAD_TO_DUCKDB
                                        : ReadTableStates::LOAD_TO_DUCKDB;

                    // Divisibility-preserving warm-up (issue #63).  See
                    // RfcReadColumnStateMachine::NextDesiredBatchSize for the rule.
                    if (limit == 0) {
                        desired_batch_size = RfcReadColumnStateMachine::NextDesiredBatchSize(
                            desired_batch_size, total_rows + extracted_from_sap,
                            owning_state_machine->bind_data->GetEffectiveMaxBatchSize());
                    }
                    break;
                }
                case ReadTableStates::LOAD_TO_DUCKDB: {
                    cardinality = LoadNextBatchToDuckDBColumn();
                    duck_count += 1;
                    pending_records -= cardinality;
                    total_rows += cardinality;
                    
                    current_state = (pending_records > 0) 
                                        ? ReadTableStates::LOAD_TO_DUCKDB 
                                        : ReadTableStates::EXTRACT_FROM_SAP;
                    
                    return_control_to_duck = true;
                    break;
                }
                case ReadTableStates::FINAL_LOAD_TO_DUCKDB: {
                    cardinality = LoadNextBatchToDuckDBColumn();
                    duck_count += 1;
                    pending_records -= cardinality;
                    total_rows += cardinality;

                    current_state = (pending_records > 0)
                                        ? ReadTableStates::FINAL_LOAD_TO_DUCKDB
                                        : ReadTableStates::FINISHED;

                    if (current_state == ReadTableStates::FINISHED) {
                        // No more batches will run on this state machine — release
                        // the cached RFC connection so we don't hold a SAP work
                        // process reservation until query teardown.
                        owning_state_machine->InvalidateCachedConnection();
                        // Drop the last batch's SDK function handle now so its
                        // (potentially large) result-table buffer is freed at
                        // end-of-column instead of at query teardown (#69).
                        owning_state_machine->current_invocation.reset();
                        owning_state_machine->current_table_handle = nullptr;
                        owning_state_machine->current_batch_rows = 0;
                    }

                    return_control_to_duck = true;
                    break;
                }
                case ReadTableStates::FINISHED: {
                    // Never actually reached / called, 
                    // as the Finished() method will return true;
                    break;
                }
            }
        }
        
        //printf("[%lu] %s\n", owning_state_machine->column_idx, owning_state_machine->ToString().c_str());
    }

    
    unsigned int RfcReadColumnTask::ExecuteNextTableReadForColumn()
    {
        auto bind_data = owning_state_machine->bind_data;
        auto rfc_type = bind_data->GetColumnType(owning_state_machine->column_idx);
        auto data_path = bind_data->read_table_result_path.empty() ? std::string("/DATA") : bind_data->read_table_result_path;
        bool use_et_data = false;

        int attempt = 0;
        int max_attempts = 5;
        int initial_delay = 10000; // milliseconds
        while (attempt < max_attempts) {
            std::shared_ptr<RfcConnection> connection;
            // Whether this batch's connection is owned by the state machine
            // (don't close on success) vs. per-batch (close on success).
            // Decided after AcquireConnection() so we observe the live
            // persistent_decision — the bind-data slot reservation only
            // resolves on first AcquireConnection() call.
            bool persistent_for_this_batch = false;
            try {
                connection = owning_state_machine->AcquireConnection();
                persistent_for_this_batch =
                    GetRfcPersistentConnections() &&
                    owning_state_machine->HasApprovedPersistentSlot();

                auto read_table_function = bind_data->GetReadTableFunctionName();
                auto read_table_delimiter = bind_data->GetReadTableDelimiter();
                if (read_table_function == "RFC_READ_TABLE" && rfc_type.IsStringType()) {
                    if (!bind_data->read_table_supports_et_data_switch.has_value()) {
                        bind_data->read_table_supports_et_data_switch = bind_data->ReadTableSupportsEtDataSwitch(connection, read_table_function);
                    }
                    if (!bind_data->read_table_supports_et_data_switch.value()) {
                        auto col_name = bind_data->GetRfcColumnNames()[owning_state_machine->column_idx];
                        throw std::runtime_error(StringUtil::Format(
                            "Cannot read string/xstring column '%s' from table '%s'. "
                            "RFC_READ_TABLE does not support USE_ET_DATA_4_RETURN/ET_DATA on this system.",
                            col_name, bind_data->table_name));
                    }
                    use_et_data = true;
                    data_path = "/ET_DATA";
                    if (read_table_delimiter.empty()) {
                        read_table_delimiter = "~";
                    }
                }

                auto func = owning_state_machine->AcquireFunction(connection, read_table_function);
                auto func_args = CreateFunctionArguments(read_table_delimiter, use_et_data);
                auto invocation = func->BeginInvocation(func_args);
                // Execute the RFC call but do NOT materialise the result as a
                // duckdb::Value tree (issue #69 — that layer drove ~95% of all
                // heap allocations).  Resolve the SDK result-table handle
                // instead and stream rows straight into the output Vector
                // during LoadNextBatchToDuckDBColumn.
                invocation->Execute();
                auto extracted = ResolveResultTable(invocation, data_path);

                if (!persistent_for_this_batch) {
                    // Per-batch open/close — either the user disabled the
                    // cache, or this state machine didn't win a persistent
                    // slot from the bind-data budget (wide-table overflow).
                    // Safe to close now: the response data lives in the
                    // function handle (kept alive via current_invocation),
                    // not the connection.
                    connection->Close();
                }
                return extracted;
            }
            catch (std::exception &e) {
                // Drop the cached connection / function on any error so the
                // next attempt opens a clean one.  This also handles the
                // RfcGetFunctionDesc failure case where the cached descriptor
                // could be stale on the next batch.
                owning_state_machine->InvalidateCachedConnection();
                if (!persistent_for_this_batch && connection) {
                    try { connection->Close(); } catch (...) {}
                }
                std::string err_msg(e.what());
                if (!IsRetryableRfcError(err_msg)) {
                    if (rfc_type.IsStringType() && err_msg.find("TABLE_WITHOUT_DATA") != std::string::npos) {
                        auto fallback_connection = bind_data->OpenNewConnection();
                        auto fallback_selected = bind_data->TrySelectFallbackReadTableFunction(fallback_connection);
                        fallback_connection->Close();
                        if (fallback_selected) {
                            // retry immediately with the fallback function
                            continue;
                        }
                    }
                    throw;
                }

                int delay = initial_delay * std::pow(2, attempt);
                ERPL_TRACE_WARN_DATA("sap_rfc", StringUtil::Format("Warning during fetching next batch. Attempt: %d, Delay: %ds", attempt + 1, (int)(delay / 1000.)), err_msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                attempt++;
            }
        }

        throw std::runtime_error(StringUtil::Format("Could not complete read task after %d attempts.", max_attempts));
    }

    std::vector<Value> RfcReadColumnTask::CreateFunctionArguments(const std::string &delimiter, bool use_et_data) 
    {
        auto bind_data = owning_state_machine->bind_data;
        auto table_name = bind_data->table_name;
        auto options = bind_data->GetOptions();
        auto fields = bind_data->GetRfcColumnName(owning_state_machine->column_idx);

        auto &desired_batch_size = owning_state_machine->desired_batch_size;
        auto &total_rows = owning_state_machine->total_rows;
        auto &limit = owning_state_machine->limit;

        // Divisibility-aware trim — see RfcReadColumnStateMachine::TrimmedActualBatchSize.
        // When MAX_ROWS would force a final batch with an illegal ROWCOUNT,
        // we over-fetch and let LoadNextBatchToDuckDBColumn() clip the
        // surplus client-side.
        auto actual_batch_size = RfcReadColumnStateMachine::TrimmedActualBatchSize(
            desired_batch_size, total_rows, limit);

        if (!bind_data->options.empty()) {
            auto options_str = StringUtil::Join(bind_data->options, bind_data->options.size(), " | ", [](auto &o) { return o; });
            ERPL_TRACE_DEBUG_DATA("sap_rfc", StringUtil::Format("sap_read_table('%s') options", table_name.c_str()), options_str);
        }

        auto args = ArgBuilder();
        if (bind_data->ReadTableHasParam("QUERY_TABLE")) {
            args.Add("QUERY_TABLE", Value(table_name));
        }
        if (bind_data->ReadTableHasParam("ROWSKIPS")) {
            // RFC_READ_TABLE requires ROWSKIPS % ROWCOUNT == 0 server-side.
            // We satisfy that by only doubling desired_batch_size when
            // total_rows is divisible by the new size, so total_rows is
            // always a valid (multiple-of-current-ROWCOUNT) offset.
            args.Add("ROWSKIPS", Value::CreateValue<int32_t>(total_rows));
        }
        if (bind_data->ReadTableHasParam("ROWCOUNT")) {
            args.Add("ROWCOUNT", Value::CreateValue<int32_t>(actual_batch_size));
        }
        if (bind_data->ReadTableHasParam("GET_SORTED")) {
            args.Add("GET_SORTED", Value("X"));
        }
        if (bind_data->ReadTableHasParam("FIELDS")) {
            args.Add("FIELDS", fields);
        }

        if (!delimiter.empty() && bind_data->ReadTableHasParam("DELIMITER")) {
            args.Add("DELIMITER", Value(delimiter));
        }
        if (use_et_data && bind_data->ReadTableHasParam("USE_ET_DATA_4_RETURN")) {
            args.Add("USE_ET_DATA_4_RETURN", Value("X"));
        }

        if (! options.empty() && bind_data->ReadTableHasParam("OPTIONS")) {
            args.Add("OPTIONS", options);
        }

        return args.BuildArgList();
    }

    unsigned int RfcReadColumnTask::ResolveResultTable(std::shared_ptr<RfcInvocation> invocation, std::string data_path)
    {
        auto sm = owning_state_machine;
        // Keep the invocation alive: it owns the SDK function handle, and the
        // result-table handle resolved below points into that handle's
        // memory.  It must outlive every LoadNextBatchToDuckDBColumn call for
        // this batch.
        sm->current_invocation = invocation;
        sm->current_table_handle = nullptr;
        sm->current_batch_rows = 0;

        auto func = invocation->GetFunction();
        auto fh = invocation->GetFunctionHandle();
        RFC_ERROR_INFO error_info;

        auto strip_slash = [](const std::string &path) -> std::string {
            auto pos = path.find_first_not_of('/');
            return (pos == std::string::npos) ? std::string() : path.substr(pos);
        };
        auto resolve = [&](const std::string &path, RFC_TABLE_HANDLE &out_tbl, unsigned int &out_rows) -> bool {
            auto param = strip_slash(path);
            if (param.empty()) {
                return false;
            }
            RFC_TABLE_HANDLE tbl = nullptr;
            auto rc = RfcGetTable(fh, std2uc(param).get(), &tbl, &error_info);
            if (rc != RFC_OK || tbl == nullptr) {
                return false;
            }
            unsigned int rows = 0;
            rc = RfcGetRowCount(tbl, &rows, &error_info);
            if (rc != RFC_OK) {
                return false;
            }
            out_tbl = tbl;
            out_rows = rows;
            return true;
        };

        std::string chosen_path = data_path;
        RFC_TABLE_HANDLE tbl = nullptr;
        unsigned int rows = 0;
        if (!resolve(data_path, tbl, rows)) {
            return 0;
        }

        // /SAPDS/RFC_READ_TABLE2 and /BODS/RFC_READ_TABLE2 distribute rows
        // across multiple TBLOUTxxxx output tables based on the projected row
        // width — the SMALLEST TBLOUTxxxx that fits gets populated, the others
        // stay empty.  ResolveReadTableResultPath picks one path at bind time,
        // but the right size depends on this batch's columns.  If our chosen
        // path is empty, probe the other TBLOUTxxxx on the SAME function
        // handle (no extra round-trip) and use whichever has rows.
        if (rows == 0 && data_path.rfind("/TBLOUT", 0) == 0) {
            static const std::vector<std::string> alt_paths = {
                "/TBLOUT128", "/TBLOUT512", "/TBLOUT2048", "/TBLOUT8192", "/TBLOUT30000"
            };
            for (auto &alt : alt_paths) {
                if (alt == data_path) {
                    continue;
                }
                RFC_TABLE_HANDLE alt_tbl = nullptr;
                unsigned int alt_rows = 0;
                if (resolve(alt, alt_tbl, alt_rows) && alt_rows > 0) {
                    chosen_path = alt;
                    tbl = alt_tbl;
                    rows = alt_rows;
                    break;
                }
            }
        }

        // The result row carries the requested column's CSV payload in a
        // single field (e.g. "WA").  Resolve its name + SDK type once from the
        // chosen table param's line structure so the per-row read in
        // LoadNextBatchToDuckDBColumn reuses the exact same conversion the
        // old RfcResultSet path applied.
        auto result_info = func->GetResultInfo(strip_slash(chosen_path));
        auto field_infos = result_info.GetRfcType()->GetFieldInfos();
        if (field_infos.empty()) {
            return 0;
        }
        sm->wa_field_name = field_infos[0].GetName();
        sm->wa_field_type = field_infos[0].GetRfcType();
        sm->current_table_handle = tbl;
        sm->current_batch_rows = rows;
        return rows;
    }

    unsigned int RfcReadColumnTask::LoadNextBatchToDuckDBColumn()
    {
        auto sm = owning_state_machine;
        auto &duck_count = sm->duck_count;
        auto &total_rows = sm->total_rows;
        auto &limit = sm->limit;

        auto table_handle = sm->current_table_handle;
        auto batch_rows = sm->current_batch_rows;
        if (batch_rows == 0 || table_handle == nullptr) {
            return 0;
        }

        idx_t batch_start = (idx_t)duck_count * STANDARD_VECTOR_SIZE;
        if (batch_start >= batch_rows) {
            return 0;
        }
        idx_t batch_end = std::min<idx_t>(batch_start + STANDARD_VECTOR_SIZE, batch_rows);
        if (limit > 0 && total_rows + (batch_end - batch_start) > limit) {
            batch_end = batch_start + (limit - total_rows);
        }

        // Hoist per-cell lookups out of the loop.  rfc_type is the column's
        // real DDIC type (drives DATE/TIME/BCD parsing in ConvertCsvValue);
        // wa_type/wa_name name the SDK result field carrying the CSV payload.
        const auto rfc_type = sm->bind_data->GetColumnType(sm->column_idx);
        auto wa_type = sm->wa_field_type;
        const auto wa_name = sm->wa_field_name;
        const bool is_row_id = sm->row_id_column_id;

        RFC_ERROR_INFO error_info;
        idx_t row_idx = 0;
        for (idx_t i = batch_start; i < batch_end; ++i, ++row_idx) {
            if (is_row_id) {
                // Synthetic rowid column — no SAP payload to read.
                current_column_output.SetValue(row_idx, Value::BIGINT(42));
                continue;
            }
            auto rc = RfcMoveTo(table_handle, (unsigned int)i, &error_info);
            if (rc != RFC_OK) {
                throw std::runtime_error(StringUtil::Format("Failed to move to row %d: %s: %s",
                                                            (int)i, rfcrc2std(error_info.code), uc2std(error_info.message)));
            }
            auto row_handle = RfcGetCurrentRow(table_handle, &error_info);
            // Stream straight from the SDK handle: read the CSV field, parse
            // it to the column's type, write into the output Vector.  No
            // whole-batch duckdb::Value materialisation (issue #69).
            auto wa_value = wa_type->ConvertRfcValueFromContainer(row_handle, wa_name);
            current_column_output.SetValue(row_idx, ParseCsvValue(rfc_type, wa_value));
        }
        return row_idx;
    }

    Value RfcReadColumnTask::ParseCsvValue(const RfcType &rfc_type, const Value &orig) const
    {
        if (owning_state_machine->row_id_column_id) {
            return Value::BIGINT(42);
        }
        return rfc_type.ConvertCsvValue(orig);
    }
    
    // --------------------------------------------------------------------------------------------
    std::string ReadTableStatesToString(ReadTableStates &state) 
    {
        switch (state) {
            case ReadTableStates::INIT: return "INIT";
            case ReadTableStates::EXTRACT_FROM_SAP: return "EXTRACT_FROM_SAP";
            case ReadTableStates::LOAD_TO_DUCKDB: return "LOAD_TO_DUCKDB";
            case ReadTableStates::FINAL_LOAD_TO_DUCKDB: return "FINAL_LOAD_TO_DUCKDB";
            case ReadTableStates::FINISHED: return "FINISHED";
            default: return "UNKNOWN";
        }
    }

} // namespace duckdb
