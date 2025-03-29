#include "duckdb.hpp"
#include "scanner_show_tables.hpp"
#include "telemetry.hpp"

namespace duckdb 
{
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

    TableFunction CreateRfcDescribeReferencessScanFunction() 
    {
        auto fun = TableFunction("sap_describe_references", {}, RfcDescribeReferencesScan, RfcDescribeReferencesBind);
        fun.projection_pushdown = false;

        return fun;
    }
} // namespace duckdb