#include "pragma_ping.hpp"

namespace duckdb {
    string RfcPing(ClientContext &context, const FunctionParameters &parameters) 
    {
        auto connection = RfcAuthParams::FromContext(context).Connect();
        connection->Ping();
        
        auto pragma_query = StringUtil::Format("SELECT 'PONG' as msg");
	    return pragma_query;
    }
} // namespace duckdb