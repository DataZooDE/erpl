#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

#include "sap_function.hpp"

namespace duckdb 
{
	string RfcFunctionDesc(ClientContext &context, const FunctionParameters &parameters);

	struct RfcReadTableGlobalState; // forward declaration
	struct RfcReadTableLocalState; // forward declaration

	struct RfcReadTableBindData : public TableFunctionData
    {
		static const idx_t MAX_OPTION_LEN = 70;

		RfcReadTableBindData(std::string table_name);

		void SetOptionsFromWhereClause(std::string &where_clause);
		void SetAndVerifyFields(std::shared_ptr<RfcConnection> connection, std::vector<std::string> &fields);

		std::vector<std::string> GetColumnNames();
		std::vector<LogicalType> GetReturnTypes();
		RfcType GetColumnType(unsigned int column_idx);
		duckdb::vector<Value> GetOptions();
		duckdb::vector<Value> GetFields();
		duckdb::vector<Value> GetField(unsigned int column_idx);

		std::unique_ptr<RfcReadTableGlobalState> InitializeGlobalState(ClientContext &context, idx_t max_threads);
		std::unique_ptr<RfcReadTableGlobalState> InitializeGlobalState(ClientContext &context);

		std::string table_name;
		std::vector<std::string> fields;
		std::vector<std::string> options;

		private:
			std::vector<std::string> column_names;
			std::vector<RfcType> column_types;

		public:
			static std::vector<Value> GetTableFieldMetas(std::shared_ptr<RfcConnection> connection, std::string table_name);
			static RfcType GetRfcTypeForFieldMeta(Value &DFIES_entry);
    };

	struct RfcReadTableGlobalState : public GlobalTableFunctionState {
		RfcReadTableGlobalState(idx_t max_threads) : max_threads(max_threads) 
		{ }

		mutex lock;
		idx_t max_threads;
		std::vector<bool> finished_threads;

		idx_t MaxThreads() const override {
			return max_threads;
		}

		bool AllFinished();
		std::unique_ptr<RfcReadTableLocalState> InitializeLocalState();
	};

	enum class ReadTableStates {
		INIT,
		EXTRACT_FROM_SAP,
		LOAD_TO_DUCKDB,
		FINAL_LOAD_TO_DUCKDB,
		FINISHED
	};

	struct RfcReadTableLocalState : public LocalTableFunctionState 
	{
		int column_idx = -1;
		ReadTableStates current_state;
		std::shared_ptr<RfcResultSet> current_result_data;

		unsigned int desired_batch_size = 50000;
		unsigned int pending_records = 0;
		unsigned int batch_count = 0;
		unsigned int duck_count = 0;

		bool Finished();
		void Step(RfcReadTableBindData &bind_data, std::shared_ptr<RfcConnection> connection, DataChunk &output);

		private:
			unsigned int ExecuteNextTableReadForColumn(RfcReadTableBindData &bind_data, std::shared_ptr<RfcConnection> connection);
			std::vector<Value> CreateFunctionArguments(RfcReadTableBindData &bind_data);

			unsigned int LoadNextBatchToDuckDBColumn(RfcReadTableBindData &bind_data, DataChunk &output);
			Value ParseCsvValue(RfcReadTableBindData &bind_data, Value &value);
	};

} // namespace duckdb