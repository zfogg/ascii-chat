/**
 * @file connection_state.h
 * @brief Connection state machine for client TCP/WebSocket connections
 *
 * Re-exports the public connection attempt API from lib/network.
 * This is a compatibility header for client mode code that previously
 * had local definitions. All functionality has been moved to lib/network.
 *
 * @date January 2026
 * @version 2.0
 */

#pragma once

// Re-export public API from network library
#include <ascii-chat/network/connection_attempt.h>
