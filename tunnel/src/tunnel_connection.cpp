#include "tunnel_connection.hpp"
#include "tunnel_secret.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
    #include <netdb.h>
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace duckdb {

TunnelConnection::TunnelConnection() {
    // Initialize Windows sockets if on Windows
#ifdef WIN32
    WSADATA wsa_data;
    const int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0) {
        throw IOException("Failed to initialize Windows sockets");
    }
#endif

    // Initialize libssh2
    const int init_result = libssh2_init(0);
    if (init_result != 0) {
        throw IOException("Failed to initialize libssh2 library");
    }
}

TunnelConnection::~TunnelConnection() {
    Close();
    libssh2_exit();

    // Cleanup Windows sockets if on Windows
#ifdef WIN32
    WSACleanup();
#endif
}

TunnelConnection::TunnelConnection(TunnelConnection&& other) noexcept
    : session_(other.session_),
      ssh_socket_(other.ssh_socket_),
      is_connected_(other.is_connected_.load()),
      is_running_(other.is_running_.load()),
      attributes_(std::move(other.attributes_)),
      worker_thread_(std::move(other.worker_thread_)) {
    other.session_ = nullptr;
    other.ssh_socket_ = -1;
    other.is_connected_.store(false);
    other.is_running_.store(false);
}

TunnelConnection& TunnelConnection::operator=(TunnelConnection&& other) noexcept {
    if (this != &other) {
        Close();
        session_ = other.session_;
        ssh_socket_ = other.ssh_socket_;
        is_connected_.store(other.is_connected_.load());
        is_running_.store(other.is_running_.load());
        attributes_ = std::move(other.attributes_);
        worker_thread_ = std::move(other.worker_thread_);
        
        other.session_ = nullptr;
        other.ssh_socket_ = -1;
        other.is_connected_.store(false);
        other.is_running_.store(false);
    }
    return *this;
}

void TunnelConnection::ValidateConnectionParameters(const std::string& ssh_host, int32_t ssh_port,
                                                   const std::string& ssh_user, const std::string& remote_host,
                                                   int32_t remote_port, int32_t local_port) const {
    if (ssh_host.empty()) {
        throw InvalidInputException("SSH host cannot be empty");
    }
    
    if (ssh_port < kMinPortNumber || ssh_port > kMaxPortNumber) {
        throw InvalidInputException("SSH port must be between " + std::to_string(kMinPortNumber) + 
                                   " and " + std::to_string(kMaxPortNumber));
    }
    
    if (ssh_user.empty()) {
        throw InvalidInputException("SSH user cannot be empty");
    }
    
    if (remote_host.empty()) {
        throw InvalidInputException("Remote host cannot be empty");
    }
    
    if (remote_port < kMinPortNumber || remote_port > kMaxPortNumber) {
        throw InvalidInputException("Remote port must be between " + std::to_string(kMinPortNumber) + 
                                   " and " + std::to_string(kMaxPortNumber));
    }
    
    if (local_port < kMinPortNumber || local_port > kMaxPortNumber) {
        throw InvalidInputException("Local port must be between " + std::to_string(kMinPortNumber) + 
                                   " and " + std::to_string(kMaxPortNumber));
    }
}

