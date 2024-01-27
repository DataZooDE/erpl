#include "duckdb.hpp"
#include "scanner_show_functions.hpp"
#include "telemetry.hpp"

namespace duckdb 
{
    static std::vector<Value> CreateFunctionArguments(TableFunctionBindInput &input) {
        child_list_t<Value> args;

        auto &named_params = input.named_parameters;
        if (named_params.find("FUNCNAME") != named_params.end()) {
            args.push_back(make_pair("FUNCNAME", named_params["FUNCNAME"])) ;
        } else {
            args.push_back(make_pair("FUNCNAME", Value::CreateValue("*")));
        }

        if (named_params.find("GROUPNAME") != named_params.end()) {
            args.push_back(make_pair("GROUPNAME", named_params["GROUPNAME"])) ;
        }
        
        if (named_params.find("LANGUAGE") != named_params.end()) {
            args.push_back(make_pair("LANGUAGE", named_params["LANGUAGE"])) ;
        }

        return std::vector<Value>( { Value::STRUCT(args) });
    }

    /**
     * @brief (Step 1) Binds the input arguments to the function.
    */
    static unique_ptr<FunctionData> RfcShowTablesBind(ClientContext &context, 
                                                          TableFunctionBindInput &input, 
                                                          vector<LogicalType> &return_types, 
                                                          vector<string> &names) 
    {   
        PostHogTelemetry::Instance().CaptureFunctionExecution("sap_rfc_search_function");
        
        // Connect to the SAP system
        auto connection = RfcAuthParams::FromContext(context).Connect();

        // Create the function
        auto func = make_shared<RfcFunction>(connection, "RFC_FUNCTION_SEARCH");
        auto func_args = CreateFunctionArguments(input);
        auto invocation = func->BeginInvocation(func_args);
        auto result_set = invocation->Invoke("/FUNCTIONS");
        names = result_set->GetResultNames();
        return_types = result_set->GetResultTypes();

        auto bind_data = make_uniq<RfcFunctionBindData>();
        bind_data->invocation = invocation;
        bind_data->result_set = result_set;

        return std::move(bind_data);
    }

    /**
     * @brief (Step 2) Initializes the global state of the function.
    */
    static void RfcShowTablesScan(ClientContext &context, 
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

    TableFunction CreateRfcShowFunctionScanFunction() 
    {
        auto fun = TableFunction("sap_rfc_show_function", {}, RfcShowTablesScan, RfcShowTablesBind);
        fun.named_parameters["FUNCNAME"] = LogicalType::VARCHAR;
        fun.named_parameters["GROUPNAME"] = LogicalType::VARCHAR;
        fun.named_parameters["LANGUAGE"] = LogicalType::VARCHAR;
        fun.projection_pushdown = false;

        return fun;
    }

} // namespace duckdb