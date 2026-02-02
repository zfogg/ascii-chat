#pragma once

/**
 * @file session/keyboard_handler.h
 * @brief 🎮 Keyboard input handler for interactive session controls
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * Provides keyboard input handling for session-level controls including:
 * - Media playback control (play/pause, seek)
 * - Audio control (volume, mute)
 * - Display control (color mode, webcam flip)
 * - Help screen toggle
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "capture.h"
#include "../platform/keyboard.h"

/* Forward declaration of opaque display context */
typedef struct session_display_ctx session_display_ctx_t;

/* ============================================================================
 * Keyboard Handler Functions
 * @{
 */

/**
 * @brief Handle keyboard input in a session
 * @param capture Capture context (may be NULL for client-only mode)
 * @param display Display context (may be NULL if not needed)
 * @param key Keyboard key code from keyboard_key_t enumeration
 *
 * Processes keyboard input and performs appropriate session actions. Supports both
 * mirror mode (local capture) and client mode (network rendering).
 *
 * **Keyboard Controls:**
 * - **'?' key**: Toggle help screen on/off (all modes)
 * - **Left Arrow**: Seek -30 seconds (file/URL sources only)
 * - **Right Arrow**: Seek +30 seconds (file/URL sources only)
 * - **Up Arrow**: Increase volume +10% (all modes with audio)
 * - **Down Arrow**: Decrease volume -10% (all modes with audio)
 * - **Spacebar**: Play/pause toggle (file/URL sources only)
 * - **'c' key**: Cycle through color modes (mono → 16 → 256 → truecolor)
 * - **'m' key**: Toggle mute (remembers previous volume for unmute)
 * - **'f' key**: Flip webcam horizontally (mirroring)
 *
 * **Source-specific behavior:**
 * - Help screen: Works in all modes (toggles with '?')
 * - Seek/pause controls: Only work with file and URL sources (not webcam)
 * - Volume/mute: Work with any audio-capable source
 * - Color cycling: Works with all rendering modes
 * - Flip: Works with all capture sources (webcam, media files, etc.)
 *
 * **Audio volume:**
 * - Volume range: [0.0, 1.0] (0% = silent, 100% = maximum/normal)
 * - Default volume: 1.0 (100%)
 * - Mute state: Remembers the previous non-zero volume when toggled back on
 * - Unrecognized keys are silently ignored (no error returned)
 *
 * **Thread safety:**
 * - Safe to call from render thread (video/audio threads)
 * - Safe to call concurrently from multiple render threads
 * - Uses RCU-protected options for atomic updates
 * - No explicit locking required (option system handles synchronization)
 * - Does not block waiting for I/O or locks
 *
 * **Error handling:**
 * - Always succeeds (void return)
 * - Invalid or unrecognized keys are silently ignored
 * - If capture is NULL (client-only mode), seek/pause requests are silently ignored
 * - If display is NULL (non-TTY mode), help screen toggle is silently ignored
 * - If audio system is unavailable, volume changes are silently ignored
 *
 * @param capture NULL for client-only mode (network rendering), non-NULL for local capture
 * @param display Display context for help screen toggle (may be NULL)
 * @param key Valid key code from keyboard_key_t enum (0-127 for ASCII, special key codes)
 *
 * @note Safe to call without prior keyboard_init() (silently handles invalid keys)
 * @note In client-only mode, capture is NULL (seek/pause unavailable, other controls work)
 * @note Volume adjustments are immediate without audible artifacts
 * @note Mute persists across multiple mute toggles with volume memory
 *
 * @ingroup session
 */
void session_handle_keyboard_input(session_capture_ctx_t *capture, session_display_ctx_t *display, keyboard_key_t key);

/** @} */
/** @} */
