#include "scanner_show_tables.hpp"

namespace duckdb 
{
    static string RfcShowTablesToString(const FunctionData *bind_data_p) {
        D_ASSERT(bind_data_p);
        //auto bind_data = (const SapRfcCallBindData *)bind_data_p;
        return StringUtil::Format("Unclear what to do here");
    }

    static unique_ptr<FunctionData> RfcShowTablesBind(ClientContext &context, 
                                                      TableFunctionBindInput &input, 
                                                      vector<LogicalType> &return_types, 
                                                      vector<string> &names) 
    {
        auto bind_data = make_uniq<RfcFunctionBindData>();
        return std::move(bind_data);
    }

    static void RfcShowTablesScan(ClientContext &context, 
                                      TableFunctionInput &data, 
                                      DataChunk &output) 
    {

    }

    CreateTableFunctionInfo CreateRfcShowTablesScanFunction() 
    {
        auto fun = TableFunction("sap_show_tables", {}, RfcShowTablesScan, RfcShowTablesBind);
        fun.named_parameters["FUNCNAME"] = LogicalType::VARCHAR;
        fun.named_parameters["GROUPNAME"] = LogicalType::VARCHAR;
        fun.named_parameters["LANGUAGE"] = LogicalType::VARCHAR;
        fun.to_string = RfcShowTablesToString;
        fun.projection_pushdown = false;

        return CreateTableFunctionInfo(fun);
    }

} // namespace duckdb