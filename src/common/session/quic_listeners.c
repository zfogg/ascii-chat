#include "quic_listeners.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/log/log.h>
#include <string.h>

asciichat_error_t session_quic_listeners_open(int port, acip_quic_endpoint_t **native,
                                              acip_quic_endpoint_t **webtransport) {
  if (!native || !webtransport || *native || *webtransport || port < 1 || port > UINT16_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session QUIC listener state");
  const char *native_address = GET_OPTION(quic_bind_address);
  const char *browser_address = GET_OPTION(webtransport_bind_address);
  const char *origin = GET_OPTION(webtransport_origin);
  const char *path = GET_OPTION(webtransport_path);
  const char *certificate = GET_OPTION(quic_tls_cert);
  const char *key = GET_OPTION(quic_tls_key);
  int quic_port = GET_OPTION(quic_port);
  bool browser = *browser_address || *origin;
  bool requested = *native_address || browser || quic_port;
  if (!requested) {
    if (*certificate || *key)
      return SET_ERRNO(ERROR_USAGE, "QUIC TLS credentials require a QUIC listener");
    return ASCIICHAT_OK;
  }
  if (quic_port < 0 || quic_port > UINT16_MAX)
    return SET_ERRNO(ERROR_USAGE, "QUIC port must be between 0 and 65535");
  const char *address = *native_address ? native_address : browser_address;
  if (!*address || !*certificate || !*key)
    return SET_ERRNO(ERROR_USAGE, "QUIC requires a bind address, --quic-tls-cert, and --quic-tls-key");
  if (browser && (!*origin || !*path))
    return SET_ERRNO(ERROR_USAGE, "WebTransport requires --webtransport-origin and --webtransport-path");
  if (*native_address && *browser_address && strcmp(native_address, browser_address))
    return SET_ERRNO(ERROR_USAGE, "Native QUIC and WebTransport share --quic-port and must use the same bind address");
  struct sockaddr_storage bind_address;
  socklen_t bind_length = 0;
  uint16_t selected_port = (uint16_t)(quic_port ? quic_port : port);
  asciichat_error_t result = parse_numeric_socket_address(address, selected_port, &bind_address, &bind_length);
  if (result != ASCIICHAT_OK)
    return result;
  acip_quic_config_t config = {.certificate_file = certificate,
                               .private_key_file = key,
                               .webtransport_origin = browser ? origin : NULL,
                               .webtransport_path = browser ? path : NULL,
                               .enable_datagrams = true,
                               .accept_native = browser,
                               .prefer_audio_datagrams = GET_OPTION(quic_audio_datagrams),
                               .prefer_visual_datagrams = GET_OPTION(quic_visual_datagrams)};
  // One endpoint owns the UDP socket and dispatches each connection by ALPN.
  // Keep the second output empty so teardown and accept never alias ownership.
  result = acip_quic_endpoint_open((const struct sockaddr *)&bind_address, bind_length, true, &config, native);
  if (result != ASCIICHAT_OK)
    return result;
  log_info("Native QUIC listener initialized on %s UDP port %u", address, selected_port);
  if (browser)
    log_info("WebTransport listener initialized on %s UDP port %u (shared QUIC listener)", address, selected_port);

  return ASCIICHAT_OK;
}

asciichat_error_t session_quic_listeners_close(acip_quic_endpoint_t **native, acip_quic_endpoint_t **webtransport) {
  if (!native || !webtransport)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing session QUIC listener outputs");
  acip_quic_endpoint_t **endpoints[] = {webtransport, native};
  for (size_t i = 0; i < 2; ++i) {
    if (!*endpoints[i])
      continue;
    asciichat_error_t result = acip_quic_endpoint_stop(*endpoints[i]);
    if (result == ASCIICHAT_OK)
      result = acip_quic_endpoint_destroy(endpoints[i]);
    if (result != ASCIICHAT_OK)
      return result;
  }
  return ASCIICHAT_OK;
}
