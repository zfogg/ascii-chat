/**
 * @file endpoints.h
 * @brief Centralized definitions for all network service endpoints
 * @ingroup network
 *
 * This file consolidates all hardcoded service endpoints (discovery service,
 * STUN servers, TURN servers) in one place. Update these defines to change
 * the endpoints used throughout the application.
 */

#pragma once

/* =============================================================================
 * Discovery Service Endpoint
 * ============================================================================= */

/** Discovery service (ACDS) endpoint for session management */
#define ENDPOINT_DISCOVERY_SERVICE "discovery-service.ascii-chat.com"

/* =============================================================================
 * STUN Server Endpoints (Session Traversal Utilities for NAT)
 * ============================================================================= */

/** Primary STUN server (ascii-chat hosted) */
#define ENDPOINT_STUN_PRIMARY "stun:stun.ascii-chat.com:3478"

/** Fallback STUN server (Google public STUN) */
#define ENDPOINT_STUN_FALLBACK "stun:stun.l.google.com:19302"

/** Default STUN servers (comma-separated list) */
#define ENDPOINT_STUN_SERVERS_DEFAULT ENDPOINT_STUN_PRIMARY "," ENDPOINT_STUN_FALLBACK

/* =============================================================================
 * TURN Server Endpoints (Traversal Using Relays around NAT)
 * ============================================================================= */

/** Primary TURN server (ascii-chat hosted) */
#define ENDPOINT_TURN_PRIMARY "turn:turn.ascii-chat.com:3478"

/** Default TURN servers (comma-separated list) */
#define ENDPOINT_TURN_SERVERS_DEFAULT ENDPOINT_TURN_PRIMARY

/* =============================================================================
 * Individual STUN/TURN Components (for code that needs them separately)
 * ============================================================================= */

/** STUN server hostname only (without protocol/port) */
#define STUN_SERVER_HOST_PRIMARY "stun.ascii-chat.com"

/** STUN server port for primary server */
#define STUN_SERVER_PORT_PRIMARY 3478

/** Fallback STUN server hostname only */
#define STUN_SERVER_HOST_FALLBACK "stun.l.google.com"

/** Fallback STUN server port */
#define STUN_SERVER_PORT_FALLBACK 19302

/** TURN server hostname only */
#define TURN_SERVER_HOST "turn.ascii-chat.com"

/** TURN server port */
#define TURN_SERVER_PORT 3478
