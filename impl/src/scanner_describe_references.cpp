#include "duckdb.hpp"
#include "scanner_show_tables.hpp"
#include "telemetry.hpp"

namespace duckdb 
{
    static string RfcDescribeReferencesToString(const FunctionData *bind_data_p) {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static unique_ptr<FunctionData> RfcDescribeReferencesBind(ClientContext &context, 
                                                              TableFunctionBindInput &input, 
                                                              vector<LogicalType> &return_types, 
                                                              vector<string> &names) 
    {
        PostHogTelemetry::Instance().CaptureFunctionExecution("sap_describe_references");

        auto bind_data = make_uniq<RfcFunctionBindData>();
        return std::move(bind_data);
    }

    static void RfcDescribeReferencesScan(ClientContext &context, 
                                          TableFunctionInput &data, 
                                          DataChunk &output) 
    {

    }

    CreateTableFunctionInfo CreateRfcDescribeReferencessScanFunction() 
    {
        auto fun = TableFunction("sap_describe_references", {}, RfcDescribeReferencesScan, RfcDescribeReferencesBind);
        fun.to_string = RfcDescribeReferencesToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }
} // namespace duckdb