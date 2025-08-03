#include "pragma_tunnel_close_all.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"
#include "tunnel_manager.hpp"
#include "telemetry.hpp"

namespace duckdb {

string TunnelCloseAll(ClientContext &context, const FunctionParameters &parameters) {
    PostHogTelemetry::Instance().CaptureFunctionExecution("erpl_tunnel_close_all");
    
    // Close all tunnels using the tunnel manager
    g_tunnel_manager->CloseAllTunnels();
    
    auto pragma_query = "SELECT 'All tunnels closed successfully' as message";
    return pragma_query;
}

PragmaFunction CreateTunnelCloseAllPragma() {
    auto tunnel_close_all_pragma = PragmaFunction::PragmaCall("erpl_tunnel_close_all", TunnelCloseAll, {});
    
    return tunnel_close_all_pragma;
}

} // namespace duckdb 