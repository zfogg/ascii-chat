/**
 * @file crypto/http_client.c
 * @ingroup crypto
 * @brief 🌐 HTTPS client with BearSSL for fetching public keys from GitHub/GitLab with CA validation
 */

#include "http_client.h"
#include "common.h"
#include "pem_utils.h"
#include "version.h"
#include "asciichat_errno.h"
#include "platform/socket.h"

#include <bearssl.h>
#ifndef _WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// BearSSL Socket Callbacks
// ============================================================================

/**
 * BearSSL low-level socket read callback
 */
static int sock_read(void *ctx, unsigned char *buf, size_t len) {
  socket_t *sock = (socket_t *)ctx;
  ssize_t n = recv(*sock, (char *)buf, (int)len, 0);
  if (n < 0) {
    SET_ERRNO_SYS(ERROR_NETWORK, "Failed to receive data from socket");
    return -1; // Error
  }
  if (n == 0) {
    SET_ERRNO(ERROR_NETWORK, "Connection closed by remote");
    return -1; // Connection closed
  }
  return (int)n;
}

/**
 * BearSSL low-level socket write callback
 */
static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
  socket_t *sock = (socket_t *)ctx;
  ssize_t n = send(*sock, (const char *)buf, (int)len, 0);
  if (n < 0) {
    SET_ERRNO_SYS(ERROR_NETWORK, "Failed to send data to socket");
    return -1; // Error
  }
  return (int)n;
}

// ============================================================================
// HTTP Response Parsing
// ============================================================================

/**
 * Extract body from HTTP response
 * Returns allocated string with response body, or NULL on error
 */
static char *extract_http_body(const char *response, size_t response_len) {
  // Find "\r\n\r\n" (end of headers)
  const char *body_start = strstr(response, "\r\n\r\n");
  if (!body_start) {
    log_error("No HTTP body found in response");
    return NULL;
  }
  body_start += 4; // Skip "\r\n\r\n"

  size_t body_len = response_len - (size_t)(body_start - response);
  char *body;
  body = SAFE_MALLOC(body_len + 1, char *);
  memcpy(body, body_start, body_len);
  body[body_len] = '\0';

  return body;
}

/**
 * Check if HTTP response indicates success (200 OK)
 */
