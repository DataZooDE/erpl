#include "scanner_describe_function.hpp"
#include "telemetry.hpp"

namespace duckdb 
{

    DescribeFunctionResult::DescribeFunctionResult(std::shared_ptr<RfcFunction> function)
        : _function(function), _has_more_results(true)
    { }

    std::vector<std::string> DescribeFunctionResult::GetResultNames() 
    {
        std::vector<std::string> names;
        names.push_back("NAME");
        names.push_back("IMPORT");
        names.push_back("EXPORT");
        names.push_back("CHANGING");
        names.push_back("TABLES");
        return names;
    }

    std::vector<LogicalType> DescribeFunctionResult::GetResultTypes() 
    {
        std::vector<LogicalType> types;
        types.push_back(LogicalType::VARCHAR); // FUNCNAME
        
        auto param_struct = GetParamterResultType();
        types.push_back(LogicalType::LIST(param_struct)); // IMPORT
        types.push_back(LogicalType::LIST(param_struct)); // EXPORT
        types.push_back(LogicalType::LIST(param_struct)); // CHANGING
        types.push_back(LogicalType::LIST(param_struct)); // TABLES

        return types;
    }

    LogicalType DescribeFunctionResult::GetParamterResultType() 
    {
        auto param_struct = LogicalType::STRUCT({ 
            { "NAME", LogicalType::VARCHAR },
            { "TYPE", LogicalType::VARCHAR },
            { "DIRECTION", LogicalType::VARCHAR},
            { "LENGTH", LogicalType::INTEGER },
            { "DECIMALS", LogicalType::INTEGER },
            { "DEFAULTVALUE", LogicalType::VARCHAR },
            { "PARAMETERTEXT", LogicalType::VARCHAR },
            { "OPTIONAL", LogicalType::BOOLEAN },
        });
        return param_struct;
    }

    bool DescribeFunctionResult::HasMoreResults() 
    {
        return _has_more_results;
    }

    void DescribeFunctionResult::FetchNextResult(DataChunk &output) 
    {
        auto param_infos = _function->GetParameterInfos();
        std::vector<RfcFunctionParameterDesc> sel_infos;
        

        output.SetValue(0, 0, Value(_function->GetName()));

        // IMPORT
         sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_IMPORT; });
        output.SetValue(1, 0, ConvertParameterInfos(sel_infos));

        // EXPORT
        sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_EXPORT; });
        output.SetValue(2, 0, ConvertParameterInfos(sel_infos));

        // CHANGING
         sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_CHANGING; });
        output.SetValue(3, 0, ConvertParameterInfos(sel_infos));

        // TABLES
         sel_infos.clear();
        std::copy_if(param_infos.begin(), param_infos.end(), std::back_inserter(sel_infos), 
                      [](RfcFunctionParameterDesc &x) { return x.GetDirection() == RFC_TABLES; });
        output.SetValue(4, 0, ConvertParameterInfos(sel_infos));

        _has_more_results = false;
        output.SetCardinality(1);
    }

    Value DescribeFunctionResult::ConvertParameterInfos(std::vector<RfcFunctionParameterDesc> &param_infos)
    {
        const auto param_type = GetParamterResultType();
        
        vector<Value> param_values;
        std::transform(param_infos.begin(), param_infos.end(), std::back_inserter(param_values), 
                       [](RfcFunctionParameterDesc &x) {
                           child_list_t<Value> values;
                           values.push_back(make_pair("NAME", Value(x.GetName())));
                           values.push_back(make_pair("TYPE", Value(x.GetRfcTypeAsString())));
                           values.push_back(make_pair("DIRECTION", Value(x.GetDirectionAsString())));
                           values.push_back(make_pair("LENGTH", Value::CreateValue(x.GetLength())));
                           values.push_back(make_pair("DECIMALS", Value::CreateValue(x.GetDecimals())));
                           values.push_back(make_pair("DEFAULTVALUE", Value(x.GetDefaultValue())));
                           values.push_back(make_pair("PARAMETERTEXT", Value(x.GetDescription())));
                           values.push_back(make_pair("OPTIONAL", Value(x.IsOptional())));                 
                           return Value::STRUCT(values);
                       });
                       
        return Value::LIST(param_type, param_values);
    }

    // DescribeFunctionResult ----------------------------------------------------

    static string RfcDescribeFunctionToString(const FunctionData *bind_data_p) {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    /**
     * @brief (Step 1) Binds the input arguments to the function.
    */
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
        auto func_name = inputs[0].GetValue<string>();
        auto func = make_shared<RfcFunction>(connection, func_name);
        auto result = make_shared<DescribeFunctionResult>(func);
        names = result->GetResultNames();
        return_types = result->GetResultTypes();

        auto bind_data = make_uniq<RfcDescribeFunctionBindData>();
        bind_data->result = result;

        return std::move(bind_data);
    }

    /**
     * @brief (Step 2) Initializes the global state of the function.
    */
    static void RfcDescribeFunctionScan(ClientContext &context, 
                                       TableFunctionInput &data, 
                                       DataChunk &output) 
    {
        auto &bind_data = data.bind_data->Cast<RfcDescribeFunctionBindData>();
        auto result = bind_data.result;

        if (! result->HasMoreResults()) {
            return;
        }

        result->FetchNextResult(output);
    }

    CreateTableFunctionInfo CreateRfcDescribeFunctionScanFunction() 
    {
        auto fun = TableFunction("sap_rfc_describe_function", {LogicalType::VARCHAR}, RfcDescribeFunctionScan, RfcDescribeFunctionBind);
        fun.named_parameters["LANGUAGE"] = LogicalType::VARCHAR;
        fun.to_string = RfcDescribeFunctionToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb