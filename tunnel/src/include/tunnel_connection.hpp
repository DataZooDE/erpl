#pragma once

#include "duckdb.hpp"
#include "libssh2.h"

#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace duckdb {

// Constants for default values
static constexpr int32_t kDefaultSshPort = 22;
static constexpr int32_t kDefaultTimeoutSeconds = 60;
static constexpr int32_t kMaxPortNumber = 65535;
static constexpr int32_t kMinPortNumber = 1;

// Authentication method constants
static constexpr const char* kAuthMethodPassword = "password";
static constexpr const char* kAuthMethodKey = "key";
static constexpr const char* kAuthMethodAgent = "agent";

// Status constants
static constexpr const char* kStatusConnected = "Connected to SSH server";
static constexpr const char* kStatusAuthenticated = "Authenticated";
static constexpr const char* kStatusListening = "Listening";
static constexpr const char* kStatusClosed = "Closed";
static constexpr const char* kStatusError = "Error";

/**
 * @brief Attributes describing a tunnel connection.
 * 
 * This struct contains all the configuration and status information
 * for an SSH tunnel connection.
 */
struct TunnelConnectionAttributes {
    std::string ssh_host;
    int32_t ssh_port{kDefaultSshPort};
    std::string ssh_user;
    std::string remote_host;
    int32_t remote_port{0};
    int32_t local_port{0};
    std::string status;
    std::string error_message;

    bool operator==(const TunnelConnectionAttributes& other) const {
        return ssh_host == other.ssh_host &&
               ssh_port == other.ssh_port &&
               ssh_user == other.ssh_user &&
               remote_host == other.remote_host &&
               remote_port == other.remote_port &&
               local_port == other.local_port &&
               status == other.status &&
               error_message == other.error_message;
    }

    bool operator!=(const TunnelConnectionAttributes& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Manages SSH tunnel connections using libssh2.
 * 
 * This class provides a thread-safe wrapper around libssh2 for creating
 * and managing SSH tunnel connections. It handles authentication,
 * connection establishment, data forwarding, and cleanup.
 * 
 * Thread Safety: This class is not thread-safe. External synchronization
 * is required when accessing from multiple threads.
 */
class TunnelConnection {
public:
    TunnelConnection();
    
    // Disable copy constructor and assignment operator
    TunnelConnection(const TunnelConnection&) = delete;
    TunnelConnection& operator=(const TunnelConnection&) = delete;
    
    // Allow move constructor and assignment
    TunnelConnection(TunnelConnection&&) noexcept;
    TunnelConnection& operator=(TunnelConnection&&) noexcept;
    
    ~TunnelConnection();

    // Connection management
    void Connect(const std::string& ssh_host, int32_t ssh_port, const std::string& ssh_user,
                 const std::string& remote_host, int32_t remote_port, int32_t local_port);
    void Close();
    bool IsConnected() const noexcept;
    
    // Status and attributes
    TunnelConnectionAttributes GetAttributes() const noexcept;
    std::string GetStatus() const noexcept;
    std::string GetErrorMessage() const noexcept;

    // Authentication methods
    bool AuthenticateWithPassword(const std::string& password);
    bool AuthenticateWithKey(const std::string& private_key_path, const std::string& passphrase = "");
    bool AuthenticateWithAgent();
    
    // Thread management
    void StartWorker();
    void StopWorker();
    bool IsRunning() const noexcept { return is_running_.load(); }
    
    // Tunnel testing
    bool TestTunnelConnection(int32_t timeout_seconds = kDefaultTimeoutSeconds);

private:
    // Worker thread function
    void WorkerFunction();
    
    // Internal methods
    void ForwardData(int32_t client_socket, LIBSSH2_CHANNEL* channel);
    
    // Socket helpers
    int32_t CreateSocket();
    void CloseSocket(int32_t socket) noexcept;
    
    // Validation helpers
    void ValidateConnectionParameters(const std::string& ssh_host, int32_t ssh_port,
                                    const std::string& ssh_user, const std::string& remote_host,
                                    int32_t remote_port, int32_t local_port) const;
    
    // SSH connection state
    LIBSSH2_SESSION* session_{nullptr};
    int32_t ssh_socket_{-1};
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> is_running_{false};
    
    // Connection attributes
    TunnelConnectionAttributes attributes_;
    
    // Worker thread for data forwarding
    std::thread worker_thread_;
};

/**
 * @brief Authentication parameters for tunnel connections.
 * 
 * This struct contains all the authentication information needed
 * to establish an SSH tunnel connection.
 */
struct TunnelAuthParams {
    std::string ssh_host;
    int32_t ssh_port{kDefaultSshPort};
    std::string ssh_user;
    std::string password;
    std::string private_key_path;
    std::string passphrase;
    std::string auth_method{kAuthMethodPassword};  // "password", "key", "agent"

    static TunnelAuthParams FromContext(ClientContext& context, const std::string& secret_name = "*");
    std::string ToString() const;
    std::shared_ptr<TunnelConnection> Connect(const std::string& remote_host, int32_t remote_port, int32_t local_port);
};

} // namespace duckdb 