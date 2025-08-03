#include "tunnel_manager.hpp"
#include "tunnel_connection.hpp"
#include "tunnel_secret.hpp"
#include "duckdb/common/exception.hpp"
#include <thread>
#include <chrono>

namespace duckdb {



TunnelManager::TunnelManager() : next_tunnel_id(1) {
}

TunnelManager::~TunnelManager() {
    CloseAllTunnels();
}

int64_t TunnelManager::CreateTunnel(const TunnelAuthParams &auth_params, 
                                   const string &remote_host, int remote_port, int local_port,
                                   const string &ssh_host, int ssh_port,
                                   const string &ssh_user,
                                   int timeout_seconds) {
    int64_t tunnel_id = GenerateTunnelId();
    
    try {
        // Create tunnel connection
        auto connection = std::make_shared<TunnelConnection>();
        
        // Connect to SSH server
        connection->Connect(ssh_host, ssh_port, ssh_user,
                           remote_host, remote_port, local_port);
        
        // Authenticate based on auth method
        bool auth_success = false;
        if (auth_params.auth_method == "password") {
            if (!auth_params.password.empty()) {
                auth_success = connection->AuthenticateWithPassword(auth_params.password);
            }
        } else if (auth_params.auth_method == "key") {
            if (!auth_params.private_key_path.empty()) {
                auth_success = connection->AuthenticateWithKey(auth_params.private_key_path, auth_params.passphrase);
            }
        } else if (auth_params.auth_method == "agent") {
            auth_success = connection->AuthenticateWithAgent();
        }
        
        if (!auth_success) {
            throw IOException("SSH authentication failed: " + connection->GetErrorMessage());
        }
        
        // Start the tunnel worker thread
        connection->StartWorker();
        
        // Give the worker thread a moment to start up
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Wait for the tunnel to be actually ready by testing the connection
        if (!connection->TestTunnelConnection(timeout_seconds)) {
            connection->Close();
            throw IOException("Tunnel creation timed out after " + std::to_string(timeout_seconds) + " seconds. Tunnel may not be ready.");
        }
        
        // Store the connection (acquire mutex only for this operation)
        {
            std::lock_guard<std::mutex> lock(tunnels_mutex);
            active_tunnels[tunnel_id] = connection;
        }
        
        return tunnel_id;
        
    } catch (const std::exception &e) {
        throw IOException("Failed to create tunnel: " + string(e.what()));
    }
}

bool TunnelManager::CloseTunnel(int64_t tunnel_id) {
    std::shared_ptr<TunnelConnection> connection_to_close;
    
    {
        std::lock_guard<std::mutex> lock(tunnels_mutex);
        
        auto it = active_tunnels.find(tunnel_id);
        if (it == active_tunnels.end()) {
            // Don't throw an exception, just return false to indicate tunnel wasn't found
            return false;
        }
        
        connection_to_close = it->second;
        
        // Remove from active tunnels first to prevent other operations
        active_tunnels.erase(it);
    }
    
    // Close the connection outside of the lock to avoid blocking other operations
    try {
        connection_to_close->Close();
    } catch (const std::exception &e) {
        // Log the error but don't re-throw to avoid blocking the caller
        // The tunnel is already removed from active_tunnels
    }
    
    return true;
}

bool TunnelManager::IsTunnelActive(int64_t tunnel_id) const {
    std::lock_guard<std::mutex> lock(tunnels_mutex);
    
    auto it = active_tunnels.find(tunnel_id);
    if (it == active_tunnels.end()) {
        return false;
    }
    
    return it->second->IsConnected();
}

std::vector<std::pair<int64_t, string>> TunnelManager::ListTunnels() const {
    // Try to acquire the mutex with a timeout
    std::unique_lock<std::mutex> lock(tunnels_mutex, std::try_to_lock);
    
    if (!lock.owns_lock()) {
        // If we can't acquire the mutex, return empty result
        return {};
    }
    
    std::vector<std::pair<int64_t, string>> result;
    for (const auto &pair : active_tunnels) {
        // Get the actual status from the tunnel connection
        result.emplace_back(pair.first, pair.second->GetStatus());
    }
    
    return result;
}

std::vector<std::pair<int64_t, TunnelConnectionAttributes>> TunnelManager::ListTunnelsWithDetails() const {
    // Try to acquire the mutex with a timeout
    std::unique_lock<std::mutex> lock(tunnels_mutex, std::try_to_lock);
    
    if (!lock.owns_lock()) {
        // If we can't acquire the mutex, return empty result
        return {};
    }
    
    std::vector<std::pair<int64_t, TunnelConnectionAttributes>> result;
    for (const auto &pair : active_tunnels) {
        // Get the detailed attributes from the tunnel connection
        result.emplace_back(pair.first, pair.second->GetAttributes());
    }
    
    return result;
}

std::string TunnelManager::GetTunnelStatus(int64_t tunnel_id) const {
    std::lock_guard<std::mutex> lock(tunnels_mutex);
    
    auto it = active_tunnels.find(tunnel_id);
    if (it == active_tunnels.end()) {
        return "Not found";
    }
    
    return it->second->GetStatus();
}

std::string TunnelManager::GetTunnelError(int64_t tunnel_id) const {
    std::lock_guard<std::mutex> lock(tunnels_mutex);
    
    auto it = active_tunnels.find(tunnel_id);
    if (it == active_tunnels.end()) {
        return "Tunnel not found";
    }
    
    return it->second->GetErrorMessage();
}

void TunnelManager::CloseAllTunnels() {
    std::lock_guard<std::mutex> lock(tunnels_mutex);
    
    for (auto &pair : active_tunnels) {
        pair.second->Close();
    }
    active_tunnels.clear();
}

void TunnelManager::CleanupInactiveTunnels() {
    std::lock_guard<std::mutex> lock(tunnels_mutex);
    
    auto it = active_tunnels.begin();
    while (it != active_tunnels.end()) {
        if (!it->second->IsConnected()) {
            it->second->Close();
            it = active_tunnels.erase(it);
        } else {
            ++it;
        }
    }
}

int64_t TunnelManager::GenerateTunnelId() {
    return next_tunnel_id++;
}

void TunnelManager::RemoveTunnel(int64_t tunnel_id) {
    auto it = active_tunnels.find(tunnel_id);
    if (it != active_tunnels.end()) {
        it->second->Close();
        active_tunnels.erase(it);
    }
}



} // namespace duckdb 