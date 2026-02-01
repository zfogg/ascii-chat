#pragma once

/**
 * @file platform/network.h
 * @brief Cross-platform network headers and socket definitions
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides unified network header includes across Windows and POSIX platforms.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

/** @} */
