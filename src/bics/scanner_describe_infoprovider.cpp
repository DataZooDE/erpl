#include "duckdb.hpp"
#include "scanner_search_infoprovider.hpp"

#include "duckdb_argument_helper.hpp"
#include "duckdb_json_helper.hpp"
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
        
        auto meta = result_set->GetResultValue("E_SX_META_DATA");
        auto meta_helper = ValueHelper(meta);

        auto characteristics_tbl = meta_helper["/CHARACTERISTICS"];     // BICS_PROV_META_TH_CHARACTERIST
        auto query_state_struct = meta_helper["/QUERY_STATE"];          // BICS_PROV_META_STRUCTURE_INFO
        auto query_props_struct = meta_helper["/QUERY_PROPERTIES"];     // BICS_PROV_META_QUERY
        auto dimensions_tbl = meta_helper["/DIMENSIONS"];

        printf("Metadata elements:\n");

        auto json = ErpelSerializer::Serialize(meta, true);
        auto restored_val = ErpelSerializer::Deserialize(json);
        printf("%s\n", json.c_str());
        /*
        Columns:
            - Dimensions <- /DIMENSIONS
                - DataPackage <- ???
                - Time (0D_NW_T01T) <- /DIMENSIONS/TEXT (/DIMENSIONS/NAME)
                    - CalendarYear (0CALYEAR) <- /CHARACTERISTICS/TEXT (/CHARACTERISTICS/NAME)
                        - 15 <- /CHARACTERISTICS/ID
                        - Reference to dimension <- /CHARACTERISTICS/DIMENSION
                    - 0CALMONTH ...
                - Product
                    - Product (0D_NW_PROD) <- /DIMENSIONS/TEXT (/DIMENSIONS/NAME)
                        - 502 <- /CHARACTERISTICS/ID
                        - 0D_NW_T013 <- /CHARACTERISTICS/DIMENSION
                        - Reference to dimension <- /CHARACTERISTICS/DIMENSION
                        - InfoObject <- 20230715T165529.571Z_39_000009BC_BAPI_IOBJ_GETDETAIL_cr.xml
                            Type: CHAR <- DATATP
                            Length: 60 <- OUTPUTLEN
                        - Attributes <- /CHARACTERISTICS/ATTRIBUTES
                            - Product Category (0D_NW_PRDCT) <- /CHARACTERISTICS/ATTRIBUTES/TEXT (/CHARACTERISTICS/ATTRIBUTES/NAME)
                                - 0D_NW_PROD__0D_NW_PRDCT
                                    - Reference via Base Characteristic <- /CHARACTERISTICS/BASE_CHARACTERISTIC_ID
                            - Product Group (0D_NW_PRDGP) <- /CHARACTERISTICS/ATTRIBUTES/TEXT (/CHARACTERISTICS/ATTRIBUTES/NAME)

        
        */

        /*
        auto func_args = ArgBuilder().Add("I_DATA_PROVIDER_HANDLE", session->GetDataProviderHandle())
                                     .Add("I_VARIABLE_CONTAINER_HANDLE", session->GetVariableContainerHandle())
                                     .BuildArgList();

        auto result_set = RfcResultSet::InvokeFunction(connection, "BICS_PROV_VAR_GET_VARIABLES", func_args);
        auto meta = result_set->GetResultValue("E_TH_META_CHARACTERISTIC");
        auto chars = result_set->GetResultValue("E_TH_STATE_CHARACTERISTIC");

        */
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