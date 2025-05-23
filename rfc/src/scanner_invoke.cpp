#include "duckdb.hpp"
#include "scanner_invoke.hpp"
#include "sap_secret.hpp"
#include "telemetry.hpp"

namespace duckdb 
{
    std::string GetPathNamedParam(TableFunctionBindInput &input) {
        auto &named_params = input.named_parameters;
        if (named_params.find("path") != named_params.end()) {
            return named_params["path"].GetValue<string>();
        } else {
            return std::string();
        }
    }

    /**
     * @brief (Step 1) Binds the input arguments to the function.
    */
    static unique_ptr<FunctionData> RfcInvokeBind(ClientContext &context, 
                                                  TableFunctionBindInput &input, 
                                                  vector<LogicalType> &return_types, 
                                                  vector<string> &names) 
    {
        PostHogTelemetry::Instance().CaptureFunctionExecution("sap_rfc_invoke");

        auto &inputs = input.inputs;
        
        // Connect to the SAP system
        auto connection = GetAuthParamsFromContext(context, input).Connect();

        // Create the function
        auto func_name = inputs[0].GetValue<string>();
        auto func_args = std::vector<Value>(inputs.begin()+1, inputs.end());
        auto path = GetPathNamedParam(input);
        auto result_set = RfcResultSet::InvokeFunction(connection, func_name, func_args, path);
        names = result_set->GetResultNames();
        return_types = result_set->GetResultTypes();

        // Make this invocation available to the bind data
        auto bind_data = make_uniq<RfcFunctionBindData>();
        bind_data->result_set = result_set;

        return std::move(bind_data);
    }

    /**
     * @brief (Step 2) Scans the function and returns the next chunk of data.
    */
    static void RfcInvokeScan(ClientContext &context, 
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

    TableFunction CreateRfcInvokeScanFunction() 
    {
        auto fun = TableFunction("sap_rfc_invoke", { LogicalType::VARCHAR }, RfcInvokeScan, RfcInvokeBind);
        fun.projection_pushdown = false;
        fun.varargs = LogicalType::ANY;
        fun.named_parameters["path"] = LogicalType::VARCHAR;
        fun.named_parameters["secret"] = LogicalType::VARCHAR;

        return fun;
    }
} // namespace duckdb