/**
 * @file network/connection_endpoint.c
 * @brief Shared endpoint normalization for TCP and WebSocket connections
 */

#include <ascii-chat/network/connection_endpoint.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/url.h>

#include <string.h>

const char *connection_endpoint_protocol_name(connection_endpoint_protocol_t protocol) {
  switch (protocol) {
  case CONNECTION_ENDPOINT_TCP:
    return "TCP";
  case CONNECTION_ENDPOINT_WEBSOCKET:
    return "WebSocket";
  default:
    return "Unknown";
  }
}

static asciichat_error_t connection_endpoint_from_parts(connection_endpoint_t *endpoint_out, const url_parts_t *parts,
                                                        connection_endpoint_protocol_t protocol,
                                                        uint16_t default_port, const char *input) {
  if (!endpoint_out || !parts || !parts->host) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid endpoint parse state");
  }

  memset(endpoint_out, 0, sizeof(*endpoint_out));
  endpoint_out->protocol = protocol;
  SAFE_STRNCPY(endpoint_out->input, input, sizeof(endpoint_out->input));
  SAFE_STRNCPY(endpoint_out->host, parts->host, sizeof(endpoint_out->host));
  endpoint_out->port = parts->port > 0 ? (uint16_t)parts->port : default_port;
  endpoint_out->has_explicit_port = parts->port > 0;
  return ASCIICHAT_OK;
}

asciichat_error_t connection_endpoint_resolve(const char *input, uint16_t default_port,
                                              connection_endpoint_t *endpoint_out) {
  if (!input || !*input || !endpoint_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "input or endpoint_out is NULL");
  }

  url_parts_t parts = {0};
  asciichat_error_t result;

  if (url_is_websocket(input)) {
    result = url_parse(input, &parts);
    if (result != ASCIICHAT_OK) {
      return result;
    }
    result = connection_endpoint_from_parts(endpoint_out, &parts, CONNECTION_ENDPOINT_WEBSOCKET, default_port, input);
    url_parts_destroy(&parts);
    return result;
  }

  if (url_is_tcp(input)) {
    result = url_parse(input, &parts);
    if (result != ASCIICHAT_OK) {
      return result;
    }
    result = connection_endpoint_from_parts(endpoint_out, &parts, CONNECTION_ENDPOINT_TCP, default_port, input);
    url_parts_destroy(&parts);
    return result;
  }

  /* Unsupported explicit scheme. Bare hostnames are allowed and have no "://". */
  if (strstr(input, "://") != NULL) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported connection scheme: %s", input);
  }

  result = url_parse(input, &parts);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  result = connection_endpoint_from_parts(endpoint_out, &parts, CONNECTION_ENDPOINT_TCP, default_port, input);
  url_parts_destroy(&parts);
  return result;
}
