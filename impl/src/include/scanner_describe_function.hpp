#pragma once

#include "duckdb.hpp"
#include "sapnwrfc.h"

#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

#include "sap_connection.hpp"
#include "sap_function.hpp"

namespace duckdb 
{
    class DescribeFunctionResult 
    {
        public:
            DescribeFunctionResult(std::shared_ptr<RfcFunction> function);

            std::vector<std::string> GetResultNames();
            std::vector<LogicalType> GetResultTypes();

            bool HasMoreResults();
            void FetchNextResult(DataChunk &output);

        private:
            std::shared_ptr<RfcFunction> _function;
            bool _has_more_results;

            LogicalType GetParamterResultType();
            Value ConvertParameterInfos(std::vector<RfcFunctionParameterDesc> &param_infos);
    };

    struct RfcDescribeFunctionBindData : public TableFunctionData
    {
        std::shared_ptr<DescribeFunctionResult> result;
    };

    CreateTableFunctionInfo CreateRfcDescribeFunctionScanFunction();
} // namespace duckdb