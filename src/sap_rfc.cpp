#include "duckdb.hpp"
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

    std::vector<std::string> RfcReadTableBindData::GetColumnNames()
    {
        return column_names;
    }

    duckdb::vector<Value> RfcReadTableBindData::GetColumnName(unsigned int column_idx) 
    {
        if (column_idx >= column_names.size()) {
            throw std::runtime_error(StringUtil::Format("Column index %d out of bounds", column_idx));
        }
        auto arg_builder = ArgBuilder().Add("FIELDNAME", Value(column_names[column_idx]));
        auto ret = duckdb::vector<Value>();
        ret.push_back(arg_builder.Build());
        return ret;
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
        return std::all_of(column_state_machines.begin(), column_state_machines.end(), 
                           [](auto &s) { return s.Finished(); });
    }

    void RfcReadTableBindData::Step(ClientContext &context, DataChunk &output) 
    {
        auto &scheduler = TaskScheduler::GetScheduler(context);
        if (max_threads > 0) {
            scheduler.SetThreads(max_threads);
        }
        auto producer_token = scheduler.CreateProducer();
        
        for (auto &sm : column_state_machines) {
            auto task = sm.CreateTaskForNextStep(context, output.data[sm.GetColumnIndex()]);
            scheduler.ScheduleTask(*producer_token, task);
        }

        scheduler.ExecuteTasks(column_state_machines.size());
        
        if (column_state_machines.size() > 1 ) {
            bool same_cardinality = std::all_of(column_state_machines.begin() + 1, column_state_machines.end(), 
                            [&] (auto &sm) {return sm.GetCardinality() == column_state_machines[0].GetCardinality();});

            if (!same_cardinality) {
                throw std::runtime_error("Cardinality of column state machines is not the same. This should not happen.");
            }
        }
        
        output.SetCardinality(column_state_machines[0].GetCardinality());
    }

    std::string RfcReadTableBindData::ToString() 
    {
        return StringUtil::Format("BindData(\n\ttable_name=%s, \n\toptions=%s, \n\tcolumn_names=%s, \n\tcolumn_types=%s\n)\n", 
                                    table_name, StringUtil::Join(options, options.size(), ", ", [](auto &o) { return o; }),
                                    StringUtil::Join(column_names, column_names.size(), ", ", [](auto &o) { return o; }),
                                    StringUtil::Join(column_types, column_types.size(), ", ", [](auto &o) { return o.GetName(); }));
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
        : desired_batch_size(other.desired_batch_size),
          pending_records(other.pending_records),
          cardinality(other.cardinality),
          batch_count(other.batch_count),
          duck_count(other.duck_count),
          total_rows(other.total_rows),
          limit(other.limit),
          column_idx(other.column_idx),
          bind_data(other.bind_data),
          current_state(other.current_state)
    {}

    RfcReadColumnStateMachine::~RfcReadColumnStateMachine() 
    {}

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

    unsigned int RfcReadColumnStateMachine::GetColumnIndex() 
    {
        std::lock_guard<mutex> t(thread_lock);
        return column_idx;
    }

    unsigned int RfcReadColumnStateMachine::GetCardinality() 
    {
        std::lock_guard<mutex> t(thread_lock);
        return cardinality;
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
        auto bind_data = owning_state_machine->bind_data;
        auto &current_result_data = owning_state_machine->current_result_data;
        auto connection = bind_data->OpenNewConnection();
        auto func = std::make_shared<RfcFunction>(connection, "RFC_READ_TABLE");
        auto func_args = CreateFunctionArguments();
        auto invocation = func->BeginInvocation(func_args);
        current_result_data = invocation->Invoke("/DATA");

        return current_result_data->TotalRows();
    }

    std::vector<Value> RfcReadColumnTask::CreateFunctionArguments() 
    {
        auto bind_data = owning_state_machine->bind_data;
        auto table_name = bind_data->table_name;
        auto options = bind_data->GetOptions();
        auto fields = bind_data->GetColumnName(owning_state_machine->column_idx);

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
            current_column_output.SetValue(row_idx, ParseCsvValue(batch[row_idx]));
        }
        return batch.size();
    }

    Value RfcReadColumnTask::ParseCsvValue(Value &orig)
    {
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
        }
    }

} // namespace duckdb