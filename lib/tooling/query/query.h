/**
 * @file tooling/query/query.h
 * @brief Runtime variable query tool API for debug builds
 *
 * This header provides the public C API for the query tool, which enables
 * runtime variable inspection via HTTP queries. The tool uses an external
 * LLDB process to attach to the running application and read variable values.
 *
 * Architecture:
 *   - Target process (ascii-chat) runs normally with debug symbols
 *   - Controller process (ascii-query-server) attaches via LLDB
 *   - HTTP server in controller accepts curl requests
 *   - When target is stopped at breakpoint, controller is still running
 *
 * Example usage:
 *   // In your application startup (debug builds only)
 *   int port = QUERY_INIT(9999);
 *   if (port > 0) {
 *       printf("Query server at http://localhost:%d\n", port);
 *   }
 *
 *   // ... application runs ...
 *
 *   // Query variables via curl:
 *   // curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count'
 *   // curl 'localhost:9999/query?file=src/server.c&line=100&name=client.socket.fd&break'
 *   // curl -X POST 'localhost:9999/continue'
 *
 *   // On shutdown
 *   QUERY_SHUTDOWN();
 *
 * Note: All functions and macros compile out completely in release builds
 * (when NDEBUG is defined). The query tool has zero runtime overhead in
 * production builds.
 *
 * @see docs/tooling/query.md for full documentation
 * @see docs/tooling/QUERY_TOOL_PLAN.md for implementation details
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the query tool by spawning the controller process
 *
 * This function spawns the ascii-query-server process, which attaches to
 * the current process via LLDB and starts an HTTP server on the specified port.
 *
 * The controller process is completely separate from the target process:
 * - Target can be stopped at breakpoints while controller serves HTTP
 * - Controller sends LLDB commands to read variables and control execution
 * - No instrumentation or code modification in the target is required
 *
 * @param preferred_port The port number for the HTTP server (e.g., 9999)
 * @return The actual port number on success, -1 on failure
 *
 * @note Only available in debug builds (NDEBUG not defined)
 * @note The controller may take a moment to attach; this function waits
 *       for the HTTP server to become ready before returning
 *
 * Platform notes:
 * - macOS: May require code signing with get-task-allow entitlement
 * - Linux: May require ptrace permissions (check /proc/sys/kernel/yama/ptrace_scope)
 * - Windows: Uses CreateProcess instead of fork/exec
 */
int query_init(int preferred_port);

/**
 * @brief Shutdown the query tool and terminate the controller process
 *
 * This function cleanly terminates the ascii-query-server process.
 * The target process continues running normally after shutdown.
 *
 * @note Safe to call even if query_init() was not called or failed
 * @note Only available in debug builds (NDEBUG not defined)
 */
void query_shutdown(void);

/**
 * @brief Check if the query tool controller is currently active
 *
 * @return true if the controller process is running and responsive
 * @return false if the controller is not running or not responding
 *
 * @note Only available in debug builds (NDEBUG not defined)
 */
bool query_is_active(void);

/**
 * @brief Get the port number of the active query server
 *
 * @return The port number if active, -1 if not active
 *
 * @note Only available in debug builds (NDEBUG not defined)
 */
int query_get_port(void);

/**
 * @name Convenience Macros
 *
 * These macros provide a convenient interface that compiles out completely
 * in release builds. Use these instead of calling the functions directly.
 *
 * @{
 */

#ifndef NDEBUG
/**
 * @brief Initialize query tool (debug builds only)
 * @param port Preferred HTTP server port
 * @return Port number on success, -1 on failure (or in release builds)
 */
#define QUERY_INIT(port) query_init(port)

/**
 * @brief Shutdown query tool (debug builds only)
 */
#define QUERY_SHUTDOWN() query_shutdown()

/**
 * @brief Check if query tool is active (debug builds only)
 * @return true if active, false otherwise (always false in release builds)
 */
#define QUERY_ACTIVE() query_is_active()

/**
 * @brief Get query server port (debug builds only)
 * @return Port number if active, -1 otherwise (always -1 in release builds)
 */
#define QUERY_PORT() query_get_port()

#else /* NDEBUG defined - release build */

#define QUERY_INIT(port) ((void)(port), -1)
#define QUERY_SHUTDOWN() ((void)0)
#define QUERY_ACTIVE() (false)
#define QUERY_PORT() (-1)

#endif /* NDEBUG */

/** @} */

#ifdef __cplusplus
}
#endif
