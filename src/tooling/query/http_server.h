/**
 * @file http_server.h
 * @brief Minimal single-threaded HTTP/1.1 server
 *
 * This provides a simple HTTP server for the query tool. It's designed to be:
 * - Single-threaded (request handlers block)
 * - Synchronous (one request at a time)
 * - Minimal (no external dependencies)
 *
 * Usage:
 *   HttpServer server;
 *   server.addRoute("GET", "/", [](const HttpRequest& req) {
 *       return HttpResponse::ok("text/html", "<h1>Hello</h1>");
 *   });
 *   server.start(9999);
 *   // ... later ...
 *   server.stop();
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "json.h"

namespace ascii_query {

/**
 * @brief Parsed HTTP request
 */
struct HttpRequest {
  std::string method;                                   ///< GET, POST, etc.
  std::string path;                                     ///< Request path (without query string)
  std::string query_string;                             ///< Query string (after ?)
  std::unordered_map<std::string, std::string> headers; ///< Request headers
  std::unordered_map<std::string, std::string> params;  ///< Parsed query parameters
  std::string body;                                     ///< Request body

  /**
   * @brief Get a query parameter value
   * @param name Parameter name
   * @param default_value Default if not found
   * @return Parameter value or default
   */
  [[nodiscard]] std::string param(const std::string &name, const std::string &default_value = "") const {
    auto it = params.find(name);
    return (it != params.end()) ? it->second : default_value;
  }

  /**
   * @brief Check if a query parameter exists (flag-style, like &break)
   * @param name Parameter name
   * @return true if parameter is present
   */
  [[nodiscard]] bool hasParam(const std::string &name) const {
    return params.find(name) != params.end();
  }

  /**
   * @brief Get a query parameter as integer
   * @param name Parameter name
   * @param default_value Default if not found or not a number
   * @return Parameter value or default
   */
  [[nodiscard]] int paramInt(const std::string &name, int default_value = 0) const {
    auto it = params.find(name);
    if (it == params.end())
      return default_value;
    try {
      return std::stoi(it->second);
    } catch (...) {
      return default_value;
    }
  }

  /**
   * @brief Get a header value
   * @param name Header name (case-insensitive)
   * @param default_value Default if not found
   * @return Header value or default
   */
  [[nodiscard]] std::string header(const std::string &name, const std::string &default_value = "") const;
};

/**
 * @brief HTTP response builder
 */
struct HttpResponse {
  int status_code = 200;
  std::string status_text = "OK";
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  HttpResponse() = default;
  HttpResponse(int code, std::string text, std::string content_type, std::string response_body)
      : status_code(code), status_text(std::move(text)), body(std::move(response_body)) {
    headers["Content-Type"] = std::move(content_type);
  }

  // Convenience constructors
  static HttpResponse ok(const std::string &content_type, const std::string &body) {
    return HttpResponse(200, "OK", content_type, body);
  }

  static HttpResponse json(const std::string &body) {
    return ok("application/json", body);
  }

  static HttpResponse html(const std::string &body) {
    return ok("text/html; charset=utf-8", body);
  }

  static HttpResponse text(const std::string &body) {
    return ok("text/plain", body);
  }

  static HttpResponse notFound(const std::string &message = "Not Found") {
    return HttpResponse(404, "Not Found", "application/json", R"({"error":")" + json::escape(message) + R"("})");
  }

  static HttpResponse badRequest(const std::string &message = "Bad Request") {
    return HttpResponse(400, "Bad Request", "application/json", R"({"error":")" + json::escape(message) + R"("})");
  }

  static HttpResponse serverError(const std::string &message = "Internal Server Error") {
    return HttpResponse(500, "Internal Server Error", "application/json", R"({"error":")" + json::escape(message) + R"("})");
  }

  static HttpResponse noContent() {
    return HttpResponse(204, "No Content", "text/plain", "");
  }

  /**
   * @brief Set a header
   */
  HttpResponse &setHeader(const std::string &name, const std::string &value) {
    headers[name] = value;
    return *this;
  }

  /**
   * @brief Serialize response to HTTP/1.1 format
   */
  [[nodiscard]] std::string serialize() const;
};

/**
 * @brief Route handler function type
 */
using RouteHandler = std::function<HttpResponse(const HttpRequest &)>;

/**
 * @brief Simple single-threaded HTTP server
 */
class HttpServer {
public:
  HttpServer();
  ~HttpServer();

  // Non-copyable
  HttpServer(const HttpServer &) = delete;
  HttpServer &operator=(const HttpServer &) = delete;

  /**
   * @brief Add a route handler
   * @param method HTTP method (GET, POST, etc.)
   * @param path URL path (exact match)
   * @param handler Handler function
   */
  void addRoute(const std::string &method, const std::string &path, RouteHandler handler);

  /**
   * @brief Set a default handler for unmatched routes
   * @param handler Handler function
   */
  void setDefaultHandler(RouteHandler handler);

  /**
   * @brief Start the server
   * @param port Port to listen on
   * @return true if server started successfully
   */
  bool start(uint16_t port);

  /**
   * @brief Stop the server
   */
  void stop();

  /**
   * @brief Check if server is running
   */
  [[nodiscard]] bool isRunning() const {
    return running_;
  }

  /**
   * @brief Get the port the server is listening on
   */
  [[nodiscard]] uint16_t port() const {
    return port_;
  }

  /**
   * @brief Get the last error message
   */
  [[nodiscard]] const std::string &lastError() const {
    return last_error_;
  }

  /**
   * @brief Process one request (blocking)
   *
   * Used for testing or manual control. Normally you'd call start() instead.
   * @return true if a request was processed, false on error
   */
  bool processOneRequest();

private:
  struct Route {
    std::string method;
    std::string path;
    RouteHandler handler;
  };

  std::vector<Route> routes_;
  RouteHandler default_handler_;

  std::atomic<bool> running_{false};
  std::thread server_thread_;
  uint16_t port_ = 0;
  int server_socket_ = -1;
  std::string last_error_;

  void serverLoop();
  void handleClient(int client_socket);
  bool parseRequest(const std::string &raw, HttpRequest &request);
  void parseQueryString(const std::string &query, std::unordered_map<std::string, std::string> &params);
  std::string urlDecode(const std::string &str);
  RouteHandler findHandler(const std::string &method, const std::string &path);
};

} // namespace ascii_query
