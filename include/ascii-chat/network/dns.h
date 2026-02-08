#pragma once

/**
 * @file network/dns.h
 * @brief 🌐 DNS resolution utilities
 * @ingroup network
 * @addtogroup network
 * @{
 *
 * Provides DNS resolution and connectivity testing utilities.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stdbool.h>

/**
 * @brief Test DNS connectivity by resolving a hostname
 *
 * Attempts to resolve the given hostname to verify DNS connectivity.
 * Uses a short timeout to avoid blocking.
 *
 * @param hostname Hostname to resolve (e.g., "api.github.com")
 * @return true if DNS resolution succeeds, false otherwise
 *
 * @note Logs warnings on failure
 * @note IPv4 resolution only (AF_INET)
 */
bool dns_test_connectivity(const char *hostname);

/** @} */
