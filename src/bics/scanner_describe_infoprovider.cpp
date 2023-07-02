#include "duckdb.hpp"
#include "scanner_search_infoprovider.hpp"

#include "duckdb_argument_helper.hpp"
#include "bics.hpp"

namespace duckdb 
{
    static string BicsDescribeInfoProviderToString(const FunctionData *bind_data_p) 
    {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static void FetchAndCreateInfoProviderDefinition(std::shared_ptr<RfcConnection> connection,
                                                     std::shared_ptr<BicsSession> session) 
    {
        auto func_args = ArgBuilder().Add("I_DATA_PROVIDER_HANDLE", session->GetDataProviderHandle())
                                     .BuildArgList();
        auto result_set = RfcResultSet::InvokeFunction(connection, "BICS_PROV_GET_DESIGN_TIME_INFO", func_args);
        auto characteristics_tbl = result_set->GetResultValue("E_SX_META_DATA/CHARACTERISTICS");
        auto query_state_struct = result_set->GetResultValue("E_SX_META_DATA/QUERY_STATE");
        auto query_props_struct = result_set->GetResultValue("E_SX_META_DATA/QUERY_PROPERTIES");
        auto dimensions_tbl = result_set->GetResultValue("E_SX_META_DATA/DIMENSIONS");

        printf("Metadata elements:\n");
    }                                         

    static unique_ptr<FunctionData> BicsDescribeCubeBind(ClientContext &context, 
                                                        TableFunctionBindInput &input, 
                                                        vector<LogicalType> &return_types, 
                                                        vector<string> &names) 
    {
        auto &inputs = input.inputs;

        // Connect to the SAP system
        auto connection = RfcAuthParams::FromContext(context).Connect();

        // Create the bics session
        auto cube_name = inputs[0].GetValue<string>();
        auto session = make_shared<BicsSession>(connection, cube_name); // will be closed when the session goes out of scope

        // Create the infoprovider defintion
        FetchAndCreateInfoProviderDefinition(connection, session);

        auto bind_data = make_uniq<RfcFunctionBindData>();
        
        return std::move(bind_data);
    }

    static void BicsDescribeCubeScan(ClientContext &context, 
                                     TableFunctionInput &data, 
                                     DataChunk &output) 
    {
        auto &bind_data = data.bind_data->Cast<RfcFunctionBindData>();
        auto result_set = bind_data.result_set;
        
        if (! result_set->HasMoreResults()) {
            return;
        }

        result_set->FetchNextResult(output);
    }

    CreateTableFunctionInfo CreateBicsDescribeInfoProviderScanFunction() 
    {
        auto fun = TableFunction("sap_bics_describe_cube", {LogicalType::VARCHAR}, BicsDescribeCubeScan, BicsDescribeCubeBind);
        fun.to_string = BicsDescribeInfoProviderToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb