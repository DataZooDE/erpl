#pragma once

#include <mutex>

#include "duckdb.hpp"
#include "duckdb/parallel/base_pipeline_event.hpp"
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

	class RfcReadTableBindData : public TableFunctionData
    {
		public: 
			static const idx_t MAX_OPTION_LEN = 70;

			RfcReadTableBindData(std::string table_name, int max_read_threads, RfcConnectionFactory_t connection_factory, ClientContext &context);

			void InitOptionsFromWhereClause(std::string &where_clause);
			void InitAndVerifyFields(std::vector<std::string> req_fields);
			
			std::vector<std::string> GetColumnNames();
			duckdb::vector<Value> GetColumnName(unsigned int column_idx);
			std::vector<LogicalType> GetReturnTypes();
			RfcType GetColumnType(unsigned int column_idx);
			duckdb::vector<Value> GetOptions();
			std::shared_ptr<RfcConnection> OpenNewConnection();
			
			bool HasMoreResults();
			void Step(ClientContext &context, DataChunk &output);

			std::string ToString();
		
		public: 
			std::string table_name;
			std::vector<std::string> options;
			unsigned int limit = 0;
			unsigned int max_threads = 0;
			
		private:
			RfcConnectionFactory_t connection_factory;
			ClientContext &client_context;
			std::vector<std::string> column_names;
			std::vector<RfcType> column_types;
			std::vector<RfcReadColumnStateMachine> column_state_machines;

			std::vector<RfcReadColumnStateMachine> CreateReadColumnStateMachines();
		public:
			static std::vector<Value> GetTableFieldMetas(std::shared_ptr<RfcConnection> connection, std::string table_name);
			static RfcType GetRfcTypeForFieldMeta(Value &DFIES_entry);
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

			bool Finished();
			std::shared_ptr<RfcReadColumnTask> CreateTaskForNextStep(ClientContext &client_context, duckdb::Vector &column_output);
			unsigned int GetColumnIndex();
			unsigned int GetCardinality();
			std::string ToString();

		private:
			unsigned int desired_batch_size = 50000;
			unsigned int pending_records = 0;
			unsigned int cardinality = 0;
			unsigned int batch_count = 0;
			unsigned int duck_count = 0;
			unsigned int total_rows = 0;
			unsigned int limit = 0;

			idx_t column_idx;

			RfcReadTableBindData *bind_data;
			ReadTableStates current_state = ReadTableStates::INIT;
			std::shared_ptr<RfcResultSet> current_result_data = nullptr;

			std::mutex thread_lock;
	};

	class RfcReadColumnTask : public ExecutorTask 
	{
		public:
			RfcReadColumnTask(RfcReadColumnStateMachine *owning_state_machine, ClientContext &client_context, duckdb::Vector &current_column_output);
			TaskExecutionResult ExecuteTask(TaskExecutionMode mode) override;
			
		private:
			unsigned int ExecuteNextTableReadForColumn();
			std::vector<Value> CreateFunctionArguments();

			unsigned int LoadNextBatchToDuckDBColumn();
			Value ParseCsvValue(Value &orig);

		private:
			RfcReadColumnStateMachine *owning_state_machine;
			duckdb::Vector &current_column_output;
	};
} // namespace duckdb