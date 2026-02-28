#pragma once

/**
 * @file session/h265/protocol.h
 * @brief H.265 codec transport protocol definitions
 */

#include <stdint.h>
#include <stddef.h>

/**
 * H.265 frame packet type identifier
 * Sent as part of the network packet header to identify codec payloads
 */
#define PACKET_TYPE_IMAGE_FRAME_H265  0x42

/**
 * H.265 packet header structure
 * Embedded in network packets for codec frames
 *
 * Layout:
 *   [type: u8][client_id: u32][frame_num: u32][flags: u8][reserved: u8][payload...]
 */
typedef struct {
    uint8_t type;           // PACKET_TYPE_IMAGE_FRAME_H265
    uint32_t client_id;     // Client identifier
    uint32_t frame_num;     // Frame sequence number
    uint8_t flags;          // H265_FLAG_* flags
    uint8_t reserved;       // Reserved for future use
} h265_packet_header_t;

/**
 * H.265 frame flags
 */
#define H265_FLAG_KEYFRAME      0x01    // IDR frame (keyframe)
#define H265_FLAG_SIZE_CHANGE   0x02    // Frame dimensions changed

#endif
