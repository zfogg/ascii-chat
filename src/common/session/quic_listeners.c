#include "quic_listeners.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/log/log.h>

asciichat_error_t session_quic_listeners_open(int port, acip_quic_endpoint_t **native,
                                              acip_quic_endpoint_t **webtransport) {
  if (!native || !webtransport || *native || *webtransport || port < 1 || port > UINT16_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session QUIC listener state");
  asciichat_error_t result = ASCIICHAT_OK;
  const char *quic_address = GET_OPTION(quic_bind_address);
  const char *quic_certificate = GET_OPTION(quic_tls_cert);
  const char *quic_key = GET_OPTION(quic_tls_key);
  int quic_port = GET_OPTION(quic_port);
  const char *webtransport_address = GET_OPTION(webtransport_bind_address);
  const char *webtransport_origin = GET_OPTION(webtransport_origin);
  const char *webtransport_path = GET_OPTION(webtransport_path);
  int webtransport_port = GET_OPTION(webtransport_port);
  bool native_quic_requested = *quic_address || quic_port;
  bool webtransport_requested = *webtransport_address || *webtransport_origin || webtransport_port;
  if ((*quic_certificate || *quic_key) && !native_quic_requested && !webtransport_requested) {
    result = SET_ERRNO(ERROR_USAGE, "QUIC TLS credentials require a QUIC or WebTransport listener");
    return result;
  }
  if (native_quic_requested) {
    if (quic_port < 0 || quic_port > 65535) {
      result = SET_ERRNO(ERROR_USAGE, "QUIC port must be between 0 and 65535");
      return result;
    }
    if (!*quic_address || !*quic_certificate || !*quic_key) {
      result = SET_ERRNO(ERROR_USAGE, "QUIC requires --quic-bind-address, --quic-tls-cert, and --quic-tls-key");
      return result;
    }
    struct sockaddr_storage bind_address;
    socklen_t bind_length = 0;
    result = parse_numeric_socket_address(quic_address, (uint16_t)(quic_port ? quic_port : port), &bind_address,
                                          &bind_length);
    if (result != ASCIICHAT_OK)
      return result;
    acip_quic_config_t quic_config = {.certificate_file = quic_certificate,
                                      .private_key_file = quic_key,
                                      .enable_datagrams = true,
                                      .prefer_audio_datagrams = GET_OPTION(quic_audio_datagrams),
                                      .prefer_visual_datagrams = GET_OPTION(quic_visual_datagrams)};
    result = acip_quic_endpoint_open((const struct sockaddr *)&bind_address, bind_length, true, &quic_config, native);
    if (result != ASCIICHAT_OK)
      return result;
    log_info("Native QUIC listener initialized on %s UDP port %d", quic_address, quic_port ? quic_port : port);
  }

  if (webtransport_requested) {
    if (!*webtransport_address || !*webtransport_origin || !*webtransport_path || !*quic_certificate || !*quic_key ||
        webtransport_port < 0 || webtransport_port > 65535) {
      result = SET_ERRNO(ERROR_USAGE, "WebTransport requires --webtransport-bind-address, --webtransport-origin, "
                                      "--quic-tls-cert, and --quic-tls-key");
      return result;
    }
    struct sockaddr_storage bind_address;
    socklen_t bind_length = 0;
    result = parse_numeric_socket_address(
        webtransport_address, (uint16_t)(webtransport_port ? webtransport_port : port), &bind_address, &bind_length);
    if (result != ASCIICHAT_OK)
      return result;
    acip_quic_config_t config = {.certificate_file = quic_certificate,
                                 .private_key_file = quic_key,
                                 .webtransport_origin = webtransport_origin,
                                 .webtransport_path = webtransport_path,
                                 .enable_datagrams = true,
                                 .prefer_audio_datagrams = GET_OPTION(quic_audio_datagrams),
                                 .prefer_visual_datagrams = GET_OPTION(quic_visual_datagrams)};
    result = acip_quic_endpoint_open((const struct sockaddr *)&bind_address, bind_length, true, &config, webtransport);
    if (result != ASCIICHAT_OK)
      return result;
    log_info("WebTransport listener initialized on %s UDP port %d", webtransport_address,
             webtransport_port ? webtransport_port : port);
  }

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
