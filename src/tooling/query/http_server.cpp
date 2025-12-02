/**
 * @file http_server.cpp
 * @brief Minimal single-threaded HTTP/1.1 server implementation
 */

#include "http_server.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

// Platform-specific includes
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
using ssize_t = ptrdiff_t; // Windows doesn't define ssize_t
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define SOCKET_ERROR_VAL SOCKET_ERROR
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET_VAL (-1)
#define SOCKET_ERROR_VAL (-1)
#define close_socket close
#endif

namespace ascii_query {

// =============================================================================
// HttpRequest
// =============================================================================

std::string HttpRequest::header(const std::string &name, const std::string &default_value) const {
    // Case-insensitive header lookup
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto &[key, value] : headers) {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower_key == lower_name) {
            return value;
        }
    }
    return default_value;
}

// =============================================================================
// HttpResponse
// =============================================================================

std::string HttpResponse::serialize() const {
    std::ostringstream oss;

    // Status line
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";

    // Headers
    bool has_content_length = false;
    bool has_connection = false;

    for (const auto &[key, value] : headers) {
        oss << key << ": " << value << "\r\n";
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower_key == "content-length")
            has_content_length = true;
        if (lower_key == "connection")
            has_connection = true;
    }

    // Add Content-Length if not present
    if (!has_content_length) {
        oss << "Content-Length: " << body.size() << "\r\n";
    }

    // Add Connection: close if not present
    if (!has_connection) {
        oss << "Connection: close\r\n";
    }

    // CORS headers for browser access
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
    oss << "Access-Control-Allow-Headers: Content-Type\r\n";

    // End headers
    oss << "\r\n";

    // Body
    oss << body;

    return oss.str();
}

// =============================================================================
// HttpServer
// =============================================================================

HttpServer::HttpServer() {
#if defined(_WIN32)
    // Initialize Winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    // Default 404 handler
    default_handler_ = [](const HttpRequest &) { return HttpResponse::notFound(); };
}

HttpServer::~HttpServer() {
    stop();

#if defined(_WIN32)
    WSACleanup();
#endif
}

void HttpServer::addRoute(const std::string &method, const std::string &path, RouteHandler handler) {
    routes_.push_back({method, path, std::move(handler)});
}

void HttpServer::setDefaultHandler(RouteHandler handler) { default_handler_ = std::move(handler); }

bool HttpServer::start(uint16_t port) {
    if (running_) {
        last_error_ = "Server already running";
        return false;
    }

    // Create socket
    server_socket_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (server_socket_ == INVALID_SOCKET_VAL) {
        last_error_ = "Failed to create socket";
        return false;
    }

    // Set SO_REUSEADDR
    int opt = 1;
#if defined(_WIN32)
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));
#else
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Bind
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only for security
    addr.sin_port = htons(port);

    if (bind(server_socket_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        last_error_ = "Failed to bind to port " + std::to_string(port);
        close_socket(server_socket_);
        server_socket_ = -1;
        return false;
    }

    // Listen
    if (listen(server_socket_, 10) < 0) {
        last_error_ = "Failed to listen on socket";
        close_socket(server_socket_);
        server_socket_ = -1;
        return false;
    }

    port_ = port;
    running_ = true;

    // Start server thread
    server_thread_ = std::thread(&HttpServer::serverLoop, this);

    return true;
}

void HttpServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // Close server socket to unblock accept()
    if (server_socket_ >= 0) {
#if defined(_WIN32)
        shutdown(server_socket_, SD_BOTH);
#else
        shutdown(server_socket_, SHUT_RDWR);
#endif
        close_socket(server_socket_);
        server_socket_ = -1;
    }

    // Wait for server thread
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::serverLoop() {
    while (running_) {
        struct sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);

        socket_t client_socket = accept(server_socket_, reinterpret_cast<struct sockaddr *>(&client_addr), &client_len);

        if (client_socket == INVALID_SOCKET_VAL) {
            if (running_) {
                // Real error, not just shutdown
                // Continue anyway
            }
            continue;
        }

        handleClient(static_cast<int>(client_socket));
    }
}

bool HttpServer::processOneRequest() {
    if (server_socket_ < 0) {
        return false;
    }

    struct sockaddr_in client_addr {};
    socklen_t client_len = sizeof(client_addr);

    socket_t client_socket = accept(server_socket_, reinterpret_cast<struct sockaddr *>(&client_addr), &client_len);

    if (client_socket == INVALID_SOCKET_VAL) {
        return false;
    }

    handleClient(static_cast<int>(client_socket));
    return true;
}

