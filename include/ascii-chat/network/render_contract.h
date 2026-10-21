#pragma once
#include <ascii-chat/network/acip/settings.h>
#include <ascii-chat/common/protocol_constants.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_RENDER_MAX_BYTES (16u * 1024u * 1024u)
#define FRAME_RENDER_CONTRACT_SCHEMA 1u
#define FRAME_RENDER_CONTRACT_HEADER 20u
#define FRAME_RENDER_CONTRACT_MAX (FRAME_RENDER_CONTRACT_HEADER + 255u)
#define FRAME_RENDER_CONTRACT_RAIN 1u
#define FRAME_RENDER_CONTRACT_GRID 2u

/* Explicit, big-endian wire prefix: schema, revision, render mode, effects,
 * palette byte length, followed by that many UTF-8 bytes (no NUL). Text sizes,
 * compression and CRC in the legacy frame header continue to describe TEXT. */
asciichat_error_t frame_render_contract_encode(const acip_render_settings_t *settings, uint32_t effects, uint8_t *wire,
                                               size_t *length);
asciichat_error_t frame_render_contract_decode(const void *wire, size_t available, acip_render_settings_t *settings,
                                               uint32_t *effects, size_t *length);

#ifdef __cplusplus
}
#endif
