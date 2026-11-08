#include <ascii-chat/network/webrtc/config.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/options/options.h>
#include <string.h>

asciichat_error_t webrtc_turn_servers_parse(const char *csv_urls, const char *username, const char *credential,
                                            turn_server_t *servers, size_t capacity, size_t *count_out) {
  if (!csv_urls || !username || !credential || !servers || !count_out || capacity == 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid TURN server configuration");

  *count_out = 0;
  if (!csv_urls[0] || !username[0] || !credential[0])
    return ASCIICHAT_OK;

  char copy[OPTIONS_BUFF_SIZE];
  SAFE_STRNCPY(copy, csv_urls, sizeof(copy));
  char *saveptr = NULL;
  char *token = platform_strtok_r(copy, ",", &saveptr);
  while (token && *count_out < capacity) {
    while (*token == ' ' || *token == '\t')
      token++;
    size_t len = strlen(token);
    while (len && (token[len - 1] == ' ' || token[len - 1] == '\t'))
      token[--len] = '\0';
    if (len >= sizeof(servers[*count_out].url))
      return SET_ERRNO(ERROR_INVALID_PARAM, "TURN URL is too long");
    if (len) {
      turn_server_t *server = &servers[*count_out];
      server->url_len = (uint8_t)len;
      SAFE_STRNCPY(server->url, token, sizeof(server->url));
      SAFE_STRNCPY(server->username, username, sizeof(server->username));
      server->username_len = (uint8_t)strlen(server->username);
      SAFE_STRNCPY(server->credential, credential, sizeof(server->credential));
      server->credential_len = (uint8_t)strlen(server->credential);
      (*count_out)++;
    }
    token = platform_strtok_r(NULL, ",", &saveptr);
  }
  return ASCIICHAT_OK;
}
