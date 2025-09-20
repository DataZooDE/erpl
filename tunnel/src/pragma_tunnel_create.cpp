#include "pragma_tunnel_create.hpp"
#include "duckdb/parser/parsed_data/create_pragma_function_info.hpp"
#include "tunnel_manager.hpp"
#include "tunnel_secret.hpp"
#include "telemetry.hpp"

namespace duckdb {

string TunnelCreate(ClientContext &context, const FunctionParameters &parameters) {
    PostHogTelemetry::Instance().CaptureFunctionExecution("tunnel_create");
    
    // Get authentication parameters from the secret
    auto auth_params = GetTunnelAuthParamsFromContext(context, parameters);
    
    // Extract tunnel parameters from named parameters
    string remote_host;
    int remote_port = 0;
    int local_port = 0;
    int timeout_seconds = 60; // Default timeout
    
    for (const auto &param : parameters.named_parameters) {
        if (param.first == "remote_host") {
            remote_host = param.second.ToString();
        } else if (param.first == "remote_port") {
            remote_port = param.second.GetValue<int32_t>();
        } else if (param.first == "local_port") {
            local_port = param.second.GetValue<int32_t>();
        } else if (param.first == "timeout") {
            timeout_seconds = param.second.GetValue<int32_t>();
        }
    }
    
    if (remote_host.empty() || remote_port == 0 || local_port == 0) {
        throw InvalidInputException("tunnel_create requires remote_host, remote_port, and local_port parameters");
    }
    
    // Create tunnel using the tunnel manager with timeout
    int64_t tunnel_id = g_tunnel_manager->CreateTunnel(auth_params, remote_host, remote_port, local_port, 
                                                      auth_params.ssh_host, auth_params.ssh_port, auth_params.ssh_user, 
                                                      timeout_seconds);
    
    auto pragma_query = StringUtil::Format("SELECT %lld as tunnel_id, 'Tunnel created successfully' as message", tunnel_id);
    return pragma_query;
}

PragmaFunction CreateTunnelCreatePragma() {
    auto tunnel_create_pragma = PragmaFunction::PragmaCall("tunnel_create", TunnelCreate, {});
    tunnel_create_pragma.named_parameters["secret"] = LogicalType::VARCHAR;
    tunnel_create_pragma.named_parameters["remote_host"] = LogicalType::VARCHAR;
    tunnel_create_pragma.named_parameters["remote_port"] = LogicalType::INTEGER;
    tunnel_create_pragma.named_parameters["local_port"] = LogicalType::INTEGER;
    tunnel_create_pragma.named_parameters["timeout"] = LogicalType::INTEGER;

    return tunnel_create_pragma;
}

} // namespace duckdb 