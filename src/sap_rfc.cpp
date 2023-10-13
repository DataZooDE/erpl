#include "duckdb.hpp"
#include <cmath>
#include <iostream>
#include <numeric>
#include <thread>  

#include <stdint.h>
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/storage/table/row_group.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"

#include "sap_rfc.hpp"
#include "sap_function.hpp"
#include "duckdb_argument_helper.hpp"

namespace duckdb
{
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
                                               RfcConnectionFactory_t connection_factory, 
                                               ClientContext &client_context)
        : table_name(table_name), limit(limit), max_threads(max_read_threads), connection_factory(connection_factory), client_context(client_context)
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
        return connection_factory(client_context);
    }

    void RfcReadTableBindData::InitOptionsFromWhereClause(std::string &where_clause)
    {
        options.clear();
        AddOptionsFromWhereClause(where_clause);
    }

    void RfcReadTableBindData::AddOptionsFromWhereClause(std::string &where_clause)
    {
        
        auto opt_start = where_clause.begin();
        
        while (opt_start != where_clause.end()) {
            auto opt_end = opt_start;
            std::advance(opt_end, MAX_OPTION_LEN);

            if (opt_end < where_clause.end()) {
                // Find the closest previous whitespace
                while (opt_end != opt_start && ! std::isspace(*opt_end)) {
                    --opt_end;
                }

                // If we couldn't find a whitespace, we just split at the 70 character mark
                if (opt_end == opt_start) {
                    throw std::runtime_error("Could not split WHERE clause into options, "
                                             "the maximal lenght of a single part of the "
                                             "clause is 70 characters.");
                }
            } else {
                opt_end = where_clause.end();
            }

            options.push_back(std::string(opt_start, opt_end));
            opt_start = opt_end;
        }
    }

    void RfcReadTableBindData::InitAndVerifyFields(std::vector<std::string> req_fields)
    {
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
        for (auto &req_field : req_fields) {
            auto fm = req_field_metas[req_field];
            auto rfc_type = GetRfcTypeForFieldMeta(fm);
            column_types.push_back(rfc_type);
        }

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

    void RfcReadTableBindData::AddOptionsFromFilters(duckdb::optional_ptr<duckdb::TableFilterSet> filter_set) 
    {
        if (filter_set == nullptr || filter_set->filters.empty()) {
            return;
        }

        vector<std::string> filter_entries;
        for (auto &[projected_column_idx, filter] : filter_set->filters) 
        {
            auto column_name = GetProjectedColumnName(projected_column_idx);
            filter_entries.push_back(TransformFilter(column_name, *filter));
        }

        auto filter_string = StringUtil::Join(filter_entries, " AND ");

        if (! options.empty()) {
            filter_string = " AND " + filter_string;
        }   

        AddOptionsFromWhereClause(filter_string);
    }

    std::string RfcReadTableBindData::TransformFilter(std::string &column_name, TableFilter &filter) 
    {
        switch(filter.filter_type)
        {
            case TableFilterType::CONJUNCTION_AND: {
                auto &and_filter = filter.Cast<ConjunctionAndFilter>();
                return CreateExpression(column_name, and_filter.child_filters, " AND ");
            }
            case TableFilterType::CONJUNCTION_OR: {
                auto &or_filter = filter.Cast<ConjunctionAndFilter>();
                return CreateExpression(column_name, or_filter.child_filters, " OR ");
            }
            case TableFilterType::CONSTANT_COMPARISON: {
                auto &const_filter = filter.Cast<ConstantFilter>();

                auto constant_string = StringUtil::Format("'%s'", const_filter.constant.ToString());
		        auto operator_string = TransformComparision(const_filter.comparison_type);
		        return StringUtil::Format("%s %s %s", column_name, operator_string, constant_string);
            }
            case TableFilterType::IS_NOT_NULL: {
                return StringUtil::Format("%s IS NOT NULL", column_name);
            }
            case TableFilterType::IS_NULL: {
                return StringUtil::Format("%s IS NULL", column_name);
            }
            default: {
                throw InternalException("Unsupported table filter type");
            }
        }
    }

    std::string RfcReadTableBindData::CreateExpression(string &column_name, vector<unique_ptr<TableFilter>> &filters, string op) 
    {
        auto filter_strings = std::vector<std::string>();
        for (auto &filter : filters) {
            filter_strings.push_back(RfcReadTableBindData::TransformFilter(column_name, *filter));
        }

        return StringUtil::Join(filter_strings, filter_strings.size(), op, [](auto &s) { return s; });
    }

    std::string RfcReadTableBindData::TransformComparision(ExpressionType type) {
        switch (type) {
            case ExpressionType::COMPARE_EQUAL:
                return "=";
            case ExpressionType::COMPARE_NOTEQUAL:
                return "!=";
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
        auto func = make_shared<RfcFunction>(connection, "DDIF_FIELDINFO_GET");
        auto func_args = args.BuildArgList();
        auto invocation = func->BeginInvocation(func_args);
        auto result_set = invocation->Invoke();

        auto field_metas = ListValue::GetChildren(result_set->GetResultValue("/DFIES_TAB"));
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
        if (max_threads > 0) {
            scheduler.SetThreads(max_threads);
        }
        //scheduler.SetThreads(5);
        auto producer_token = scheduler.CreateProducer();
        
        for (auto &sm : column_state_machines) {
            if (! sm.Active()) {
                continue;
            }
            auto task = sm.CreateTaskForNextStep(context, output.data[sm.GetProjectedColumnIndex()]);
            scheduler.ScheduleTask(*producer_token, task);
        }

        auto n_active = NActiveStateMachines();
        scheduler.ExecuteTasks(n_active);
        
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

    RfcReadColumnStateMachine::RfcReadColumnStateMachine(RfcReadTableBindData* bind_data, idx_t column_idx, unsigned int limit)
        : limit(limit), column_idx(column_idx), bind_data(bind_data)
    {
        if (limit > 0 && limit < desired_batch_size) {
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

    std::shared_ptr<RfcReadColumnTask> RfcReadColumnStateMachine::CreateTaskForNextStep(ClientContext &client_context, duckdb::Vector &column_output) 
    {
        std::lock_guard<mutex> t(thread_lock);
        auto task = std::make_shared<RfcReadColumnTask>(this, client_context, column_output);
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

    std::shared_ptr<RfcConnection> RfcReadColumnStateMachine::GetConnection() 
    {
        if (current_connection == nullptr) {
            current_connection = bind_data->OpenNewConnection();
        }
        return current_connection;
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

    RfcReadColumnTask::RfcReadColumnTask(RfcReadColumnStateMachine *owning_state_machine, ClientContext &client_context, duckdb::Vector &current_column_output)
        :  ExecutorTask(client_context), owning_state_machine(owning_state_machine), current_column_output(current_column_output)
    { }

    TaskExecutionResult RfcReadColumnTask::ExecuteTask(TaskExecutionMode mode) 
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
        return TaskExecutionResult::TASK_FINISHED;   
    }

    
    unsigned int RfcReadColumnTask::ExecuteNextTableReadForColumn() 
    {
        int attempt = 0;
        int max_attempts = 5;
        int initial_delay = 10000; // milliseconds
        while (attempt < max_attempts) {
            try {
                auto bind_data = owning_state_machine->bind_data;
                auto &current_result_data = owning_state_machine->current_result_data;
                //auto connection = owning_state_machine->GetConnection();
                auto connection = bind_data->OpenNewConnection();
                auto func = std::make_shared<RfcFunction>(connection, "RFC_READ_TABLE");
                auto func_args = CreateFunctionArguments();
                auto invocation = func->BeginInvocation(func_args);
                current_result_data = invocation->Invoke("/DATA");
                return current_result_data->TotalRows();
            }
            catch (std::exception &e) {
                int delay = initial_delay * std::pow(2, attempt);
                std::cerr << StringUtil::Format("Exception: %s. Attempt: %d, Delay: %ds. Retrying...\n", e.what(), attempt + 1, (int)(delay / 1000.));
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                attempt++;
            }
        }

        throw std::runtime_error(StringUtil::Format("Could not complete read task after %d attempts.", max_attempts));
    }

    std::vector<Value> RfcReadColumnTask::CreateFunctionArguments() 
    {
        auto bind_data = owning_state_machine->bind_data;
        auto table_name = bind_data->table_name;
        auto options = bind_data->GetOptions();
        auto fields = bind_data->GetRfcColumnName(owning_state_machine->column_idx);

        auto &desired_batch_size = owning_state_machine->desired_batch_size;
        auto &batch_count = owning_state_machine->batch_count;
        auto &total_rows = owning_state_machine->total_rows;
        auto &limit = owning_state_machine->limit;

        auto actual_batch_size = limit > 0 && total_rows + desired_batch_size > limit 
                                    ? limit - total_rows 
                                    : desired_batch_size;

        auto args = ArgBuilder()
            .Add("QUERY_TABLE", Value(table_name))
            .Add("ROWSKIPS", Value::CreateValue<int32_t>(batch_count * desired_batch_size))
            .Add("ROWCOUNT", Value::CreateValue<int32_t>(actual_batch_size))
            .Add("FIELDS", fields);

        if (! options.empty()) {
            args.Add("OPTIONS", options);
        }

        return args.BuildArgList();
    }

    unsigned int RfcReadColumnTask::LoadNextBatchToDuckDBColumn() 
    {
        auto &current_result_data = owning_state_machine->current_result_data;
        auto &duck_count = owning_state_machine->duck_count;
        auto &total_rows = owning_state_machine->total_rows;
        auto &limit = owning_state_machine->limit;

        if (current_result_data->TotalRows() == 0) {
            return 0;
        }

        auto result_vec = ListValue::GetChildren(current_result_data->GetResultValue(0));
        auto batch_start = result_vec.begin() + duck_count * STANDARD_VECTOR_SIZE;
        auto batch_end = batch_start + STANDARD_VECTOR_SIZE > result_vec.end() 
                            ? result_vec.end() 
                            : batch_start + STANDARD_VECTOR_SIZE;
        if (limit > 0 && total_rows + batch_end - batch_start > limit) {
            batch_end = batch_start + (limit - total_rows);
        }

        auto batch = duckdb::vector<Value>(batch_start, batch_end);
        
        for (idx_t row_idx = 0; row_idx < batch.size(); row_idx++) {
            auto val = ParseCsvValue(batch[row_idx]);
            current_column_output.SetValue(row_idx, val);
        }
        return batch.size();
    }

    Value RfcReadColumnTask::ParseCsvValue(Value &orig)
    {
        if (owning_state_machine->row_id_column_id) {
            return Value::BIGINT(42);
        }

        auto bind_data = owning_state_machine->bind_data;
        auto rfc_type = bind_data->GetColumnType(owning_state_machine->column_idx);
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