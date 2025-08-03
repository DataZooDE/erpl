#include "pragma_tunnel_close.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"
#include "tunnel_manager.hpp"
#include "telemetry.hpp"

namespace duckdb {

string TunnelClose(ClientContext &context, const FunctionParameters &parameters) {
    PostHogTelemetry::Instance().CaptureFunctionExecution("erpl_tunnel_close");
    
    // Extract tunnel_id from positional parameters
    int64_t tunnel_id = parameters.values[0].GetValue<int64_t>(); 
    
    // Close tunnel using the tunnel manager
    bool success = g_tunnel_manager->CloseTunnel(tunnel_id);
    
    if (success) {
        auto pragma_query = StringUtil::Format("SELECT %lld as tunnel_id, 'Tunnel closed successfully' as message", tunnel_id);
        return pragma_query;
    } else {
        auto pragma_query = StringUtil::Format("SELECT %lld as tunnel_id, 'Tunnel not found or already closed' as message", tunnel_id);
        return pragma_query;
    }
}

PragmaFunction CreateTunnelClosePragma() {
    auto tunnel_close_pragma = PragmaFunction::PragmaCall("erpl_tunnel_close", TunnelClose, { LogicalType::INTEGER});
    return tunnel_close_pragma;
}

} // namespace duckdb 