void HttpServer::handleClient(int client_socket) {
    // Read request (up to 64KB)
    char buffer[65536];
    ssize_t bytes_read;

#if defined(_WIN32)
    bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
#else
    bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
#endif

    if (bytes_read <= 0) {
        close_socket(client_socket);
        return;
    }
    buffer[bytes_read] = '\0';

    // Parse request
    HttpRequest request;
    if (!parseRequest(buffer, request)) {
        HttpResponse response = HttpResponse::badRequest("Invalid HTTP request");
        std::string raw = response.serialize();
        send(client_socket, raw.c_str(), static_cast<int>(raw.size()), 0);
        close_socket(client_socket);
        return;
    }

    // Handle CORS preflight
    if (request.method == "OPTIONS") {
        HttpResponse response = HttpResponse::noContent();
        std::string raw = response.serialize();
        send(client_socket, raw.c_str(), static_cast<int>(raw.size()), 0);
        close_socket(client_socket);
        return;
    }

    // Find handler
    RouteHandler handler = findHandler(request.method, request.path);

    // Call handler
    HttpResponse response;
    try {
        response = handler(request);
    } catch (const std::exception &e) {
        response = HttpResponse::serverError(e.what());
    } catch (...) {
        response = HttpResponse::serverError("Unknown error");
    }

    // Send response
    std::string raw = response.serialize();
    send(client_socket, raw.c_str(), static_cast<int>(raw.size()), 0);

    close_socket(client_socket);
}

bool HttpServer::parseRequest(const std::string &raw, HttpRequest &request) {
    // Find end of request line
    size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        return false;
    }

    // Parse request line: METHOD PATH HTTP/1.x
    std::string request_line = raw.substr(0, line_end);
    std::istringstream iss(request_line);
    std::string http_version;
    std::string full_path;

    if (!(iss >> request.method >> full_path >> http_version)) {
        return false;
    }

    // Split path and query string
    size_t query_pos = full_path.find('?');
    if (query_pos != std::string::npos) {
        request.path = full_path.substr(0, query_pos);
        request.query_string = full_path.substr(query_pos + 1);
        parseQueryString(request.query_string, request.params);
    } else {
        request.path = full_path;
    }

    // Parse headers
    size_t pos = line_end + 2;
    while (pos < raw.size()) {
        size_t next_line = raw.find("\r\n", pos);
        if (next_line == std::string::npos) {
            break;
        }

        std::string header_line = raw.substr(pos, next_line - pos);
        if (header_line.empty()) {
            // Empty line = end of headers
            pos = next_line + 2;
            break;
        }

        size_t colon = header_line.find(':');
        if (colon != std::string::npos) {
            std::string name = header_line.substr(0, colon);
            std::string value = header_line.substr(colon + 1);

            // Trim leading whitespace from value
            size_t value_start = value.find_first_not_of(" \t");
            if (value_start != std::string::npos) {
                value = value.substr(value_start);
            }

            request.headers[name] = value;
        }

        pos = next_line + 2;
    }

    // Body is everything after headers
    if (pos < raw.size()) {
        request.body = raw.substr(pos);
    }

    return true;
}

void HttpServer::parseQueryString(const std::string &query, std::unordered_map<std::string, std::string> &params) {
    size_t pos = 0;
    while (pos < query.size()) {
        // Find next parameter
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) {
            amp = query.size();
        }

        std::string param = query.substr(pos, amp - pos);

        // Split key=value
        size_t eq = param.find('=');
        if (eq != std::string::npos) {
            std::string key = urlDecode(param.substr(0, eq));
            std::string value = urlDecode(param.substr(eq + 1));
            params[key] = value;
        } else {
            // Flag parameter (no value)
            std::string key = urlDecode(param);
            if (!key.empty()) {
                params[key] = "";
            }
        }

        pos = amp + 1;
    }
}

std::string HttpServer::urlDecode(const std::string &str) {
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int val = 0;
            char hex[3] = {str[i + 1], str[i + 2], '\0'};
            if (sscanf(hex, "%2x", &val) == 1) {
                result += static_cast<char>(val);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }

    return result;
}

RouteHandler HttpServer::findHandler(const std::string &method, const std::string &path) {
    for (const auto &route : routes_) {
        if (route.method == method && route.path == path) {
            return route.handler;
        }
    }
    return default_handler_;
}

} // namespace ascii_query
