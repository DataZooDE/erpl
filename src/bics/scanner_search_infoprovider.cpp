#include "duckdb.hpp"
#include "scanner_search_infoprovider.hpp"

#include "duckdb_argument_helper.hpp"

namespace duckdb 
{
    static string BicsSearchInfoProviderToString(const FunctionData *bind_data_p) 
    {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static string ConvertObjectType(TableFunctionBindInput &input) 
    {
        auto &named_params = input.named_parameters;
        if (HasParam(named_params, "OBJ_TYPE") && named_params["OBJ_TYPE"].ToString() == "CUBE") {
            return "IPRO";
        }

        return "QU";
    }

    static std::vector<Value> CreateFunctionArguments(TableFunctionBindInput &input) 
    {
        auto &named_params = input.named_parameters;
        auto args = ArgBuilder()
            .Add("I_S_PARAMS", ArgBuilder()
                .Add("OBJ_TYPE", Value::CreateValue(ConvertObjectType(input)))
                .Add("OBJ_VIEW", Value::CreateValue("SE"))
                .Add("DISP_MODE", Value::CreateValue("0"))
            )
            .Add("I_S_SEARCH_PARAMS", ArgBuilder()
                .Add("SEARCH_STRING", HasParam(named_params, "SEARCH") ? named_params["SEARCH"] : Value::CreateValue("*"))
                .Add("SEARCH_IN_KEY", ConvertBoolArgument(named_params, "SEARCH_IN_KEY", true))
                .Add("SEARCH_IN_TEXT", ConvertBoolArgument(named_params, "SEARCH_IN_TEXT", true))
            );

        return std::vector<Value>({ args.Build() });
    }


    static unique_ptr<FunctionData> BicsSearchInfoProviderBind(ClientContext &context, 
                                                               TableFunctionBindInput &input, 
                                                               vector<LogicalType> &return_types, 
                                                               vector<string> &names) 
    {
        // Connect to the SAP system
        auto connection = RfcAuthParams::FromContext(context).Connect();

        // Create the function
        auto func = make_shared<RfcFunction>(connection, "RSOBJS_GET_NODES");
        auto func_args = CreateFunctionArguments(input);
        auto invocation = func->BeginInvocation(func_args);
        auto result_set = invocation->Invoke("/E_T_NODES");
        names = result_set->GetResultNames();
        return_types = result_set->GetResultTypes();

        auto bind_data = make_uniq<RfcFunctionBindData>();
        bind_data->invocation = invocation;
        bind_data->result_set = result_set;

        return std::move(bind_data);
    }

    static void BicsSearchInfoProviderScan(ClientContext &context, 
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

    CreateTableFunctionInfo CreateBicsSearchInfoProviderScanFunction() 
    {
        auto fun = TableFunction("sap_bics_search_infoprovider", {}, BicsSearchInfoProviderScan, BicsSearchInfoProviderBind);
        fun.to_string = BicsSearchInfoProviderToString;
        
        auto obj_type_enum = Vector(LogicalType::VARCHAR, 2);
        obj_type_enum.SetValue(0, Value("QUERY"));
        obj_type_enum.SetValue(1, Value("CUBE"));
        fun.named_parameters["OBJ_TYPE"] = LogicalType::ENUM("OBJ_TYPE", obj_type_enum, 2);
        fun.named_parameters["SEARCH"] = LogicalType::VARCHAR;
        fun.named_parameters["SEARCH_IN_KEY"] = LogicalType::BOOLEAN;
        fun.named_parameters["SEARCH_IN_TEXT"] = LogicalType::BOOLEAN;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb