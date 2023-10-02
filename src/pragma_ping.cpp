#include "pragma_ping.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"

namespace duckdb {
    string RfcPing(ClientContext &context, const FunctionParameters &parameters) 
    {
        auto connection = RfcAuthParams::FromContext(context).Connect();
        connection->Ping();
        
        auto pragma_query = StringUtil::Format("SELECT 'PONG' as msg");
	    return pragma_query;
    }

    CreatePragmaFunctionInfo CreateRfcPingPragma() 
    {
        auto rfc_ping_pragma = PragmaFunction::PragmaStatement("sap_rfc_ping", RfcPing);
        CreatePragmaFunctionInfo rfc_ping_info(rfc_ping_pragma);
        return rfc_ping_info;
    }
} // namespace duckdb