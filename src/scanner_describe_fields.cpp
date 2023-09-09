#include "duckdb.hpp"
#include "scanner_show_tables.hpp"

namespace duckdb 
{
    static string RfcDescribeFieldsToString(const FunctionData *bind_data_p) {
        D_ASSERT(bind_data_p);
        return StringUtil::Format("Unclear what to do here");
    }

    static std::vector<Value> CreateFunctionArguments(TableFunctionBindInput &input) {
        child_list_t<Value> args;


        args.push_back(make_pair("TABNAME", input.inputs[0])) ;
     
        auto &named_params = input.named_parameters;
           
        if (named_params.find("LANGUAGE") != named_params.end()) {
            args.push_back(make_pair("LANGU", named_params["LANGUAGE"])) ;
        } else {
            args.push_back(make_pair("LANGU", Value::CreateValue("E"))) ;
        }

        return std::vector<Value>( { Value::STRUCT(args) });
    }

    static unique_ptr<FunctionData> RfcDescribeFieldsBind(ClientContext &context, 
                                                          TableFunctionBindInput &input, 
                                                          vector<LogicalType> &return_types, 
                                                          vector<string> &names) 
    {
        // Connect to the SAP system
        auto connection = RfcAuthParams::FromContext(context).Connect();

        // Create the function
        auto func = make_shared<RfcFunction>(connection, "DDIF_FIELDINFO_GET");
        auto func_args = CreateFunctionArguments(input);
        auto invocation = func->BeginInvocation(func_args);
        auto result_set = invocation->Invoke("/DFIES_TAB");
        names = result_set->GetResultNames();
        return_types = result_set->GetResultTypes();

        auto bind_data = make_uniq<RfcFunctionBindData>();
        bind_data->invocation = invocation;
        bind_data->result_set = result_set;

        return std::move(bind_data);
    }

    static void RfcDescribeFieldsScan(ClientContext &context, 
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

    CreateTableFunctionInfo CreateRfcDescribeFieldsScanFunction() 
    {
        auto fun = TableFunction("sap_describe_fields", {}, RfcDescribeFieldsScan, RfcDescribeFieldsBind);
        fun.to_string = RfcDescribeFieldsToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }
} // namespace duckdb