#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <atomic>

#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <signal.h>
#endif

#include "libssh2.h"

std::atomic<bool> g_running(true);

void signal_handler(int sig) {
    g_running = false;
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  --host HOST       SSH host (default: localhost)\n"
              << "  --port PORT       SSH port (default: 22)\n"
              << "  --user USER       SSH username (default: root)\n"
              << "  --key KEYFILE     SSH private key file\n"
              << "  --lport PORT      Local port to bind (default: 9000)\n"
              << "  --rhost HOST      Remote host to forward to\n"
              << "  --rport PORT      Remote port to forward to\n"
              << "  --help            Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << program_name << " --host 127.0.0.1 --port 2222 --user root --key ./key \\\n"
              << "      --lport 9000 --rhost service --rport 8000\n";
}

int main(int argc, char* argv[]) {
    std::string ssh_host = "localhost";
    int ssh_port = 22;
    std::string ssh_user = "root";
    std::string key_file;
    int local_port = 9000;
    std::string remote_host;
    int remote_port = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            ssh_host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            ssh_port = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            ssh_user = argv[++i];
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            key_file = argv[++i];
        } else if (strcmp(argv[i], "--lport") == 0 && i + 1 < argc) {
            local_port = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rhost") == 0 && i + 1 < argc) {
            remote_host = argv[++i];
        } else if (strcmp(argv[i], "--rport") == 0 && i + 1 < argc) {
            remote_port = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate required parameters
    if (key_file.empty()) {
        std::cerr << "Error: --key parameter is required\n";
        return 1;
    }
    if (remote_host.empty()) {
        std::cerr << "Error: --rhost parameter is required\n";
        return 1;
    }
    if (remote_port == 0) {
        std::cerr << "Error: --rport parameter is required\n";
        return 1;
    }

    std::cout << "SSH Tunnel Client\n"
              << "  SSH Host: " << ssh_host << ":" << ssh_port << "\n"
              << "  SSH User: " << ssh_user << "\n"
              << "  Key File: " << key_file << "\n"
              << "  Local Port: " << local_port << "\n"
              << "  Remote Host: " << remote_host << ":" << remote_port << "\n";

    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize libssh2
    int rc = libssh2_init(0);
    if (rc != 0) {
        std::cerr << "Failed to initialize libssh2\n";
        return 1;
    }

    // Create socket and connect to SSH server
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Failed to create socket\n";
        libssh2_exit();
        return 1;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(ssh_port);
    sin.sin_addr.s_addr = inet_addr(ssh_host.c_str());

    if (connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
        std::cerr << "Failed to connect to SSH server\n";
        close(sock);
        libssh2_exit();
        return 1;
    }

    // Create SSH session
    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        std::cerr << "Failed to create SSH session\n";
        close(sock);
        libssh2_exit();
        return 1;
    }

    // Perform SSH handshake
    if (libssh2_session_handshake(session, sock)) {
        std::cerr << "SSH handshake failed\n";
        libssh2_session_free(session);
        close(sock);
        libssh2_exit();
        return 1;
    }

    // Authenticate with public key
    if (libssh2_userauth_publickey_fromfile(session, ssh_user.c_str(), 
                                           key_file.c_str(), nullptr, nullptr)) {
        std::cerr << "SSH authentication failed\n";
        libssh2_session_free(session);
        close(sock);
        libssh2_exit();
        return 1;
    }

    std::cout << "SSH connection established successfully\n";

    // Create local listening socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        std::cerr << "Failed to create listening socket\n";
        libssh2_session_free(session);
        close(sock);
        libssh2_exit();
        return 1;
    }

    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(local_port);
    listen_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) != 0) {
        std::cerr << "Failed to bind to local port " << local_port << "\n";
        close(listen_sock);
        libssh2_session_free(session);
        close(sock);
        libssh2_exit();
        return 1;
    }

    if (listen(listen_sock, 5) != 0) {
        std::cerr << "Failed to listen on local port " << local_port << "\n";
        close(listen_sock);
        libssh2_session_free(session);
        close(sock);
        libssh2_exit();
        return 1;
    }

    std::cout << "Listening on local port " << local_port << "\n";

    // Main forwarding loop
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (g_running) {
                std::cerr << "Failed to accept client connection\n";
            }
            break;
        }

        std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr) << "\n";

        // Create SSH channel for forwarding
        LIBSSH2_CHANNEL* channel = libssh2_channel_direct_tcpip_ex(session, 
                                                                  remote_host.c_str(), 
                                                                  remote_port, 
                                                                  "127.0.0.1", 8080);
        if (!channel) {
            std::cerr << "Failed to create SSH channel\n";
            close(client_sock);
            continue;
        }

        // Handle forwarding in a separate thread
        std::thread([channel, client_sock, session]() {
            char buffer[16384];
            fd_set fds;
            struct timeval tv;
            
            // Set client socket to non-blocking
            int flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

            while (g_running) {
                FD_ZERO(&fds);
                FD_SET(client_sock, &fds);
                tv.tv_sec = 0;
                tv.tv_usec = 100000; // 100ms

                if (select(client_sock + 1, &fds, NULL, NULL, &tv) > 0) {
                    int bytes = read(client_sock, buffer, sizeof(buffer));
                    if (bytes <= 0) break;
                    
                    int written = libssh2_channel_write(channel, buffer, bytes);
                    if (written < 0) break;
                }

                int bytes_from_channel = libssh2_channel_read(channel, buffer, sizeof(buffer));
                if (bytes_from_channel > 0) {
                    write(client_sock, buffer, bytes_from_channel);
                } else if (bytes_from_channel == 0) {
                    if (libssh2_channel_eof(channel)) break;
                } else if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
                    break;
                }
            }
            
            close(client_sock);
            libssh2_channel_free(channel);
        }).detach();
    }

    std::cout << "Shutting down tunnel...\n";

    // Cleanup
    close(listen_sock);
    libssh2_session_disconnect(session, "Shutdown");
    libssh2_session_free(session);
    close(sock);
    libssh2_exit();

    std::cout << "Tunnel closed\n";
    return 0;
} 