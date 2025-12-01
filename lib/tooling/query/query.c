/**
 * @file query.c
 * @brief Query tool runtime library implementation
 *
 * This provides the QUERY_INIT/QUERY_SHUTDOWN macros that applications use
 * to auto-spawn the query controller process.
 *
 * Platform support:
 * - Unix (macOS/Linux): Uses fork/exec to spawn controller
 * - Windows: Uses CreateProcess to spawn controller
 */

#include "query.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// Timeouts for HTTP health check
#define HEALTH_CHECK_TIMEOUT_MS 10000
#define HEALTH_CHECK_INTERVAL_MS 100
#define HEALTH_CHECK_CONNECT_TIMEOUT_MS 500

// Global state for the query tool runtime
static bool g_query_active = false;
static int g_query_port = -1;

#ifdef _WIN32
static HANDLE g_controller_handle = NULL;
static DWORD g_controller_pid = 0;
#else
static pid_t g_controller_pid = -1;
#endif

/**
 * @brief Try to connect to the HTTP server to check if it's ready
 * @param port Port number to check
 * @param timeout_ms Connection timeout in milliseconds
 * @return true if server is responding, false otherwise
 */
static bool try_http_connect(int port, int timeout_ms) {
#ifdef _WIN32
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    return false;
  }

  // Set non-blocking mode for timeout
  u_long mode = 1;
  ioctlsocket(sock, FIONBIO, &mode);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  connect(sock, (struct sockaddr *)&addr, sizeof(addr));

  // Use select for timeout
  fd_set writefds;
  FD_ZERO(&writefds);
  FD_SET(sock, &writefds);

  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int result = select(0, NULL, &writefds, NULL, &tv);
  closesocket(sock);

  return result > 0;
#else
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return false;
  }

  // Set socket timeout
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  close(sock);

  return result == 0;
#endif
}

/**
 * @brief Wait for the HTTP server to become ready
 * @param port Port number to check
 * @param timeout_ms Total timeout in milliseconds
 * @return true if server became ready, false on timeout
 */
static bool wait_for_http_ready(int port, int timeout_ms) {
  int elapsed = 0;

  while (elapsed < timeout_ms) {
    if (try_http_connect(port, HEALTH_CHECK_CONNECT_TIMEOUT_MS)) {
      return true;
    }

// Check if controller process is still alive
#ifdef _WIN32
    if (g_controller_handle != NULL) {
      DWORD exit_code;
      if (GetExitCodeProcess(g_controller_handle, &exit_code)) {
        if (exit_code != STILL_ACTIVE) {
          // Controller exited unexpectedly
          return false;
        }
      }
    }
#else
    if (g_controller_pid > 0) {
      int status;
      pid_t result = waitpid(g_controller_pid, &status, WNOHANG);
      if (result == g_controller_pid) {
        // Controller exited unexpectedly
        g_controller_pid = -1;
        return false;
      }
    }
#endif

// Sleep between checks
#ifdef _WIN32
    Sleep(HEALTH_CHECK_INTERVAL_MS);
#else
    usleep(HEALTH_CHECK_INTERVAL_MS * 1000);
#endif
    elapsed += HEALTH_CHECK_INTERVAL_MS;
  }

  return false;
}

/**
 * @brief Find the path to the query server executable
 * @param buffer Buffer to store the path
 * @param buffer_size Size of the buffer
 * @return true if found, false otherwise
 */
static bool find_query_server_path(char *buffer, size_t buffer_size) {
  // Try common locations relative to the executable
  const char *search_paths[] = {// Relative to build directory
                                ".deps-cache/query-tool/ascii-query-server",
                                "../.deps-cache/query-tool/ascii-query-server",
                                "../../.deps-cache/query-tool/ascii-query-server",
                                // In PATH
                                "ascii-query-server",
                                // Absolute paths for development
                                NULL};

  for (int i = 0; search_paths[i] != NULL; i++) {
#ifdef _WIN32
    // On Windows, append .exe
    snprintf(buffer, buffer_size, "%s.exe", search_paths[i]);
    if (GetFileAttributesA(buffer) != INVALID_FILE_ATTRIBUTES) {
      return true;
    }
#else
    snprintf(buffer, buffer_size, "%s", search_paths[i]);
    if (access(buffer, X_OK) == 0) {
      return true;
    }
#endif
  }

  // Try to find via environment variable
  const char *query_server_path = getenv("ASCIICHAT_QUERY_SERVER");
  if (query_server_path != NULL) {
    snprintf(buffer, buffer_size, "%s", query_server_path);
#ifdef _WIN32
    if (GetFileAttributesA(buffer) != INVALID_FILE_ATTRIBUTES) {
      return true;
    }
#else
    if (access(buffer, X_OK) == 0) {
      return true;
    }
#endif
  }

  return false;
}

