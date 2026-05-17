#include "scanner_describe_function.hpp"
#include "telemetry.hpp"
#include "erpl_tracing.hpp"

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

    // Create the function — gives us the SDK-side parameter introspection
    // (param names, types, directions). This works for every callable RFC.
    auto func_name = inputs[0].ToString();
    auto func_handle = std::make_shared<RfcFunction>(connection, func_name);

    // Reading the ABAP source/short text/function pool requires
    // RPY_FUNCTIONMODULE_READ, which fails with FL180 ("Source wider than
    // 72 char") on a small number of older modules (e.g. SXPG_COMMAND_EXECUTE,
    // IDOC_INBOUND_ASYNCHRONOUS, STFC_RETURN_DATA). When that happens we
    // gracefully degrade: keep the SDK-side parameter info and leave the
    // RPY-sourced metadata fields (text, function_group, remote_callable,
    // source) empty rather than failing the whole describe call.
    std::shared_ptr<RfcResultSet> result_set;
    try {
        auto func_args = CreateFunctionArguments(func_name);
        result_set = RfcResultSet::InvokeFunction(connection, "RPY_FUNCTIONMODULE_READ", func_args);
    } catch (std::exception &ex) {
        ERPL_TRACE_WARN_DATA("sap_rfc_describe_function",
                             "RPY_FUNCTIONMODULE_READ failed; falling back to SDK-only describe",
                             ex.what());
        result_set = nullptr;
    }

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