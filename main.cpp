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
    #define POLL_FN WSAPoll
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
    inline void init_network() {}
    inline void cleanup_network() {}
    #define CLOSE_SOCKET close
    #define POLL_FN poll
#endif

#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include "Cache.hpp"

bool send_all (int sock,const char* data, size_t len){
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, data + sent, len - sent, 0);// slice data 
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true; 
}
size_t parse_content_length(const std::string& headers_block) {
    std::istringstream iss(headers_block);
    std::string line;
    std::getline(iss, line);
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (to_lower(line.substr(0, colon)) == "content-length") {
            try {
                return static_cast<size_t>(std::stoull(line.substr(colon + 1)));
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}
std::string read_request(int client_socket) {
    std::string raw;
    char buf[4096];
    size_t hdr_end = std::string::npos;

    while ((hdr_end = raw.find("\r\n\r\n")) == std::string::npos) {
        ssize_t n = recv(client_socket, buf, sizeof(buf), 0);
        if (n <= 0) return raw;
        raw.append(buf, static_cast<size_t>(n));
        if (raw.size() > 65536) return raw;
    }

    size_t content_len = parse_content_length(raw.substr(0, hdr_end));
    size_t total_expected = hdr_end + 4 + content_len;
    while (raw.size() < total_expected && raw.size() < 16 * 1024 * 1024) {
        ssize_t n = recv(client_socket, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
    }

    return raw;
}

std::string force_connection_close(const std::string& raw_request) {
    size_t hdr_end = raw_request.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return raw_request;

    std::string head = raw_request.substr(0, hdr_end);
    std::string body = raw_request.substr(hdr_end + 4);

    std::istringstream iss(head);
    std::ostringstream out;
    std::string line;
    if (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out << line << "\r\n";
    }
    
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = to_lower(line.substr(0, colon));
            if (key == "connection" || key == "proxy-connection") {
                continue;
            }
        }
        out << line << "\r\n";
    }
    out << "Connection: close\r\n\r\n" << body;
    return out.str();
}


bool parse_host_and_port(const std::string& raw_request, std::string& method,
                         std::string& host, int& port, std::string& cache_key_host) 
{
    std::istringstream iss(raw_request);
    std::string target, version;
    if (!(iss >> method >> target >> version)) {
        return false;
    }  
    
    if (method == "CONNECT") {
        auto colon = target.rfind(':');
        if (colon != std::string::npos) {
            host = target.substr(0, colon);
            try {
                port = std::stoi(target.substr(colon + 1));
            } catch (...) {
                port = 443;
            }
        }
        else {
            host = target;
            port = 443;
        }
        cache_key_host = target;
        return !host.empty();
    }
    std::string line;
    std::getline(iss, line);
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (to_lower(line.substr(0, colon)) == "host") {
            std::string val = line.substr(colon + 1);
            size_t start = val.find_first_not_of(" \t");
            if (start != std::string::npos) val = val.substr(start);

            auto port_colon = val.rfind(':');
            if (port_colon != std::string::npos) {
                host = val.substr(0, port_colon);
                try {
                    port = std::stoi(val.substr(port_colon + 1));
                } catch (...) {
                    port = 80;
                }
            } else {
                host = val;
                port = 80;
            }
            break;
        }
    }

    cache_key_host = host + target;
    return !host.empty();
}

 bool resolve_and_connect(const std::string& host, int port, int& out_sock, uint32_t& out_ipv4) {
    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
     struct addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
        return false;
    }

    int sock = -1;
    for (auto* p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            auto* addr_in = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
            out_ipv4 = ntohl(addr_in->sin_addr.s_addr);
            out_sock = sock;
            freeaddrinfo(res);
            return true;
        }
        CLOSE_SOCKET(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    return false;
}

void tunnel_bidirectional(int client_socket, int upstream_sock) {
    struct pollfd fds[2];
    fds[0].fd = client_socket;
    fds[0].events = POLLIN;
    fds[1].fd = upstream_sock;
    fds[1].events = POLLIN;

    char buf[8192];
    while (true) {
        int ready = POLL_FN(fds, 2, 60000);
        if (ready <= 0) break;

        for (int i = 0; i < 2; ++i) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                return;
            }
            if (fds[i].revents & POLLIN) {
                int dst = (i == 0) ? upstream_sock : client_socket;
                ssize_t n = recv(fds[i].fd, buf, sizeof(buf), 0);
                if (n <= 0 || !send_all(dst, buf, static_cast<size_t>(n))) {
                    return;
                }
            }
        }
    }
}

std::string build_http_response(const HTTP_cache& entry) {
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n";
    for (const auto& h : entry.header) {
        oss << h.key << ": " << h.value << "\r\n";
    }
    oss << "\r\n";
    std::string res = oss.str();
    res.append(entry.body.begin(), entry.body.end());
    return res;
}


