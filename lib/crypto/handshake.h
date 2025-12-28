#pragma once

/**
 * @file crypto/handshake.h
 * @brief Cryptographic handshake - common types and shared functions
 * @ingroup handshake
 *
 * This header provides the core handshake types and common functions shared
 * between client and server implementations.
 *
 * For server-specific functions, include: crypto/handshake/server.h
 * For client-specific functions, include: crypto/handshake/client.h
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

// Include only the common module - no compatibility wrapper
#include "handshake/common.h"
