#include "duckdb.hpp"
#include "scanner_show_tables.hpp"

namespace duckdb 
{
    static string RfcDescribeFieldsToString(const FunctionData *bind_data_p) {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static unique_ptr<FunctionData> RfcDescribeFieldsBind(ClientContext &context, 
                                                          TableFunctionBindInput &input, 
                                                          vector<LogicalType> &return_types, 
                                                          vector<string> &names) 
    {
        auto bind_data = make_uniq<RfcFunctionBindData>();
        return std::move(bind_data);
    }

    static void RfcDescribeFieldsScan(ClientContext &context, 
                                      TableFunctionInput &data, 
                                      DataChunk &output) 
    {

    }

    CreateTableFunctionInfo CreateRfcDescribeFieldsScanFunction() 
    {
        auto fun = TableFunction("sap_describe_fields", {}, RfcDescribeFieldsScan, RfcDescribeFieldsBind);
        fun.to_string = RfcDescribeFieldsToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }
} // namespace duckdb