static asciichat_error_t check_http_status(const char *response) {
  // Look for "HTTP/1.x 200 OK"
  if (strncmp(response, "HTTP/1.", 7) != 0) {
    return SET_ERRNO(ERROR_NETWORK, "Invalid HTTP response: %s", response);
  }

  // Skip to status code
  const char *status = response + 9;
  if (strncmp(status, "200", 3) != 0) {
    return SET_ERRNO(ERROR_NETWORK, "HTTP request failed: %.50s", response);
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// HTTPS GET Implementation
// ============================================================================

char *https_get(const char *hostname, const char *path) {
  if (!hostname || !path) {
    log_error("Invalid arguments to https_get");
    return NULL;
  }

  log_info("HTTPS GET https://%s%s", hostname, path);

  // Load system CA certificates
  char *pem_data = NULL;
  size_t pem_size = 0;
  if (platform_load_system_ca_certs(&pem_data, &pem_size) != 0) {
    log_error("Failed to load system CA certificates");
    return NULL;
  }

  // Parse PEM certificates into BearSSL trust anchors
  anchor_list anchors = ANCHOR_LIST_INIT;
  size_t num_anchors = read_trust_anchors_from_memory(&anchors, (unsigned char *)pem_data, pem_size);
  SAFE_FREE(pem_data);

  if (num_anchors == 0) {
    log_error("No trust anchors loaded");
    // MEMORY FIX: Free anchors before returning to prevent leak
    goto cleanup_anchors;
  }
  log_info("Loaded %zu trust anchors", num_anchors);

  // Resolve hostname using getaddrinfo (modern, non-deprecated API)
  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (getaddrinfo(hostname, "443", &hints, &result) != 0) {
    log_error("Failed to resolve hostname: %s", hostname);
    goto cleanup_anchors;
  }

  // Create TCP socket
  socket_t sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (sock == INVALID_SOCKET_VALUE) {
    log_error("Failed to create socket");
    freeaddrinfo(result);
    goto cleanup_anchors;
  }

  // Connect to server
  if (connect(sock, result->ai_addr, (int)result->ai_addrlen) != 0) {
    log_error("Failed to connect to %s:443", hostname);
    socket_close(sock);
    freeaddrinfo(result);
    goto cleanup_anchors;
  }

  freeaddrinfo(result);

  log_info("Connected to %s:443", hostname);

  // Initialize BearSSL X.509 minimal validator
  br_x509_minimal_context xc;
  br_x509_minimal_init(&xc, &br_sha256_vtable, anchors.buf, anchors.ptr);

  // Initialize BearSSL client context
  br_ssl_client_context sc;
  br_ssl_client_init_full(&sc, &xc, anchors.buf, anchors.ptr);

  // Set I/O buffer
  unsigned char *iobuf;
  iobuf = SAFE_MALLOC(BR_SSL_BUFSIZE_BIDI, unsigned char *);
  br_ssl_engine_set_buffer(&sc.eng, iobuf, BR_SSL_BUFSIZE_BIDI, 1);

  // Initialize I/O context
  br_sslio_context ioc;
  br_sslio_init(&ioc, &sc.eng, sock_read, &sock, sock_write, &sock);

  // Start TLS handshake
  br_ssl_client_reset(&sc, hostname, 0);

  log_info("Starting TLS handshake with %s", hostname);

  // Build HTTP request
  char request[BUFFER_SIZE_LARGE];
  int request_len = safe_snprintf(request, sizeof(request),
                                  "GET %s HTTP/1.1\r\n"
                                  "Host: %s\r\n"
                                  "Connection: close\r\n"
                                  "User-Agent: ascii-chat/" ASCII_CHAT_VERSION_STRING "\r\n"
                                  "\r\n",
                                  path, hostname);

  // Send HTTP request over TLS
  if (br_sslio_write_all(&ioc, request, (size_t)request_len) != 0) {
    log_error("Failed to send HTTP request");
    SAFE_FREE(iobuf);
    socket_close(sock);
    goto cleanup_anchors;
  }

  br_sslio_flush(&ioc);
  log_info("Sent HTTP request");

  // Read HTTP response
  char *response_buf = NULL;
  size_t response_capacity = 8192;
  size_t response_len = 0;
  response_buf = SAFE_MALLOC(response_capacity, char *);

  while (1) {
    // Ensure we have space to read
    if (response_len + 1024 > response_capacity) {
      response_capacity *= 2;
      response_buf = SAFE_REALLOC(response_buf, response_capacity, char *);
    }

    // Read data
    int n = br_sslio_read(&ioc, response_buf + response_len, response_capacity - response_len);
    if (n < 0) {
      // Check for TLS errors
      int err = br_ssl_engine_last_error(&sc.eng);
      if (err != BR_ERR_OK) {
        log_error("TLS error: %d", err);
        SAFE_FREE(response_buf);
        SAFE_FREE(iobuf);
        socket_close(sock);
        goto cleanup_anchors;
      }
      // EOF or connection closed
      break;
    }
    if (n == 0) {
      break; // EOF
    }

    response_len += (size_t)n;
  }

  response_buf[response_len] = '\0';
  log_info("Received %zu bytes", response_len);

  // Close connection
  br_sslio_close(&ioc);
  socket_close(sock);

  // Parse HTTP response
  asciichat_error_t status = check_http_status(response_buf);
  if (status != ASCIICHAT_OK) {
    SAFE_FREE(response_buf);
    SAFE_FREE(iobuf);
    goto cleanup_anchors;
  }

  char *body = extract_http_body(response_buf, response_len);
  SAFE_FREE(response_buf);
  SAFE_FREE(iobuf);

  // Cleanup trust anchors
  for (size_t i = 0; i < anchors.ptr; i++) {
    free_ta_contents(&anchors.buf[i]);
  }
  SAFE_FREE(anchors.buf);

  return body;

cleanup_anchors:
  for (size_t i = 0; i < anchors.ptr; i++) {
    free_ta_contents(&anchors.buf[i]);
  }
  SAFE_FREE(anchors.buf);
  return NULL;
}

// NOTE: fetch_github_ssh_keys, fetch_gitlab_ssh_keys, fetch_github_gpg_keys,
// and fetch_gitlab_gpg_keys have been moved to crypto/keys/https_keys.c
// They properly belong in the keys module, not the http_client module.
