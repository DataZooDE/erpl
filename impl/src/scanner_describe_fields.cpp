#include "duckdb.hpp"
#include "scanner_describe_fields.hpp"
#include "telemetry.hpp"

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
        PostHogTelemetry::Instance().CaptureFunctionExecution("sap_describe_fields");

        // Connect to the SAP system
        auto connection = RfcAuthParams::FromContext(context).Connect();

        // Create the function
        auto func = make_shared<RfcFunction>(connection, "DDIF_FIELDINFO_GET");
        auto func_args = CreateFunctionArguments(input);
        auto invocation = func->BeginInvocation(func_args);
        auto result_set = invocation->Invoke("/DFIES_TAB");
        
        // Create the bind data
        std::vector<std::tuple<std::string, std::string>> selected_fields = {
            {"POSITION",    "pos"},
            {"KEYFLAG",     "is_key"},
            {"FIELDNAME",   "field"}, 
            {"FIELDTEXT",   "text"}, 
            {"DATATYPE",    "sap_type"}, 
            {"LENG",        "length"}, 
            {"DECIMALS",    "decimals"},
            {"CHECKTABLE",  "check_table"}, 
            {"REFTABLE",    "ref_table"}, 
            {"REFFIELD",    "ref_field"},
            {"LANGU",       "language"}
        };
        auto bind_data = make_uniq<RfcFunctionBindData>(selected_fields);
        bind_data->invocation = invocation;
        bind_data->result_set = result_set;
        names = bind_data->GetResultNames();
        return_types = bind_data->GetResultTypes();

        return std::move(bind_data);
    }

    static void RfcDescribeFieldsScan(ClientContext &context, 
                                      TableFunctionInput &data, 
                                      DataChunk &output) 
    {
        auto &bind_data = data.bind_data->CastNoConst<RfcFunctionBindData>();
        if (! bind_data.HasMoreResults()) {
            return;
        }

        bind_data.FetchNextResult(output);
    }

    CreateTableFunctionInfo CreateRfcDescribeFieldsScanFunction() 
    {
        auto fun = TableFunction("sap_describe_fields", {LogicalType::VARCHAR}, RfcDescribeFieldsScan, RfcDescribeFieldsBind);
        fun.to_string = RfcDescribeFieldsToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }
} // namespace duckdb