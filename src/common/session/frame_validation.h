#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>
#include <stdint.h>

/* Complete rendered text frames from local converters or network peers.
 * Geometry uses utf8proc display cells, matching the shared grid renderer.
 * Returns ERROR_INVALID_FRAME for malformed content. Binary image/video output
 * and terminal UI control sequences are separate presentation surfaces. */
asciichat_error_t session_frame_validate(const char *text, size_t length, uint32_t width, uint32_t height);

#include <ascii-chat/network/render_contract.h>
asciichat_error_t session_frame_validate_contract(const char *text, size_t length, uint32_t width, uint32_t height,
                                                  const acip_render_settings_t *settings, uint32_t effects);
