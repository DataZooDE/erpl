#include "pragma_ping.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"
#include "telemetry.hpp"

namespace duckdb {
    string RfcPing(ClientContext &context, const FunctionParameters &parameters) 
    {
        PostHogTelemetry::Instance().CaptureFunctionExecution("sap_rfc_ping");

        auto connection = RfcAuthParams::FromContext(context).Connect();
        connection->Ping();
        
        auto pragma_query = StringUtil::Format("SELECT 'PONG' as msg");
	    return pragma_query;
    }

    PragmaFunction CreateRfcPingPragma() 
    {
        auto rfc_ping_pragma = PragmaFunction::PragmaStatement("sap_rfc_ping", RfcPing);
        return rfc_ping_pragma;
    }
} // namespace duckdb