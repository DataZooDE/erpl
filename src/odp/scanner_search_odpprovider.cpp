#include "duckdb.hpp"
#include "scanner_search_infoprovider.hpp"


namespace duckdb 
{
    static unique_ptr<FunctionData> OdpSearchOdpProviderBind(ClientContext &context, 
                                                              TableFunctionBindInput &input, 
                                                              vector<LogicalType> &return_types, 
                                                              vector<string> &names) 
    {
        return nullptr;
    }

    static void OdpSearchOdpProviderScan(ClientContext &context, 
                                          TableFunctionInput &data, 
                                          DataChunk &output) 
    {
    }

    static string OdpSearchOdpProviderToString(const FunctionData *bind_data_p) {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    CreateTableFunctionInfo CreateOdpSearchOdpProviderScanFunction() 
    {
        auto fun = TableFunction("sap_odp_search_provider", {}, OdpSearchOdpProviderScan, OdpSearchOdpProviderBind);
        fun.to_string = OdpSearchOdpProviderToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb