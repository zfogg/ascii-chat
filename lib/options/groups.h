/**
 * @file groups.h
 * @brief Option group definitions for composable mode presets
 * @ingroup options
 *
 * This header defines option groups that allow modes to share common options
 * without duplication. Each group is a collection of related options that
 * can be added to a builder to create mode-specific configurations.
 *
 * ## Option Group Architecture
 *
 * Options are organized into logical groups:
 * - **BINARY**: Options parsed before mode selection (help, version, logging)
 * - **TERMINAL**: Terminal dimension options (width, height)
 * - **NETWORK**: Network connection options (address, port, reconnect)
 * - **WEBCAM**: Video capture options (device, flip, test pattern)
 * - **DISPLAY**: Rendering options (color mode, palette, render mode)
 * - **AUDIO**: Audio streaming options (enable, devices, volume)
 * - **SNAPSHOT**: Single-frame capture options
 * - **CRYPTO**: Encryption and authentication options
 * - **COMPRESSION**: Network compression options
 * - **ACDS**: Discovery service integration options
 * - **MEDIA**: Media file streaming options
 *
 * ## Mode Group Composition
 *
 * Each mode uses a specific combination of groups:
 * - **Server**: BINARY + NETWORK + CRYPTO + COMPRESSION + ACDS
 * - **Client**: BINARY + TERMINAL + NETWORK + WEBCAM + DISPLAY + AUDIO + SNAPSHOT + CRYPTO + COMPRESSION + ACDS + MEDIA
 * - **Mirror**: BINARY + TERMINAL + WEBCAM + DISPLAY + SNAPSHOT + MEDIA
 * - **Discovery**: BINARY + TERMINAL + NETWORK + WEBCAM + DISPLAY + AUDIO + SNAPSHOT + CRYPTO + ACDS + MEDIA
 * - **ACDS**: BINARY + NETWORK + CRYPTO
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdint.h>

/**
 * @brief Option group identifiers
 *
 * Bit flags that identify groups of related options. These can be combined
 * to specify which option groups a mode supports.
 *
 * @ingroup options
 */
typedef enum {
  OPT_GROUP_NONE        = 0,          ///< No groups
  OPT_GROUP_BINARY      = (1 << 0),   ///< Binary-level options (help, version, logging)
  OPT_GROUP_TERMINAL    = (1 << 1),   ///< Terminal dimension options (width, height)
  OPT_GROUP_NETWORK     = (1 << 2),   ///< Network options (address, port, reconnect)
  OPT_GROUP_WEBCAM      = (1 << 3),   ///< Webcam options (device, flip, test pattern)
  OPT_GROUP_DISPLAY     = (1 << 4),   ///< Display options (color mode, palette, render mode)
  OPT_GROUP_AUDIO       = (1 << 5),   ///< Audio options (enable, devices, volume)
  OPT_GROUP_SNAPSHOT    = (1 << 6),   ///< Snapshot mode options
  OPT_GROUP_CRYPTO      = (1 << 7),   ///< Encryption and authentication options
  OPT_GROUP_COMPRESSION = (1 << 8),   ///< Compression options
  OPT_GROUP_ACDS        = (1 << 9),   ///< ACDS discovery service options
  OPT_GROUP_MEDIA       = (1 << 10),  ///< Media file streaming options
  OPT_GROUP_SERVER      = (1 << 11),  ///< Server-specific options (max_clients, client-keys)
  OPT_GROUP_CLIENT      = (1 << 12),  ///< Client-specific options (reconnect, server-key)
  OPT_GROUP_WEBRTC      = (1 << 13),  ///< WebRTC connectivity options (STUN/TURN)
} option_group_t;

/**
 * @brief Mode group presets
 *
 * Predefined combinations of option groups for each mode.
 * These define which options are available in each mode.
 *
 * @ingroup options
 */

