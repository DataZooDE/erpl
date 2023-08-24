#include "scanner_show_tables.hpp"
#include "duckdb_argument_helper.hpp"

namespace duckdb 
{
    static string RfcShowTablesToString(const FunctionData *bind_data_p) 
    {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static std::vector<Value> CreateFunctionArguments(TableFunctionBindInput &input) 
    {
        auto &named_params = input.named_parameters;
        auto table_name = named_params.find("TABLENAME") != named_params.end() 
                            ? named_params["TABLENAME"].ToString() : "%";
    
        auto pred1 = StringUtil::Format("TABNAME LIKE '%s' AND ( TABCLASS = 'VIEW' OR TABCLASS = 'TRANSP' OR", table_name);
        auto pred2 = StringUtil::Format("TABCLASS = 'POOL' OR TABCLASS = 'CLUSTER' ) AND DDTEXT LIKE '%s'", table_name);

        

        auto args = ArgBuilder()
            .Add("QUERY_TABLE", Value("DD02V"))
            .Add("ROWSKIPS", Value::CreateValue(0))
            .Add("ROWCOUNT", Value::CreateValue(1000))
            .Add("OPTIONS", {
                ArgBuilder().Add("TEXT", Value(pred1)).Build(),
                ArgBuilder().Add("TEXT", Value(pred2)).Build()
            })
            .Add("FIELDS", {
                ArgBuilder().Add("FIELDNAME", Value("TABNAME")).Build(),
                ArgBuilder().Add("FIELDNAME", Value("DDTEXT")).Build(),
                ArgBuilder().Add("FIELDNAME", Value("DDLANGUAGE")).Build(),
                ArgBuilder().Add("FIELDNAME", Value("TABCLASS")).Build(),
            });

        return args.BuildArgList();
    }

    static unique_ptr<FunctionData> RfcShowTablesBind(ClientContext &context, 
                                                      TableFunctionBindInput &input, 
                                                      vector<LogicalType> &return_types, 
                                                      vector<string> &names) 
    {
        // Connect to the SAP system
        auto connection = RfcAuthParams::FromContext(context).Connect();

        // Create the function
        auto func = make_shared<RfcFunction>(connection, "RFC_READ_TABLE");
        auto func_args = CreateFunctionArguments(input);
        auto func_args_debug = func_args.front().ToSQLString();
        auto invocation = func->BeginInvocation(func_args);
        auto result_set = invocation->Invoke("/DATA");
        names = result_set->GetResultNames();
        return_types = result_set->GetResultTypes();

        auto bind_data = make_uniq<RfcFunctionBindData>();
        bind_data->invocation = invocation;
        bind_data->result_set = result_set;

        return std::move(bind_data);
    }

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

    CreateTableFunctionInfo CreateRfcShowTablesScanFunction() 
    {
        auto fun = TableFunction("sap_show_tables", { }, RfcShowTablesScan, RfcShowTablesBind);
        fun.named_parameters["TABLENAME"] = LogicalType::VARCHAR;
        fun.to_string = RfcShowTablesToString;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb