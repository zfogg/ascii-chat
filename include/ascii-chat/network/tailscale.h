#pragma once

/**
 * @file network/tailscale.h
 * @brief Tailscale host identity helpers
 * @ingroup network
 */

#include <stdbool.h>

/**
 * @brief Determine whether an address belongs to the local Tailscale tailnet.
 *
 * A fully-qualified `.ts.net` name is accepted directly. Literal IP addresses
 * are reverse-resolved, while short hostnames are forward-resolved and then
 * reverse-checked. The resolved name must end in `.ts.net`.
 */
bool is_tailscale_host(const char *address);
