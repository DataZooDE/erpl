#include "duckdb.hpp"
#include "scanner_show_groups.hpp"
#include "telemetry.hpp"

namespace duckdb 
{
static std::vector<Value> CreateFunctionArguments(TableFunctionBindInput &input) {
    child_list_t<Value> args;

    auto &named_params = input.named_parameters;
    if (named_params.find("GROUPNAME") != named_params.end()) {
        args.push_back(make_pair("GROUPNAME", named_params["GROUPNAME"])) ;
    }
    else {
        args.push_back(make_pair("GROUPNAME", Value::CreateValue("*")));
    }
    
    if (named_params.find("LANGUAGE") != named_params.end()) {
        args.push_back(make_pair("LANGUAGE", named_params["LANGUAGE"])) ;
    }

    return std::vector<Value>( { Value::STRUCT(args) });
}

/**
 * @brief (Step 1) Binds the input arguments to the function.
*/
static unique_ptr<FunctionData> RfcShowGroupBind(ClientContext &context, 
                                                        TableFunctionBindInput &input, 
                                                        vector<LogicalType> &return_types, 
                                                        vector<string> &names) 
{
    PostHogTelemetry::Instance().CaptureFunctionExecution("sap_rfc_search_group");

    // Connect to the SAP system
    auto connection = RfcAuthParams::FromContext(context).Connect();

    // Create the function
    auto func = make_shared<RfcFunction>(connection, "RFC_GROUP_SEARCH");
    auto func_args = CreateFunctionArguments(input);
    auto result_set = RfcResultSet::InvokeFunction(connection, "RFC_GROUP_SEARCH", func_args, "/GROUPS");
    names = std::vector<string>( { "name", "text" });
    return_types = result_set->GetResultTypes();

    auto bind_data = make_uniq<RfcFunctionBindData>();
    bind_data->result_set = result_set;

    return std::move(bind_data);
}

static void RfcShowGroupScan(ClientContext &context, 
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

TableFunction CreateRfcShowGroupScanFunction() 
{
    auto fun = TableFunction("sap_rfc_show_groups", {}, RfcShowGroupScan, RfcShowGroupBind);
    fun.named_parameters["GROUPNAME"] = LogicalType::VARCHAR;
    fun.named_parameters["LANGUAGE"] = LogicalType::VARCHAR;

    return fun;
}

} // namespace duckdb