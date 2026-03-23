/**
 * @file connection_factory.h
 * @brief Shared connection factory for TCP and WebSocket endpoints
 */
#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/connection_endpoint.h>

/**
 * Open a transport for a resolved endpoint.
 *
 * The factory hides the backend-specific setup details:
 * - TCP endpoints are resolved and connected with timeout handling
 * - WebSocket endpoints use the WebSocket transport constructor
 *
 * @param name Transport/debug name
 * @param input Original endpoint string
 * @param default_port Fallback port for bare hostnames
 * @param crypto_ctx Optional crypto context to attach to the transport
 * @param transport_out Receives the connected transport on success
 * @param endpoint_out Optional normalized endpoint output
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t connection_factory_open(const char *name, const char *input, uint16_t default_port,
                                          crypto_context_t *crypto_ctx, acip_transport_t **transport_out,
                                          connection_endpoint_t *endpoint_out);
