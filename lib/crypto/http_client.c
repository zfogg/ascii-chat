/**
 * @file http_client.c
 * @brief HTTPS client implementation using BearSSL
 *
 * Simple HTTPS GET client for fetching public keys from GitHub/GitLab.
 * Uses BearSSL for TLS and system CA certificates for trust validation.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "http_client.h"
#include "common.h"
#include "pem_utils.h"
#include "platform/abstraction.h"
#include "version.h"

#include <bearssl.h>
#ifndef _WIN32
#include <netdb.h>
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
    return -1; // Error
  }
  if (n == 0) {
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

  size_t body_len = response_len - (body_start - response);
  char *body;
  SAFE_MALLOC(body, body_len + 1, char *);
  memcpy(body, body_start, body_len);
  body[body_len] = '\0';

  return body;
}

/**
 * Check if HTTP response indicates success (200 OK)
 */
static int check_http_status(const char *response) {
  // Look for "HTTP/1.x 200 OK"
  if (strncmp(response, "HTTP/1.", 7) != 0) {
    log_error("Invalid HTTP response");
    return -1;
  }

  // Skip to status code
  const char *status = response + 9;
  if (strncmp(status, "200", 3) != 0) {
    log_error("HTTP request failed: %.50s", response);
    return -1;
  }

  return 0;
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
  free(pem_data);

  if (num_anchors == 0) {
    log_error("No trust anchors loaded");
    return NULL;
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
  SAFE_MALLOC(iobuf, BR_SSL_BUFSIZE_BIDI, unsigned char *);
  br_ssl_engine_set_buffer(&sc.eng, iobuf, BR_SSL_BUFSIZE_BIDI, 1);

  // Initialize I/O context
  br_sslio_context ioc;
  br_sslio_init(&ioc, &sc.eng, sock_read, &sock, sock_write, &sock);

  // Start TLS handshake
  br_ssl_client_reset(&sc, hostname, 0);

  log_info("Starting TLS handshake with %s", hostname);

  // Build HTTP request
  char request[1024];
  int request_len = snprintf(request, sizeof(request),
                             "GET %s HTTP/1.1\r\n"
                             "Host: %s\r\n"
                             "Connection: close\r\n"
                             "User-Agent: ascii-chat/" ASCII_CHAT_VERSION_STRING "\r\n"
                             "\r\n",
                             path, hostname);

  // Send HTTP request over TLS
  if (br_sslio_write_all(&ioc, request, request_len) != 0) {
    log_error("Failed to send HTTP request");
    free(iobuf);
    socket_close(sock);
    goto cleanup_anchors;
  }

  br_sslio_flush(&ioc);
  log_info("Sent HTTP request");

  // Read HTTP response
  char *response_buf = NULL;
  size_t response_capacity = 8192;
  size_t response_len = 0;
  SAFE_MALLOC(response_buf, response_capacity, char *);

  while (1) {
    // Ensure we have space to read
    if (response_len + 1024 > response_capacity) {
      response_capacity *= 2;
      SAFE_REALLOC(response_buf, response_capacity, char *);
    }

    // Read data
    int n = br_sslio_read(&ioc, response_buf + response_len, response_capacity - response_len);
    if (n < 0) {
      // Check for TLS errors
      int err = br_ssl_engine_last_error(&sc.eng);
      if (err != BR_ERR_OK) {
        log_error("TLS error: %d", err);
        free(response_buf);
        free(iobuf);
        socket_close(sock);
        goto cleanup_anchors;
      }
      // EOF or connection closed
      break;
    }
    if (n == 0) {
      break; // EOF
    }

    response_len += n;
  }

  response_buf[response_len] = '\0';
  log_info("Received %zu bytes", response_len);

  // Close connection
  br_sslio_close(&ioc);
  socket_close(sock);

  // Parse HTTP response
  if (check_http_status(response_buf) != 0) {
    free(response_buf);
    free(iobuf);
    goto cleanup_anchors;
  }

  char *body = extract_http_body(response_buf, response_len);
  free(response_buf);
  free(iobuf);

  // Cleanup trust anchors
  for (size_t i = 0; i < anchors.ptr; i++) {
    free_ta_contents(&anchors.buf[i]);
  }
  free(anchors.buf);

  return body;

cleanup_anchors:
  for (size_t i = 0; i < anchors.ptr; i++) {
    free_ta_contents(&anchors.buf[i]);
  }
  free(anchors.buf);
  return NULL;
}

// ============================================================================
// GitHub/GitLab Key Fetching - SSH Keys
// ============================================================================

/**
 * Filter Ed25519 keys from SSH key response
 * Returns allocated array of strings (caller must free)
 */
static char **filter_ed25519_keys(const char *keys_text, size_t *num_keys_out) {
  if (!keys_text || !num_keys_out) {
    return NULL;
  }

  *num_keys_out = 0;

  // Count Ed25519 keys
  size_t num_keys = 0;
  const char *line = keys_text;
  while (*line) {
    if (strncmp(line, "ssh-ed25519 ", 12) == 0) {
      num_keys++;
    }
    // Skip to next line
    while (*line && *line != '\n') {
      line++;
    }
    if (*line == '\n') {
      line++;
    }
  }

  if (num_keys == 0) {
    return NULL;
  }

  // Allocate array of key strings
  char **keys;
  SAFE_MALLOC(keys, num_keys * sizeof(char *), char **);

  // Extract Ed25519 keys
  size_t key_idx = 0;
  line = keys_text;
  while (*line && key_idx < num_keys) {
    if (strncmp(line, "ssh-ed25519 ", 12) == 0) {
      // Find end of line
      const char *line_end = line;
      while (*line_end && *line_end != '\n' && *line_end != '\r') {
        line_end++;
      }

      // Allocate and copy key
      size_t key_len = line_end - line;
      char *key;
      SAFE_MALLOC(key, key_len + 1, char *);
      memcpy(key, line, key_len);
      key[key_len] = '\0';

      keys[key_idx++] = key;
    }

    // Skip to next line
    while (*line && *line != '\n') {
      line++;
    }
    if (*line == '\n') {
      line++;
    }
  }

  *num_keys_out = num_keys;
  return keys;
}

int fetch_github_ssh_keys(const char *username, char ***keys_out, size_t *num_keys_out) {
  if (!username || !keys_out || !num_keys_out) {
    return -1;
  }

  // Build GitHub keys URL: https://github.com/username.keys
  char path[512];
  snprintf(path, sizeof(path), "/%s.keys", username);

  char *keys_text = https_get("github.com", path);
  if (!keys_text) {
    return -1;
  }

  // Filter Ed25519 keys only
  char **keys = filter_ed25519_keys(keys_text, num_keys_out);
  free(keys_text);

  if (!keys) {
    log_error("No Ed25519 keys found for GitHub user: %s", username);
    return -1;
  }

  *keys_out = keys;
  return 0;
}

int fetch_gitlab_ssh_keys(const char *username, char ***keys_out, size_t *num_keys_out) {
  if (!username || !keys_out || !num_keys_out) {
    return -1;
  }

  // Build GitLab keys URL: https://gitlab.com/username.keys
  char path[512];
  snprintf(path, sizeof(path), "/%s.keys", username);

  char *keys_text = https_get("gitlab.com", path);
  if (!keys_text) {
    return -1;
  }

  // Filter Ed25519 keys only
  char **keys = filter_ed25519_keys(keys_text, num_keys_out);
  free(keys_text);

  if (!keys) {
    log_error("No Ed25519 keys found for GitLab user: %s", username);
    return -1;
  }

  *keys_out = keys;
  return 0;
}

// ============================================================================
// GitHub/GitLab Key Fetching - GPG Keys
// ============================================================================

/**
 * Parse PGP armored key blocks from response text
 * Returns allocated array of key block strings (caller must free)
 */
static char **parse_pgp_key_blocks(const char *gpg_text, size_t *num_keys_out) {
  if (!gpg_text || !num_keys_out) {
    return NULL;
  }

  *num_keys_out = 0;

  // Count number of PGP key blocks
  size_t num_keys = 0;
  const char *search = gpg_text;
  while ((search = strstr(search, "-----BEGIN PGP PUBLIC KEY BLOCK-----")) != NULL) {
    num_keys++;
    search += 36; // Skip past the BEGIN marker
  }

  if (num_keys == 0) {
    return NULL;
  }

  // Allocate array for keys
  char **keys;
  SAFE_MALLOC(keys, num_keys * sizeof(char *), char **);

  // Extract each key block
  size_t key_idx = 0;
  const char *block_start = gpg_text;
  while (key_idx < num_keys) {
    // Find BEGIN marker
    const char *begin = strstr(block_start, "-----BEGIN PGP PUBLIC KEY BLOCK-----");
    if (!begin) {
      break;
    }

    // Find END marker
    const char *end = strstr(begin, "-----END PGP PUBLIC KEY BLOCK-----");
    if (!end) {
      break;
    }

    // Include the END marker in the key block
    end += 35; // strlen("-----END PGP PUBLIC KEY BLOCK-----")

    // Find end of line after END marker (or end of string)
    while (*end && *end != '\n') {
      end++;
    }
    if (*end == '\n') {
      end++;
    }

    // Allocate and copy key block
    size_t block_len = end - begin;
    char *key;
    SAFE_MALLOC(key, block_len + 1, char *);
    memcpy(key, begin, block_len);
    key[block_len] = '\0';

    keys[key_idx++] = key;
    block_start = end;
  }

  *num_keys_out = key_idx;
  return keys;
}

int fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys_out) {
  if (!username || !keys_out || !num_keys_out) {
    return -1;
  }

  // Build GitHub GPG keys URL: https://github.com/username.gpg
  char path[512];
  snprintf(path, sizeof(path), "/%s.gpg", username);

  char *gpg_text = https_get("github.com", path);
  if (!gpg_text) {
    return -1;
  }

  // Parse PGP key blocks
  char **keys = parse_pgp_key_blocks(gpg_text, num_keys_out);
  free(gpg_text);

  if (!keys) {
    log_error("No GPG keys found for GitHub user: %s", username);
    return -1;
  }

  *keys_out = keys;
  return 0;
}

int fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys_out) {
  if (!username || !keys_out || !num_keys_out) {
    return -1;
  }

  // Build GitLab GPG keys URL: https://gitlab.com/username.gpg
  char path[512];
  snprintf(path, sizeof(path), "/%s.gpg", username);

  char *gpg_text = https_get("gitlab.com", path);
  if (!gpg_text) {
    return -1;
  }

  // Parse PGP key blocks
  char **keys = parse_pgp_key_blocks(gpg_text, num_keys_out);
  free(gpg_text);

  if (!keys) {
    log_error("No GPG keys found for GitLab user: %s", username);
    return -1;
  }

  *keys_out = keys;
  return 0;
}
