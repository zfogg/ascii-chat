#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/webrtc/turn.h>
#include <stddef.h>

/** Parse TURN URLs and apply one set of credentials to every endpoint. */
asciichat_error_t webrtc_turn_servers_parse(const char *csv_urls, const char *username, const char *credential,
                                            turn_server_t *servers, size_t capacity, size_t *count_out);