void handle_client(int client_socket, Cache& cache) {
    std::string raw_request = read_request(client_socket);
    if (raw_request.empty()) {
        CLOSE_SOCKET(client_socket);
        return;
    }

    std::string method, host, cache_key_host;
    int port = 80;
    if (!parse_host_and_port(raw_request, method, host, port, cache_key_host)) {
        const char* bad_req = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send_all(client_socket, bad_req, std::strlen(bad_req));
        CLOSE_SOCKET(client_socket);
        return;
    }

    int upstream_sock = -1; 
    uint32_t target_ip = 0;
    if (!resolve_and_connect(host, port, upstream_sock, target_ip)) {
        const char* bad_gw = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
        send_all(client_socket, bad_gw, std::strlen(bad_gw));
        CLOSE_SOCKET(client_socket);
        return;
    }

    // Handle HTTPS CONNECT tunneling
    if (method == "CONNECT") {
        const char* established = "HTTP/1.1 200 Connection Established\r\n\r\n";
        if (send_all(client_socket, established, std::strlen(established))) {
            tunnel_bidirectional(client_socket, upstream_sock);
        }
        CLOSE_SOCKET(upstream_sock);
        CLOSE_SOCKET(client_socket);
        return;
    }

    // 1. Check in-memory queue (and disk log on miss) for GET requests
    if (method == "GET") {
        if (auto hit = cache.Find_in_Queue(target_ip, cache_key_host)) {
            CLOSE_SOCKET(upstream_sock);
            std::string response = build_http_response(*hit);
            send_all(client_socket, response.data(), response.size());
            CLOSE_SOCKET(client_socket);
            return;
        }
        if (auto log_hit = cache.Find_in_Log(target_ip, cache_key_host)) {
            CLOSE_SOCKET(upstream_sock);
            cache.add_to_queue(*log_hit);
            std::string response = build_http_response(*log_hit);
            send_all(client_socket, response.data(), response.size());
            CLOSE_SOCKET(client_socket);
            return;
        }
    }

    // 2. Cache miss: forward request with Connection: close
    std::string outbound_request = force_connection_close(raw_request);
    if (!send_all(upstream_sock, outbound_request.data(), outbound_request.size())) {
        CLOSE_SOCKET(upstream_sock);
        CLOSE_SOCKET(client_socket);
        return;
    }

    std::string raw_response;
    char resp_buf[4096];
    ssize_t n;
    while ((n = recv(upstream_sock, resp_buf, sizeof(resp_buf), 0)) > 0) {
        raw_response.append(resp_buf, static_cast<size_t>(n));
        send_all(client_socket, resp_buf, static_cast<size_t>(n));
    }
    CLOSE_SOCKET(upstream_sock);
    CLOSE_SOCKET(client_socket);

    // 3. Cache 200 OK GET responses if Cache-Control allows it
    if (method == "GET" && raw_response.rfind("HTTP/1.", 0) == 0 &&
        raw_response.find(" 200 ") != std::string::npos) {
        size_t hdr_end = raw_response.find("\r\n\r\n");
        if (hdr_end != std::string::npos) {
            HTTP_cache entry;
            entry.IPv4 = target_ip;
            entry.host = cache_key_host;

            std::istringstream hstream(raw_response.substr(0, hdr_end));
            std::string status_line;
            std::getline(hstream, status_line);

            std::vector<HeaderEntry> parsed_headers;
            std::string hline;
            while (std::getline(hstream, hline)) {
                if (!hline.empty() && hline.back() == '\r') hline.pop_back();
                auto colon = hline.find(':');
                if (colon == std::string::npos) continue;
                std::string k = hline.substr(0, colon);
                std::string v = hline.substr(colon + 1);
                size_t first = v.find_first_not_of(" \t");
                if (first != std::string::npos) v = v.substr(first);
                parsed_headers.emplace_back(std::move(k), std::move(v));
            }
            entry.add_header(parsed_headers);

            std::time_t ttl = 300;
            if (entry.is_cacheable(ttl, 300)) {
                const char* body_ptr = raw_response.data() + hdr_end + 4;
                size_t body_len = raw_response.size() - (hdr_end + 4);
                entry.update_body(std::vector<char>(body_ptr, body_ptr + body_len));
                entry.update_TTL(ttl);

                cache.add_to_queue(entry);
                cache.write_to_log(entry);
            }
        }
    }
}

int main(){

    init_network();
    #ifndef _WIN32
    std::signal(SIGPIPE,SIG_IGN );
    #endif
    int proxy = socket(2,SOCK_STREAM,0);
    if (proxy <0){
        cleanup_network();
        return 1;
    }
    int opt = 1;
    setsockopt(proxy, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    if (bind (proxy,reinterpret_cast<struct sockaddr*>(&address), sizeof(address))<0){

          CLOSE_SOCKET(proxy);
        cleanup_network();
        return 1;
    }


    if (listen(proxy, 128) < 0) {
        CLOSE_SOCKET(proxy);
        cleanup_network();
        return 1;
    }

    Cache cache;
    std::cout << "HTTP Proxy running on port 8080...\n";

    while (true) {
        int client_socket = accept(proxy, nullptr, nullptr);
        if (client_socket < 0) continue;
        std::thread(handle_client, client_socket, std::ref(cache)).detach();
    }

    CLOSE_SOCKET(proxy);
    cleanup_network();
    return 0;

}