bool TunnelConnection::TestTunnelConnection(int32_t timeout_seconds) {
    const auto start_time = std::chrono::steady_clock::now();
    const auto timeout_duration = std::chrono::seconds(timeout_seconds);
    
    while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
        // Check if the tunnel worker is running and has started listening
        if (!is_running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Try to connect to the local port
        const int32_t test_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (test_socket < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Set socket to non-blocking for timeout
#ifdef WIN32
        u_long non_blocking = 1;
        ioctlsocket(test_socket, FIONBIO, &non_blocking);
#else
        const int flags = fcntl(test_socket, F_GETFL, 0);
        fcntl(test_socket, F_SETFL, flags | O_NONBLOCK);
#endif
        
        sockaddr_in test_addr{};
        test_addr.sin_family = AF_INET;
        test_addr.sin_port = htons(attributes_.local_port);
        test_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        
        const int connect_result = connect(test_socket, reinterpret_cast<struct sockaddr*>(&test_addr), sizeof(test_addr));
        if (connect_result == 0) {
            // Connection successful immediately
            CloseSocket(test_socket);
            return true;
        }
        
        if (errno == EINPROGRESS) {
            // Connection in progress, wait for it to complete
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(test_socket, &write_fds);
            
            timeval tv{};
            tv.tv_sec = 1;  // 1 second timeout for each attempt
            tv.tv_usec = 0;
            
            const int select_result = select(test_socket + 1, nullptr, &write_fds, nullptr, &tv);
            if (select_result > 0) {
            // Check if connection was successful
#ifdef WIN32
            char error = 0;
            int len = sizeof(error);
            if (getsockopt(test_socket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
#else
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(test_socket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
#endif
                    CloseSocket(test_socket);
                    return true;
                }
            }
        }
        
        CloseSocket(test_socket);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return false;
}

void TunnelConnection::Connect(const std::string& ssh_host, int32_t ssh_port, const std::string& ssh_user,
                              const std::string& remote_host, int32_t remote_port, int32_t local_port) {
    // Validate input parameters
    ValidateConnectionParameters(ssh_host, ssh_port, ssh_user, remote_host, remote_port, local_port);
    
    // Store connection attributes
    attributes_.ssh_host = ssh_host;
    attributes_.ssh_port = ssh_port;
    attributes_.ssh_user = ssh_user;
    attributes_.remote_host = remote_host;
    attributes_.remote_port = remote_port;
    attributes_.local_port = local_port;
    
    // Create socket and connect to SSH server
    ssh_socket_ = CreateSocket();
    if (ssh_socket_ < 0) {
        attributes_.error_message = "Failed to create socket for SSH connection";
        throw IOException("Failed to create socket for SSH connection to " + ssh_host);
    }
    
    // Use IPv4 for connection
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(ssh_port);
    
    // Convert hostname to IP address
#ifdef WIN32
    const unsigned long addr = inet_addr(ssh_host.c_str());
#else
    const in_addr_t addr = inet_addr(ssh_host.c_str());
#endif
    if (addr == INADDR_NONE) {
        // Try to resolve hostname
        struct hostent* he = gethostbyname(ssh_host.c_str());
        if (he == nullptr) {
            attributes_.error_message = "Failed to resolve hostname: " + ssh_host;
            CloseSocket(ssh_socket_);
            throw IOException("Failed to resolve hostname '" + ssh_host + "'. Check if the host is reachable.");
        }
#ifdef WIN32
        sin.sin_addr.s_addr = *reinterpret_cast<unsigned long*>(he->h_addr_list[0]);
#else
        sin.sin_addr.s_addr = *reinterpret_cast<in_addr_t*>(he->h_addr_list[0]);
#endif
    } else {
        sin.sin_addr.s_addr = addr;
    }
    
    if (connect(ssh_socket_, reinterpret_cast<struct sockaddr*>(&sin), sizeof(struct sockaddr_in)) != 0) {
        attributes_.error_message = "Failed to connect to SSH host: " + ssh_host + ":" + std::to_string(ssh_port);
        CloseSocket(ssh_socket_);
        throw IOException("Failed to connect to SSH host '" + ssh_host + ":" + std::to_string(ssh_port) + 
                         "'. Check if the SSH server is running and accessible.");
    }
    
    // Initialize SSH session
    session_ = libssh2_session_init();
    if (!session_) {
        attributes_.error_message = "Failed to initialize SSH session";
        CloseSocket(ssh_socket_);
        throw IOException("Failed to initialize SSH session. libssh2 may not be properly installed.");
    }
    
    // Perform SSH handshake
    const int handshake_result = libssh2_session_handshake(session_, ssh_socket_);
    if (handshake_result != 0) {
        attributes_.error_message = "SSH handshake failed";
        CloseSocket(ssh_socket_);
        throw IOException("SSH handshake failed. The server may not support SSH or the connection was interrupted.");
    }
    
    // Authentication will be handled by the calling code
    is_connected_ = true;
    attributes_.status = kStatusConnected;
}

void TunnelConnection::StartWorker() {
    is_running_ = true;
    worker_thread_ = std::thread([this]() {
        WorkerFunction();
    });
}

void TunnelConnection::StopWorker() {
    is_running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void TunnelConnection::Close() {
    is_running_ = false;
    
    // Close SSH session first to unblock any pending operations
    if (session_) {
        // Set a timeout for session operations
        libssh2_session_set_timeout(session_, 5000); // 5 second timeout
        
        // Disconnect the session
        libssh2_session_disconnect(session_, "Shutdown");
        libssh2_session_free(session_);
        session_ = nullptr;
    }
    
    // Close the SSH socket
    if (ssh_socket_ >= 0) {
        CloseSocket(ssh_socket_);
        ssh_socket_ = -1;
    }
    
    // Try to join the worker thread with a timeout
    if (worker_thread_.joinable()) {
        // Use a timeout approach to avoid blocking indefinitely
        const auto start_time = std::chrono::steady_clock::now();
        const auto timeout_duration = std::chrono::seconds(3); // 3 second timeout
        
        while (worker_thread_.joinable() && 
               std::chrono::steady_clock::now() - start_time < timeout_duration) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // If the thread is still joinable after timeout, detach it
        if (worker_thread_.joinable()) {
            worker_thread_.detach();
        }
    }
    
    is_connected_ = false;
    attributes_.status = kStatusClosed;
}

bool TunnelConnection::IsConnected() const noexcept {
    return is_connected_ && is_running_;
}

TunnelConnectionAttributes TunnelConnection::GetAttributes() const noexcept {
    return attributes_;
}

std::string TunnelConnection::GetStatus() const noexcept {
    return attributes_.status;
}

std::string TunnelConnection::GetErrorMessage() const noexcept {
    return attributes_.error_message;
}

TunnelAuthParams TunnelAuthParams::FromContext(ClientContext& context, const std::string& secret_name) {
    TunnelAuthParams auth_params;
    
    // Try to get the secret from the context
    auto& secret_manager = SecretManager::Get(context);
    auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
    
    // Lookup the secret
    auto secret_match = secret_manager.LookupSecret(transaction, secret_name, TUNNEL_SECRET_TYPE_NAME);
    if (!secret_match.HasMatch()) {
        // If secret is not found, return default auth params instead of throwing
        // This allows the function to be called without a secret configured
        auth_params.ssh_host = "localhost";
        auth_params.ssh_port = kDefaultSshPort;
        auth_params.ssh_user = "";
        auth_params.password = "";
        auth_params.private_key_path = "";
        auth_params.passphrase = "";
        auth_params.auth_method = kAuthMethodAgent; // Default to SSH agent
        return auth_params;
    }
    
    // Cast to KeyValueSecret
    const auto& duck_secret = dynamic_cast<const KeyValueSecret&>(secret_match.GetSecret());
    auth_params = ConvertTunnelSecretToAuthParams(duck_secret);
    
    return auth_params;
}

std::string TunnelAuthParams::ToString() const {
    return "TunnelAuthParams{ssh_host='" + ssh_host + "', ssh_port=" + std::to_string(ssh_port) + 
           ", ssh_user='" + ssh_user + "', auth_method='" + auth_method + "'}";
}

std::shared_ptr<TunnelConnection> TunnelAuthParams::Connect(const std::string& remote_host, int32_t remote_port, int32_t local_port) {
    auto connection = std::make_shared<TunnelConnection>();
    connection->Connect(ssh_host, ssh_port, ssh_user, remote_host, remote_port, local_port);
    
    // Authenticate based on the auth method
    bool auth_success = false;
    if (auth_method == kAuthMethodPassword && !password.empty()) {
        auth_success = connection->AuthenticateWithPassword(password);
    } else if (auth_method == kAuthMethodKey && !private_key_path.empty()) {
        auth_success = connection->AuthenticateWithKey(private_key_path, passphrase);
    } else if (auth_method == kAuthMethodAgent) {
        auth_success = connection->AuthenticateWithAgent();
    }
    
    if (!auth_success) {
        throw IOException("Failed to authenticate tunnel connection: " + connection->GetErrorMessage());
    }
    
    return connection;
}

bool TunnelConnection::AuthenticateWithPassword(const std::string& password) {
    if (!session_) {
        attributes_.error_message = "No SSH session available for authentication";
        return false;
    }
    
    const int auth_result = libssh2_userauth_password(session_, attributes_.ssh_user.c_str(), password.c_str());
    if (auth_result != 0) {
        attributes_.error_message = "SSH password authentication failed. Check username and password.";
        return false;
    }
    
    attributes_.status = kStatusAuthenticated;
    return true;
}

bool TunnelConnection::AuthenticateWithKey(const std::string& private_key_path, const std::string& passphrase) {
    if (!session_) {
        attributes_.error_message = "No SSH session available for key authentication";
        return false;
    }
    
    // Read private key file
    std::ifstream key_file(private_key_path);
    if (!key_file.is_open()) {
        attributes_.error_message = "Failed to open private key file: " + private_key_path;
        return false;
    }
    
    std::stringstream key_buffer;
    key_buffer << key_file.rdbuf();
    const std::string private_key = key_buffer.str();
    
    int auth_result;
    if (passphrase.empty()) {
        auth_result = libssh2_userauth_publickey_frommemory(session_, attributes_.ssh_user.c_str(), 
                                                           attributes_.ssh_user.length(),
                                                           nullptr, 0,
                                                           private_key.c_str(), private_key.length(),
                                                           nullptr);
    } else {
        auth_result = libssh2_userauth_publickey_frommemory(session_, attributes_.ssh_user.c_str(), 
                                                           attributes_.ssh_user.length(),
                                                           nullptr, 0,
                                                           private_key.c_str(), private_key.length(),
                                                           passphrase.c_str());
    }
    
    if (auth_result != 0) {
        attributes_.error_message = "SSH key authentication failed. Check if the private key is valid and the passphrase is correct.";
        return false;
    }
    
    attributes_.status = kStatusAuthenticated;
    return true;
}

bool TunnelConnection::AuthenticateWithAgent() {
    if (!session_) {
        attributes_.error_message = "No SSH session available for agent authentication";
        return false;
    }
    
    // Note: libssh2_userauth_agent is not available in all libssh2 versions
    // For now, we'll use a fallback approach
    attributes_.error_message = "SSH agent authentication not supported in this libssh2 version";
    return false;
}

void TunnelConnection::WorkerFunction() {
    attributes_.status = "Starting tunnel on port " + std::to_string(attributes_.local_port);
    
    // Create listening socket
    const int32_t listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        attributes_.error_message = "Failed to create listening socket for tunnel";
        attributes_.status = kStatusError;
        return;
    }
    
    // Set socket to non-blocking to allow for graceful shutdown
#ifdef WIN32
    u_long non_blocking = 1;
    ioctlsocket(listen_sock, FIONBIO, &non_blocking);
#else
    const int flags = fcntl(listen_sock, F_GETFL, 0);
    fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK);
#endif
    
    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(attributes_.local_port);
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(listen_sock, reinterpret_cast<struct sockaddr*>(&listen_addr), sizeof(listen_addr)) != 0) {
        attributes_.error_message = "Failed to bind to port " + std::to_string(attributes_.local_port) + 
                                   ". Port may be in use or insufficient permissions.";
        attributes_.status = kStatusError;
        CloseSocket(listen_sock);
        return;
    }
    
    if (listen(listen_sock, 5) != 0) {
        attributes_.error_message = "Failed to listen on port " + std::to_string(attributes_.local_port);
        attributes_.status = kStatusError;
        CloseSocket(listen_sock);
        return;
    }
    
    attributes_.status = kStatusListening + std::string(" on port ") + std::to_string(attributes_.local_port);
    
    while (is_running_) {
        // Use select with timeout to make accept non-blocking
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout
        
        const int select_result = select(listen_sock + 1, &read_fds, nullptr, nullptr, &tv);
        if (select_result < 0) {
            // Error in select
            break;
        } else if (select_result == 0) {
            // Timeout - continue loop to check is_running_
            continue;
        }
        
        // Accept connection
        const int32_t client_sock = accept(listen_sock, nullptr, nullptr);
        if (client_sock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        
        if (!is_running_) {
            CloseSocket(client_sock);
            break;
        }
        
        // Create SSH channel for port forwarding
        LIBSSH2_CHANNEL* channel = libssh2_channel_direct_tcpip_ex(session_, 
                                                                  attributes_.remote_host.c_str(), 
                                                                  attributes_.remote_port, 
                                                                  "127.0.0.1", attributes_.remote_port);
        if (!channel) {
            attributes_.error_message = "Failed to create SSH channel for port forwarding";
            CloseSocket(client_sock);
            continue;
        }
        
        // Handle data forwarding in a separate thread
        std::thread([this, channel, client_sock]() {
            ForwardData(client_sock, channel);
        }).detach();
    }
    
    CloseSocket(listen_sock);
    attributes_.status = "Tunnel stopped";
}

void TunnelConnection::ForwardData(int32_t client_socket, LIBSSH2_CHANNEL* channel) {
    static constexpr size_t kBufferSize = 16384;
    char buffer[kBufferSize];
    fd_set fds;
    timeval tv;
    
    // Set client socket to non-blocking
#ifdef WIN32
    u_long non_blocking = 1;
    ioctlsocket(client_socket, FIONBIO, &non_blocking);
#else
    const int client_flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, client_flags | O_NONBLOCK);
#endif
    
    while (is_running_) {
        FD_ZERO(&fds);
        FD_SET(client_socket, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms timeout for faster response to shutdown
        
        if (select(client_socket + 1, &fds, nullptr, nullptr, &tv) > 0) {
            const ssize_t bytes = read(client_socket, buffer, sizeof(buffer));
            if (bytes <= 0) break;
            
            size_t written = 0;
            while (written < static_cast<size_t>(bytes) && is_running_) {
                const ssize_t rc = libssh2_channel_write(channel, buffer + written, bytes - written);
                if (rc < 0) {
                    if (libssh2_session_last_errno(session_) != LIBSSH2_ERROR_EAGAIN) {
                        goto cleanup;
                    }
                    break;
                }
                written += rc;
            }
        }
        
        // Read from SSH channel
        const ssize_t bytes_from_channel = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (bytes_from_channel > 0) {
            size_t written = 0;
            while (written < static_cast<size_t>(bytes_from_channel) && is_running_) {
                const ssize_t rc = write(client_socket, buffer + written, bytes_from_channel - written);
                if (rc < 0) {
                    goto cleanup;
                }
                written += rc;
            }
        } else if (bytes_from_channel == 0) {
            if (libssh2_channel_eof(channel)) break;
        } else if (libssh2_session_last_errno(session_) != LIBSSH2_ERROR_EAGAIN) {
            break;
        }
    }
    
cleanup:
    CloseSocket(client_socket);
    libssh2_channel_free(channel);
}

int32_t TunnelConnection::CreateSocket() {
    return socket(AF_INET, SOCK_STREAM, 0);
}

void TunnelConnection::CloseSocket(int32_t socket) noexcept {
    if (socket >= 0) {
        close(socket);
    }
}

} // namespace duckdb 