int query_init(int preferred_port) {
  // Already initialized?
  if (g_query_active) {
    return g_query_port;
  }

  // Find the query server executable
  char server_path[1024];
  if (!find_query_server_path(server_path, sizeof(server_path))) {
    fprintf(stderr, "[query] Could not find ascii-query-server executable\n");
    fprintf(stderr, "[query] Set ASCIICHAT_QUERY_SERVER environment variable or ensure "
                    "it's in .deps-cache/query-tool/\n");
    return -1;
  }

#ifdef _WIN32
  // Windows implementation using CreateProcess
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    fprintf(stderr, "[query] WSAStartup failed\n");
    return -1;
  }

  char cmdline[2048];
  snprintf(cmdline, sizeof(cmdline), "\"%s\" --attach %lu --port %d", server_path, (unsigned long)GetCurrentProcessId(),
           preferred_port);

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi));

  // Create the controller process
  if (!CreateProcessA(NULL,               // Use command line
                      cmdline,            // Command line
                      NULL,               // Process security attributes
                      NULL,               // Thread security attributes
                      FALSE,              // Don't inherit handles
                      CREATE_NEW_CONSOLE, // Creation flags
                      NULL,               // Use parent's environment
                      NULL,               // Use parent's directory
                      &si, &pi)) {
    fprintf(stderr, "[query] Failed to start query server: error %lu\n", GetLastError());
    return -1;
  }

  g_controller_handle = pi.hProcess;
  g_controller_pid = pi.dwProcessId;
  CloseHandle(pi.hThread); // Don't need the thread handle

  fprintf(stderr, "[query] Started query server (PID %lu) on port %d\n", (unsigned long)g_controller_pid,
          preferred_port);

#else
  // Unix implementation using fork/exec
  pid_t self_pid = getpid();

  char port_str[16];
  char pid_str[16];
  snprintf(port_str, sizeof(port_str), "%d", preferred_port);
  snprintf(pid_str, sizeof(pid_str), "%d", self_pid);

  pid_t child = fork();
  if (child < 0) {
    fprintf(stderr, "[query] fork() failed: %s\n", strerror(errno));
    return -1;
  }

  if (child == 0) {
    // Child process: exec the controller
    // Redirect stdout/stderr to /dev/null or a log file to avoid clutter
    // (The controller has its own logging)

    execl(server_path, "ascii-query-server", "--attach", pid_str, "--port", port_str, (char *)NULL);

    // If exec fails
    fprintf(stderr, "[query] exec(%s) failed: %s\n", server_path, strerror(errno));
    _exit(1);
  }

  // Parent process
  g_controller_pid = child;
  fprintf(stderr, "[query] Started query server (PID %d) on port %d\n", child, preferred_port);
#endif

  // Wait for the HTTP server to become ready
  fprintf(stderr, "[query] Waiting for HTTP server to be ready...\n");
  if (!wait_for_http_ready(preferred_port, HEALTH_CHECK_TIMEOUT_MS)) {
    fprintf(stderr, "[query] Timeout waiting for query server to start\n");
    query_shutdown();
    return -1;
  }

  g_query_active = true;
  g_query_port = preferred_port;

  fprintf(stderr, "[query] Query server ready at http://localhost:%d\n", preferred_port);
  return preferred_port;
}

void query_shutdown(void) {
  if (!g_query_active && g_controller_pid <= 0) {
    return;
  }

  fprintf(stderr, "[query] Shutting down query server...\n");

#ifdef _WIN32
  if (g_controller_handle != NULL) {
    // Send termination signal
    TerminateProcess(g_controller_handle, 0);

    // Wait for process to exit (with timeout)
    WaitForSingleObject(g_controller_handle, 3000);

    CloseHandle(g_controller_handle);
    g_controller_handle = NULL;
    g_controller_pid = 0;
  }
#else
  if (g_controller_pid > 0) {
    // Send SIGTERM for graceful shutdown
    kill(g_controller_pid, SIGTERM);

    // Wait for process to exit (with timeout)
    int status;
    int wait_count = 0;
    while (wait_count < 30) { // 3 second timeout
      pid_t result = waitpid(g_controller_pid, &status, WNOHANG);
      if (result == g_controller_pid) {
        break;
      }
      if (result < 0) {
        break;
      }
      usleep(100000); // 100ms
      wait_count++;
    }

    // If still running, force kill
    if (wait_count >= 30) {
      kill(g_controller_pid, SIGKILL);
      waitpid(g_controller_pid, &status, 0);
    }

    g_controller_pid = -1;
  }
#endif

  g_query_active = false;
  g_query_port = -1;

  fprintf(stderr, "[query] Query server stopped\n");
}

bool query_is_active(void) {
  if (!g_query_active) {
    return false;
  }

// Verify the controller is still running
#ifdef _WIN32
  if (g_controller_handle != NULL) {
    DWORD exit_code;
    if (GetExitCodeProcess(g_controller_handle, &exit_code)) {
      if (exit_code != STILL_ACTIVE) {
        g_query_active = false;
        g_controller_handle = NULL;
        g_controller_pid = 0;
        return false;
      }
    }
  }
#else
  if (g_controller_pid > 0) {
    // Check if process is still alive
    if (kill(g_controller_pid, 0) != 0) {
      g_query_active = false;
      g_controller_pid = -1;
      return false;
    }
  }
#endif

  return g_query_active;
}

int query_get_port(void) {
  return g_query_port;
}
