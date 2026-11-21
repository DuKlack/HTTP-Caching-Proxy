#pragma once

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    inline void init_network() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    inline void cleanup_network() { WSACleanup(); }
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    inline void init_network() {}
    inline void cleanup_network() {}
    #define CLOSE_SOCKET close
#endif

#include <iostream>
#include <thread>
#include "Cache.hpp"

void handle_client(int client_socket) {
    char buffer[4096];
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        std::string raw_request(buffer);

        // 1. Parse request / extract host to query Cache
        // 2. If cached, send response to client_socket via send()
        // 3. If cache miss, open connection to destination server, fetch, cache, and reply
    }
    CLOSE_SOCKET(client_socket);
}

int main() {
    init_network();
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    std::cout << "HTTP Proxy running on port 8080...\n";

    while (true) {
        int client_socket = accept(server_fd, nullptr, nullptr);
        if (client_socket < 0) continue;

        // Spawn a detached or managed thread for concurrent client processing
        std::thread(handle_client, client_socket).detach();
    }

    CLOSE_SOCKET(server_fd);
    cleanup_network();
    return 0;
}