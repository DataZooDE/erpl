#include "scanner_tunnels.hpp"
#include "tunnel_manager.hpp"
#include "telemetry.hpp"

namespace duckdb {

unique_ptr<FunctionData> ListTunnelsBind(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &return_types, vector<string> &names) {
    PostHogTelemetry::Instance().CaptureFunctionExecution("tunnels");
    
    return_types.push_back(LogicalType::BIGINT);    // tunnel_id
    return_types.push_back(LogicalType::VARCHAR);   // ssh_host
    return_types.push_back(LogicalType::INTEGER);   // ssh_port
    return_types.push_back(LogicalType::VARCHAR);   // ssh_user
    return_types.push_back(LogicalType::VARCHAR);   // remote_host
    return_types.push_back(LogicalType::INTEGER);   // remote_port
    return_types.push_back(LogicalType::INTEGER);   // local_port
    return_types.push_back(LogicalType::VARCHAR);   // status
    
    names.push_back("tunnel_id");
    names.push_back("ssh_host");
    names.push_back("ssh_port");
    names.push_back("ssh_user");
    names.push_back("remote_host");
    names.push_back("remote_port");
    names.push_back("local_port");
    names.push_back("status");
    
    // Get the list of tunnels with detailed information from the tunnel manager
    std::vector<std::pair<int64_t, TunnelConnectionAttributes>> tunnels;
    if (g_tunnel_manager) {
        tunnels = g_tunnel_manager->ListTunnelsWithDetails();
    }
    
    auto bind_data = make_uniq<TunnelsBindData>(std::move(tunnels));
    return std::move(bind_data);
}

void ListTunnelsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
    auto &bind_data = data_p.bind_data->CastNoConst<TunnelsBindData>();
    
    if (!bind_data.HasMoreResults()) {
        return;
    }

    bind_data.FetchNextResult(output);
}

TableFunction CreateTunnelsTableFunction() {
    TableFunction list_tunnels("tunnels", {}, ListTunnelsFunction, ListTunnelsBind);
    return list_tunnels;
}

} // namespace duckdb 