#include "scanner_describe_function.hpp"
#include "telemetry.hpp"

namespace duckdb 
{

static std::vector<Value> CreateFunctionArguments(std::string &func_name)
{
    return ArgBuilder()
        .Add("FUNCTIONNAME", func_name)
        .BuildArgList();
}

static unique_ptr<FunctionData> RfcDescribeFunctionBind(ClientContext &context, 
                                                        TableFunctionBindInput &input, 
                                                        vector<LogicalType> &return_types, 
                                                        vector<string> &names)
{
    PostHogTelemetry::Instance().CaptureFunctionExecution("sap_rfc_describe_function");

    auto &inputs = input.inputs;
    
    // Connect to the SAP system
    auto connection = RfcAuthParams::FromContext(context).Connect();

    // Create the function
    auto func_name = inputs[0].ToString();
    auto func_handle = make_shared<RfcFunction>(connection, func_name);

    auto func_args = CreateFunctionArguments(func_name);
    auto result_set = RfcResultSet::InvokeFunction(connection, "RPY_FUNCTIONMODULE_READ", func_args);

    auto bind_data = RfcDescribeFunctionBindData::FromFunctionHandleAndResultSet(func_handle, result_set);
    names = bind_data->GetResultNames();
    return_types = bind_data->GetResultTypes();
    
    return std::move(bind_data);
}

static void RfcDescribeFunctionScan(ClientContext &context, 
                                    TableFunctionInput &data, 
                                    DataChunk &output) 
{
    auto &bind_data = data.bind_data->CastNoConst<RfcDescribeFunctionBindData>();
    if (! bind_data.HasMoreResults()) {
        return;
    }

    bind_data.FetchNextResult(output);
}

TableFunction CreateRfcDescribeFunctionScanFunction() 
{
    auto fun = TableFunction("sap_rfc_describe_function", {LogicalType::VARCHAR}, RfcDescribeFunctionScan, RfcDescribeFunctionBind);
    return fun;
}

} // namespace duckdb