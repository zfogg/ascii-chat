
/**
 * @file platform/abstraction.c
 * @ingroup platform
 * @brief 🏗️ Common platform abstraction stubs (OS-specific code in posix/ and windows/ subdirectories)
 */

#include <errno.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/common.h>

// ============================================================================
// Common Platform Functions
// ============================================================================
// This file is reserved for common platform functions that don't need
// OS-specific implementations. Currently all functions are OS-specific.
//
// The OS-specific implementations are in:
// - platform_windows.c (Windows)
// - platform_posix.c (POSIX/Unix/Linux/macOS)
//
// Socket-specific implementations are in:
// - platform/socket.c (Common socket utilities like socket_optimize_for_streaming)
//
// I/O and terminal functions are in:
// - platform/system.c (platform_write_all)
// - platform/terminal.c (terminal_should_use_control_sequences)
// ============================================================================
