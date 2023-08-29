#include "duckdb.hpp"
#include <numeric>

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

        // auto pragma_query = StringUtil::Format("SELECT '%s' as msg", func_name);
        auto pragma_query = ss.str();
        return pragma_query;
    }

    // --------------------------------------------------------------------------------------------

    RfcReadTableBindData::RfcReadTableBindData(std::string table_name)
        : table_name(table_name)
    { }


    std::vector<std::string> RfcReadTableBindData::GetColumnNames()
    {
        return column_names;
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
            ret.push_back(Value(o));
        }

        return ret;
    }

    duckdb::vector<Value> RfcReadTableBindData::GetFields()
    {
        auto ret = duckdb::vector<Value>();
        for (auto &f : fields) {
            ret.push_back(Value(f));
        }

        return ret;
    }

    duckdb::vector<Value> RfcReadTableBindData::GetField(unsigned int column_idx) 
    {
        auto ret = duckdb::vector<Value>();
        ret.push_back(Value(fields[column_idx]));
        return ret;
    }

    void RfcReadTableBindData::SetOptionsFromWhereClause(std::string &where_clause)
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

    void RfcReadTableBindData::SetAndVerifyFields(std::shared_ptr<RfcConnection> connection, std::vector<std::string> &fields)
    {
        auto req_fields = std::vector<std::string>(fields);
        auto req_field_metas = std::map<std::string, Value>();

        for (auto &fm :  GetTableFieldMetas(connection, table_name)) {
            auto fm_helper = ValueHelper(fm);
            auto field_name = fm_helper["FIELDNAME"].ToString();
            auto req_field_it = std::find(req_fields.begin(), req_fields.end(), field_name);
            if (req_field_it == req_fields.end()) {
                continue;
            }

            req_field_metas[field_name] = fm;
            req_fields.erase(req_field_it);
        }

        if (! req_fields.empty()) {
            auto fields_str = StringUtil::Join(req_fields, req_fields.size(), ", ", [](auto &f) { return f; });
            throw std::runtime_error(StringUtil::Format("Could not find the following fields in table %s: %s", table_name, fields_str));
        }
        
        auto extract_name_and_type = [](auto &v, auto &fm) {
            auto field_name = fm.first;
            auto rfc_type = GetRfcTypeForFieldMeta(fm.second);
            auto tpl = std::make_tuple(field_name, rfc_type);
            v.push_back(tpl);
            return v;
        };
 
        auto x = std::accumulate(req_field_metas.begin(), req_field_metas.end(), 
                                 std::vector<std::tuple<std::string, RfcType>>(), 
                                 extract_name_and_type);

        column_names.clear();
        std::transform(x.begin(), x.end(), std::back_inserter(column_names), [](auto &t) { return std::get<0>(t); });

        column_types.clear();
        std::transform(x.begin(), x.end(), std::back_inserter(column_types), [](auto &t) { return std::get<1>(t); });
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

    std::unique_ptr<RfcReadTableGlobalState> RfcReadTableBindData::InitializeGlobalState(ClientContext &context, idx_t max_threads)
    {
        idx_t actual_threads = std::min(max_threads, column_names.size());
        return make_uniq<RfcReadTableGlobalState>(actual_threads);
    }

    std::unique_ptr<RfcReadTableGlobalState> RfcReadTableBindData::InitializeGlobalState(ClientContext &context)
    {
        idx_t max_threads = UINT_MAX;
        return InitializeGlobalState(context, max_threads);
    }

    // --------------------------------------------------------------------------------------------

    bool RfcReadTableGlobalState::AllFinished() 
    {
        if (finished_threads.empty()) {
            return true;
        }
            
        return std::all_of(finished_threads.begin(), finished_threads.end(), [](bool x) { return x; });
    }

    std::unique_ptr<RfcReadTableLocalState> RfcReadTableGlobalState::InitializeLocalState() 
    {
        auto ret = make_uniq<RfcReadTableLocalState>();
        lock.lock();
        ret->column_idx = finished_threads.size();
        finished_threads.push_back(false);
        lock.unlock();

        return ret;
    }

    // --------------------------------------------------------------------------------------------
    bool RfcReadTableLocalState::Finished() 
    {
        return current_state == ReadTableStates::FINISHED;
    }

    void RfcReadTableLocalState::Step(RfcReadTableBindData &bind_data, std::shared_ptr<RfcConnection> connection, DataChunk &output) 
    {
        switch(current_state) 
        {
            case ReadTableStates::INIT: {
                current_state = ReadTableStates::EXTRACT_FROM_SAP;
                break;
            }
            case ReadTableStates::EXTRACT_FROM_SAP: {
                auto extracted_from_sap = ExecuteNextTableReadForColumn(bind_data, connection);
                batch_count += 1;
                duck_count = 0;
                pending_records += extracted_from_sap;

                current_state = (extracted_from_sap < desired_batch_size) 
                                    ? ReadTableStates::FINAL_LOAD_TO_DUCKDB 
                                    : ReadTableStates::LOAD_TO_DUCKDB;

                current_state = ReadTableStates::LOAD_TO_DUCKDB;
                break;
            }
            case ReadTableStates::LOAD_TO_DUCKDB: {
                auto loaded_to_duckdb = LoadNextBatchToDuckDBColumn(bind_data, output);
                duck_count += 1;
                pending_records -= loaded_to_duckdb;
                
                current_state = (pending_records > 0) 
                                    ? ReadTableStates::LOAD_TO_DUCKDB 
                                    : ReadTableStates::EXTRACT_FROM_SAP;
                    
                break;
            }
            case ReadTableStates::FINAL_LOAD_TO_DUCKDB: {
                auto loaded_to_duckdb = LoadNextBatchToDuckDBColumn(bind_data, output);
                duck_count += 1;
                pending_records -= loaded_to_duckdb;

                current_state = (pending_records > 0)
                                    ? ReadTableStates::FINAL_LOAD_TO_DUCKDB
                                    : ReadTableStates::FINISHED;

                break;
            }
            case ReadTableStates::FINISHED: {
                // Never actually reached / called, 
                // as the Finished() method will return true;
                break;
            }
        }
    }

    std::vector<Value> RfcReadTableLocalState::CreateFunctionArguments(RfcReadTableBindData &bind_data) 
    {
        auto table_name = bind_data.table_name;
        auto options = bind_data.GetOptions();
        auto fields = bind_data.GetField(column_idx);
        
        auto args = ArgBuilder()
            .Add("QUERY_TABLE", Value(table_name))
            .Add("ROWSKIPS", Value::CreateValue(batch_count * desired_batch_size))
            .Add("ROWCOUNT", Value::CreateValue(desired_batch_size))
            .Add("OPTIONS", options)
            .Add("FIELDS", fields);

        return args.BuildArgList();
    }

    unsigned int RfcReadTableLocalState::ExecuteNextTableReadForColumn(RfcReadTableBindData &bind_data, std::shared_ptr<RfcConnection> connection) 
    {
        auto func = std::make_shared<RfcFunction>(connection, "RFC_READ_TABLE");
        auto func_args = CreateFunctionArguments(bind_data);
        auto func_args_debug = func_args.front().ToSQLString();
        auto invocation = func->BeginInvocation(func_args);
        current_result_data = invocation->Invoke("/DATA");

        return current_result_data->TotalRows();
    }

    unsigned int RfcReadTableLocalState::LoadNextBatchToDuckDBColumn(RfcReadTableBindData &bind_data, DataChunk &output) 
    {
        auto result_vec = ListValue::GetChildren(current_result_data->GetResultValue("/"));
        auto batch_start = result_vec.begin() + duck_count * STANDARD_VECTOR_SIZE;
        auto batch_end = batch_start + STANDARD_VECTOR_SIZE > result_vec.end() 
                            ? result_vec.end() 
                            : batch_start + STANDARD_VECTOR_SIZE;
        auto batch = duckdb::vector<Value>(batch_start, batch_end);

        for (idx_t i = 0; i < batch.size(); i++) {
            output.SetValue(column_idx, i, ParseCsvValue(bind_data, batch[i]));
        }
        
        output.SetCardinality(batch.size());
        return batch.size();
    }

    Value RfcReadTableLocalState::ParseCsvValue(RfcReadTableBindData &bind_data, Value &orig)
    {
        auto rfc_type = bind_data.GetColumnType(column_idx);
        return rfc_type.ConvertCsvValue(orig);
    }

} // namespace duckdb