#pragma once

#include <mutex>
#include <optional>
#include <thread>

#include "duckdb.hpp"
#include "duckdb/parallel/base_pipeline_event.hpp"
#include "duckdb/parallel/task_executor.hpp"
#include "sapnwrfc.h"

#include "sap_function.hpp"

namespace duckdb 
{
	string RfcFunctionDesc(ClientContext &context, const FunctionParameters &parameters);

	// Toggle for caching one RFC connection + function descriptor per
	// RfcReadColumnStateMachine across all batches of a scan.  Default true.
	// Wired to the `erpl_rfc_persistent_connections` extension option.
	void SetRfcPersistentConnections(bool enabled);
	bool GetRfcPersistentConnections();

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

			void SetSecretName(const std::string &name) { secret_name = name; }
			const std::string &GetSecretName() const { return secret_name; }

		private:
			std::string secret_name;
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
			bool IsRowIdColumnId();
			void SetRowIdColumnId();
			unsigned int GetCardinality();
			unsigned int GetBatchCount();
			unsigned int GetDesiredBatchSize();
			std::string ToString();

			// Persistent-connection cache (issue #63 throughput follow-up).
			// Each state machine owns one RFC connection and one cached
			// RfcFunction descriptor for the duration of a scan, instead of
			// opening + logging on + RfcGetFunctionDesc'ing on every batch.
			// AcquireConnection returns the cached connection if alive, else
			// opens a fresh one via bind_data->OpenNewConnection().
			// AcquireFunction does the same for the function descriptor,
			// rebuilding it whenever the function name changes (RFC_READ_TABLE
			// fallback path) or the connection has been invalidated.
			// InvalidateCachedConnection drops both — call on any RFC error so
			// the next batch starts from a clean state.
			std::shared_ptr<RfcConnection> AcquireConnection();
			std::shared_ptr<RfcFunction> AcquireFunction(std::shared_ptr<RfcConnection> connection,
			                                             const std::string &function_name);
			void InvalidateCachedConnection();

		public:
			// Per-RFC batch size.  RFC_READ_TABLE enforces server-side that
			// ROWSKIPS must be an integer multiple of ROWCOUNT.  We start
			// each scan at a small STANDARD_VECTOR_SIZE batch and double
			// after each round-trip — but only at moments when total_rows
			// is divisible by the larger size.  That preserves the ABAP
			// invariant while keeping the first batch cheap so LIMIT N
			// queries terminate before paying for a full SDK buffer.  Cap
			// is a power-of-two so the doubling sequence reaches it cleanly.
			static constexpr unsigned int MAX_BATCH_SIZE = 16 * STANDARD_VECTOR_SIZE;

			// Pure helpers that encode the two ABAP-divisibility-preserving
			// rules used inside the state machine.  Exposed so they can be
			// tested without driving a live RFC scan.
			//
			//   NextDesiredBatchSize: the size to use for the *next* RFC call
			//     after a successful EXTRACT.  Doubles the current size only
			//     when total_rows is already a multiple of the larger size,
			//     so ROWSKIPS % ROWCOUNT stays valid forever.
			//
			//   TrimmedActualBatchSize: the ROWCOUNT to send when MAX_ROWS
			//     would otherwise force the final batch below desired_batch_size.
			//     Returns the desired size unchanged when trimming would
			//     produce an illegal (non-divisor) ROWCOUNT — in that case
			//     we over-fetch and rely on LoadNextBatchToDuckDBColumn() to
			//     clip the surplus client-side.
			static unsigned int NextDesiredBatchSize(unsigned int current, unsigned int total_rows_after);
			static unsigned int TrimmedActualBatchSize(unsigned int desired, unsigned int total_rows, unsigned int limit);

		private:
			bool active = true;
			bool row_id_column_id = false;
			unsigned int desired_batch_size = STANDARD_VECTOR_SIZE;
			unsigned int pending_records = 0;
			unsigned int cardinality = 0;
			unsigned int batch_count = 0;
			unsigned int duck_count = 0;
			unsigned int total_rows = 0;
			unsigned int limit;
			std::shared_ptr<RfcConnection> cached_connection;
			std::shared_ptr<RfcFunction> cached_function;
			std::string cached_function_name;
			// The SAP NW RFC SDK requires that any one RFC_CONNECTION_HANDLE
			// be used by at most one thread.  DuckDB's TaskExecutor pumps
			// tasks from a shared worker pool, so a single state machine's
			// successive batches can land on different worker threads.
			// When that happens we drop the cached handle and open a fresh
			// one on the new thread.
			std::optional<std::thread::id> cached_connection_thread;

			idx_t column_idx;
			idx_t projected_column_idx = DConstants::INVALID_INDEX;

			RfcReadTableBindData *bind_data;
			ReadTableStates current_state = ReadTableStates::INIT;
			std::shared_ptr<RfcResultSet> current_result_data = nullptr;

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
