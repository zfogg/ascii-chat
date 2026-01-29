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
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "capture.h"

/* ============================================================================
 * Keyboard Handler Functions
 * @{
 */

/**
 * @brief Handle keyboard input in a session
 * @param capture Capture context (may be NULL for client mode)
 * @param key Keyboard key code (from keyboard_read_nonblocking)
 *
 * Processes keyboard input and performs appropriate session actions.
 * Supports both mirror mode (local capture) and client mode (network rendering).
 *
 * **Keyboard Controls:**
 * - **Left/Right Arrow**: Seek ±30 seconds (file/URL sources only)
 * - **Up/Down Arrow**: Adjust volume ±10% (all modes with audio)
 * - **Spacebar**: Play/pause toggle (file/URL sources only)
 * - **'c' key**: Cycle through color modes (mono → 16 → 256 → truecolor)
 * - **'m' key**: Toggle mute (remember previous volume)
 * - **'f' key**: Flip webcam horizontally
 *
 * **Source-specific behavior:**
 * - Seek/pause controls: Only work with file and URL sources
 * - Volume/mute: Work with any audio-capable source
 * - Color/flip: Work with all rendering modes
 *
 * **Thread safety:**
 * - Safe to call from render thread
 * - Uses RCU-protected options for updates
 * - No locking required (option system is thread-safe)
 *
 * @note In client mode, capture is NULL (no seek/pause available)
 * @note Unrecognized keys are silently ignored
 * @note Volume is clamped to [0.0, 2.0] range
 * @note Mute remembers the previous non-zero volume for unmute
 *
 * @ingroup session
 */
void session_handle_keyboard_input(session_capture_ctx_t *capture, int key);

/** @} */
/** @} */
