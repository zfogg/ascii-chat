#pragma once

/**
 * @file platform/network.h
 * @brief Cross-platform network header consolidation
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Centralizes all network-related header includes that differ between platforms.
 * Replaces scattered #ifdef _WIN32 blocks in application code with a single
 * platform-aware include.
 *
 * Includes:
 * - Socket headers (winsock2.h on Windows, sys/socket.h on POSIX)
 * - DNS/resolution headers (ws2tcpip.h on Windows, netdb.h on POSIX)
 * - Address conversion (inet_ntop, inet_pton)
 * - TCP/IP definitions
 *
 * @note This header is included by lib/platform/socket.h already for socket ops.
 *       Application code should include this directly for DNS/address operations.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // For getaddrinfo(), gai_strerror(), inet_ntop(), TCP_NODELAY
#include <mmsystem.h> // For timeEndPeriod() (audio/timing)
#else
#include <netdb.h>     // For getaddrinfo(), gai_strerror(), freeaddrinfo()
#include <arpa/inet.h> // For inet_ntop(), inet_pton()
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY and other TCP options
#endif

/** @} */