/** Server mode options: network binding, crypto, compression, ACDS */
#define MODE_SERVER_GROUPS    (OPT_GROUP_BINARY | OPT_GROUP_NETWORK | OPT_GROUP_CRYPTO | \
                               OPT_GROUP_COMPRESSION | OPT_GROUP_ACDS | OPT_GROUP_SERVER | \
                               OPT_GROUP_WEBRTC)

/** Client mode options: display, webcam, audio, crypto, ACDS */
#define MODE_CLIENT_GROUPS    (OPT_GROUP_BINARY | OPT_GROUP_TERMINAL | OPT_GROUP_NETWORK | \
                               OPT_GROUP_WEBCAM | OPT_GROUP_DISPLAY | OPT_GROUP_AUDIO | \
                               OPT_GROUP_SNAPSHOT | OPT_GROUP_CRYPTO | OPT_GROUP_COMPRESSION | \
                               OPT_GROUP_ACDS | OPT_GROUP_MEDIA | OPT_GROUP_CLIENT | OPT_GROUP_WEBRTC)

/** Mirror mode options: local webcam preview without networking */
#define MODE_MIRROR_GROUPS    (OPT_GROUP_BINARY | OPT_GROUP_TERMINAL | OPT_GROUP_WEBCAM | \
                               OPT_GROUP_DISPLAY | OPT_GROUP_SNAPSHOT | OPT_GROUP_MEDIA)

/** Discovery mode options: participant that can become host */
#define MODE_DISCOVERY_GROUPS (OPT_GROUP_BINARY | OPT_GROUP_TERMINAL | OPT_GROUP_NETWORK | \
                               OPT_GROUP_WEBCAM | OPT_GROUP_DISPLAY | OPT_GROUP_AUDIO | \
                               OPT_GROUP_SNAPSHOT | OPT_GROUP_CRYPTO | OPT_GROUP_COMPRESSION | \
                               OPT_GROUP_ACDS | OPT_GROUP_MEDIA | OPT_GROUP_WEBRTC)

/** ACDS mode options: discovery service with network and crypto */
#define MODE_ACDS_GROUPS      (OPT_GROUP_BINARY | OPT_GROUP_NETWORK | OPT_GROUP_CRYPTO | \
                               OPT_GROUP_WEBRTC)

/**
 * @brief Get human-readable name for an option group
 *
 * @param group Option group identifier
 * @return Static string with group name, or "UNKNOWN" if invalid
 *
 * @ingroup options
 */
static inline const char *option_group_name(option_group_t group) {
  switch (group) {
    case OPT_GROUP_NONE:        return "NONE";
    case OPT_GROUP_BINARY:      return "BINARY";
    case OPT_GROUP_TERMINAL:    return "TERMINAL";
    case OPT_GROUP_NETWORK:     return "NETWORK";
    case OPT_GROUP_WEBCAM:      return "WEBCAM";
    case OPT_GROUP_DISPLAY:     return "DISPLAY";
    case OPT_GROUP_AUDIO:       return "AUDIO";
    case OPT_GROUP_SNAPSHOT:    return "SNAPSHOT";
    case OPT_GROUP_CRYPTO:      return "CRYPTO";
    case OPT_GROUP_COMPRESSION: return "COMPRESSION";
    case OPT_GROUP_ACDS:        return "ACDS";
    case OPT_GROUP_MEDIA:       return "MEDIA";
    case OPT_GROUP_SERVER:      return "SERVER";
    case OPT_GROUP_CLIENT:      return "CLIENT";
    case OPT_GROUP_WEBRTC:      return "WEBRTC";
    default:                    return "UNKNOWN";
  }
}

/**
 * @brief Check if a mode includes a specific option group
 *
 * @param mode_groups The mode's group bitmask (e.g., MODE_SERVER_GROUPS)
 * @param group The group to check for
 * @return true if the mode includes the group, false otherwise
 *
 * @ingroup options
 */
static inline bool option_group_has(uint32_t mode_groups, option_group_t group) {
  return (mode_groups & (uint32_t)group) != 0;
}
