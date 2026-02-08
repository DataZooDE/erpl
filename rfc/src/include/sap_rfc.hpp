#pragma once

#include <mutex>
#include <optional>

#include "duckdb.hpp"
#include "duckdb/parallel/base_pipeline_event.hpp"
#include "duckdb/parallel/task_executor.hpp"
#include "sapnwrfc.h"

#include "sap_function.hpp"

namespace duckdb 
{
	string RfcFunctionDesc(ClientContext &context, const FunctionParameters &parameters);

	//struct RfcReadTableGlobalState; // forward declaration
	//struct RfcReadTableLocalState; // forward declaration
	class RfcReadColumnStateMachine; // forward declaration
	class RfcReadColumnTask; // forward declaration

	typedef std::shared_ptr<RfcConnection> (* RfcConnectionFactory_t)(ClientContext &context);
	std::shared_ptr<RfcConnection> DefaultRfcConnectionFactory(ClientContext &context);

	class RfcReadTableBindData : public TableFunctionData
    {
		public: 
			static const idx_t MAX_OPTION_LEN = 70;

			RfcReadTableBindData(std::string table_name, 
								 int max_read_threads,
								 unsigned int limit, 
								 std::string read_table_function,
								 std::string read_table_delimiter,
								 bool read_table_function_user_set,
								 RfcConnectionFactory_t connection_factory, 
								 ClientContext &context);
			RfcReadTableBindData(std::string table_name,
								 int max_read_threads,
								 unsigned int limit,
								 RfcConnectionFactory_t connection_factory,
								 ClientContext &context);

			void InitOptionsFromWhereClause(std::string &where_clause);
			void AddOptionsFromWhereClause(std::string &where_clause);
			void InitAndVerifyFields(std::vector<std::string> req_fields);
			
			void ActivateColumns(vector<column_t> &column_ids);
			void AddOptionsFromFilters(duckdb::optional_ptr<duckdb::TableFilterSet> filters);

			std::vector<std::string> GetRfcColumnNames();
			duckdb::vector<Value> GetRfcColumnName(unsigned int column_idx);
			std::string GetProjectedColumnName(unsigned int projected_column_idx);
			std::vector<LogicalType> GetReturnTypes();
			RfcType GetColumnType(unsigned int column_idx);
			duckdb::vector<Value> GetOptions();
			std::shared_ptr<RfcConnection> OpenNewConnection();
			std::string GetReadTableFunctionName();
			std::string GetReadTableDelimiter();
			bool IsReadTableFunctionUserSet();
			bool ReadTableFunctionSupportsEtData(std::shared_ptr<RfcConnection> connection, const std::string &function_name);
			bool ReadTableSupportsEtDataSwitch(std::shared_ptr<RfcConnection> connection, const std::string &function_name);
			void ValidateReadTableFunctionName();
			void ResolveReadTableFunctionForStringTypes(std::shared_ptr<RfcConnection> connection);
			bool ReadTableSupportsEtData();
			bool TrySelectFallbackReadTableFunction(std::shared_ptr<RfcConnection> connection);
			void ResolveReadTableResultPath(std::shared_ptr<RfcConnection> connection);
			void ResolveReadTableImportParams(std::shared_ptr<RfcConnection> connection);
			bool ReadTableHasParam(const std::string &param_name);
			
			bool HasMoreResults();
			void Step(ClientContext &context, DataChunk &output);

			std::string ToString();
			double GetProgress();

		public: 
			std::string table_name;
			std::vector<std::string> options;
			unsigned int limit = 0;
			unsigned int max_threads = 0;
			std::string read_table_function;
			std::string read_table_delimiter;
			bool read_table_function_user_set = false;
			std::optional<bool> read_table_supports_et_data;
			std::optional<bool> read_table_supports_et_data_switch;
			std::string read_table_result_path;
			std::set<std::string> read_table_import_params;
			
		private:
			RfcConnectionFactory_t connection_factory;
			ClientContext &client_context;
			std::vector<std::string> column_names;
			std::vector<RfcType> column_types;
			std::vector<RfcReadColumnStateMachine> column_state_machines;

			std::vector<RfcReadColumnStateMachine> CreateReadColumnStateMachines();
			unsigned int NActiveStateMachines();
			unsigned int FirstActiveStateMachineCardinality();
			bool AreActiveStateMachineCaridnalitiesEqual();
		public:
			static std::vector<Value> GetTableFieldMetas(std::shared_ptr<RfcConnection> connection, std::string table_name);
			static RfcType GetRfcTypeForFieldMeta(Value &DFIES_entry);
	
			static std::string TransformFilter(std::string &column_name, TableFilter &filter);
			static std::string TransformLiteral(const Value &val);
			static std::string TransformBlob(const std::string &val);
			static std::string CreateExpression(string &column_name, vector<unique_ptr<TableFilter>> &filters, string op);
			static std::string TransformComparision(ExpressionType type);
    };

	enum class ReadTableStates {
		INIT,
		EXTRACT_FROM_SAP,
		LOAD_TO_DUCKDB,
		FINAL_LOAD_TO_DUCKDB,
		FINISHED
	};

	std::string ReadTableStatesToString(ReadTableStates &state);

	class RfcReadColumnStateMachine 
	{
		friend class RfcReadColumnTask;

		public:
			RfcReadColumnStateMachine(RfcReadTableBindData *bind_data, idx_t column_idx, unsigned int limit);
			RfcReadColumnStateMachine(const RfcReadColumnStateMachine& other);
			~RfcReadColumnStateMachine();

			bool Active();
			void SetInactive();
			void SetActive(idx_t projected_column_idx);
			bool Finished();
			duckdb::unique_ptr<RfcReadColumnTask> CreateTaskForNextStep(duckdb::TaskExecutor &executor, duckdb::Vector &column_output);
			unsigned int GetRfcColumnIndex();
			unsigned int GetProjectedColumnIndex();
			std::shared_ptr<RfcConnection> GetConnection();
			bool IsRowIdColumnId();
			void SetRowIdColumnId();
			unsigned int GetCardinality();
			unsigned int GetBatchCount();
			std::string ToString();

		private:
			bool active = true;
			bool row_id_column_id = false;
			unsigned int desired_batch_size = 20*STANDARD_VECTOR_SIZE;
			unsigned int pending_records = 0;
			unsigned int cardinality = 0;
			unsigned int batch_count = 0;
			unsigned int duck_count = 0;
			unsigned int total_rows = 0;
			unsigned int limit;

			idx_t column_idx;
			idx_t projected_column_idx = DConstants::INVALID_INDEX;

			RfcReadTableBindData *bind_data;
			ReadTableStates current_state = ReadTableStates::INIT;
			std::shared_ptr<RfcResultSet> current_result_data = nullptr;
			std::shared_ptr<RfcConnection> current_connection = nullptr;

			std::mutex thread_lock;
	};

	class RfcReadColumnTask : public duckdb::BaseExecutorTask 
	{
		public:
			RfcReadColumnTask(RfcReadColumnStateMachine *owning_state_machine, duckdb::TaskExecutor &executor, duckdb::Vector &current_column_output);
			void ExecuteTask();
			
		private:
			unsigned int ExecuteNextTableReadForColumn();
			std::vector<Value> CreateFunctionArguments(const std::string &delimiter, bool use_et_data);

			unsigned int LoadNextBatchToDuckDBColumn();
			Value ParseCsvValue(Value &orig);

		private:
			RfcReadColumnStateMachine *owning_state_machine;
			duckdb::Vector &current_column_output;
	};
} // namespace duckdb
