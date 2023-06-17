#include "duckdb.hpp"
#include "scanner_search_infoprovider.hpp"


namespace duckdb 
{
    static unique_ptr<FunctionData> BicsSearchInfoProviderBind(ClientContext &context, 
                                                               TableFunctionBindInput &input, 
                                                               vector<LogicalType> &return_types, 
                                                               vector<string> &names) 
    {
        return nullptr;
    }

    static void BicsSearchInfoProviderScan(ClientContext &context, 
                                           TableFunctionInput &data, 
                                           DataChunk &output) 
    {
    }

    static string BicsSearchInfoProviderToString(const FunctionData *bind_data_p) {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    CreateTableFunctionInfo CreateBicsSearchInfoProviderScanFunction() 
    {
        auto fun = TableFunction("sap_bics_search_infoprovider", {}, BicsSearchInfoProviderScan, BicsSearchInfoProviderBind);
        fun.to_string = BicsSearchInfoProviderToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb