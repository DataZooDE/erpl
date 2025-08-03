#pragma once

#include "duckdb.hpp"
#include "tunnel_connection.hpp"
#include <unordered_map>
#include <mutex>
#include <memory>

namespace duckdb {

/**
 * @brief Manages multiple SSH tunnel connections.
 * 
 * This class provides thread-safe management of multiple tunnel connections,
 * including creation, monitoring, and cleanup of tunnels.
 */
class TunnelManager {
public:
    TunnelManager();
    ~TunnelManager();

    // Tunnel management
    int64_t CreateTunnel(const TunnelAuthParams &auth_params, 
                         const string &remote_host, int remote_port, int local_port,
                         const string &ssh_host, int ssh_port,
                         const string &ssh_user,
                         int timeout_seconds = 60);
    bool CloseTunnel(int64_t tunnel_id);
    bool IsTunnelActive(int64_t tunnel_id) const;
    
    // Tunnel information
    std::vector<std::pair<int64_t, string>> ListTunnels() const;
    std::vector<std::pair<int64_t, TunnelConnectionAttributes>> ListTunnelsWithDetails() const;
    std::string GetTunnelStatus(int64_t tunnel_id) const;
    std::string GetTunnelError(int64_t tunnel_id) const;
    
    // Cleanup
    void CloseAllTunnels();
    void CleanupInactiveTunnels();

private:
    mutable std::mutex tunnels_mutex;
    std::unordered_map<int64_t, std::shared_ptr<TunnelConnection>> active_tunnels;
    int64_t next_tunnel_id;
    
    // Helper methods
    int64_t GenerateTunnelId();
    void RemoveTunnel(int64_t tunnel_id);
};

// Global tunnel manager instance (defined in tunnel_extension.cpp)
extern std::unique_ptr<TunnelManager> g_tunnel_manager;

} // namespace duckdb 