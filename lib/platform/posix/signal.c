/**
 * @file platform/posix/signal.c
 * @brief POSIX crash signal handler implementation
 * @ingroup platform
 */

#ifndef _WIN32

#include <ascii-chat/signal.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>

/**
 * Global crash handler callback
 */
static platform_crash_handler_t g_crash_handler = NULL;

/**
 * POSIX signal handler wrapper
 * Invokes the registered crash handler callback
 */
static void signal_handler_wrapper(int sig) {
  if (g_crash_handler) {
    g_crash_handler(sig, NULL);
  }
}

/**
 * Install POSIX crash signal handler
 */
asciichat_error_t platform_install_crash_handler(platform_crash_handler_t handler) {
  struct sigaction sa, old_sa;

  /* Store the handler callback */
  g_crash_handler = handler;

  if (!handler) {
    /* Uninstalling - restore default behavior */
    return platform_uninstall_crash_handler();
  }

  /* Setup sigaction structure */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler_wrapper;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART; /* Restart interrupted syscalls */

  /* Install handlers for critical signals */
  const int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL};
  const int num_signals = sizeof(signals) / sizeof(signals[0]);

  for (int i = 0; i < num_signals; i++) {
    if (sigaction(signals[i], &sa, &old_sa) != 0) {
      log_error("Failed to install signal handler for signal %d", signals[i]);
      return SET_ERRNO_SYS(ERROR_SYSTEM, "sigaction() failed");
    }
  }

  log_debug("Installed POSIX crash signal handler");
  return ASCIICHAT_OK;
}

/**
 * Uninstall POSIX crash signal handler
 */
asciichat_error_t platform_uninstall_crash_handler(void) {
  struct sigaction sa;

  /* Reset to default signal handlers */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  const int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL};
  const int num_signals = sizeof(signals) / sizeof(signals[0]);

  for (int i = 0; i < num_signals; i++) {
    sigaction(signals[i], &sa, NULL);
  }

  g_crash_handler = NULL;
  log_debug("Uninstalled POSIX crash signal handler");
  return ASCIICHAT_OK;
}

/**
 * Get human-readable signal name
 */
const char *platform_signal_name(int signal) {
  switch (signal) {
  case SIGSEGV:
    return "SIGSEGV";
  case SIGABRT:
    return "SIGABRT";
  case SIGBUS:
    return "SIGBUS";
  case SIGILL:
    return "SIGILL";
  case SIGFPE:
    return "SIGFPE";
  case SIGSYS:
    return "SIGSYS";
  case SIGPIPE:
    return "SIGPIPE";
  case SIGTERM:
    return "SIGTERM";
  case SIGINT:
    return "SIGINT";
  case SIGHUP:
    return "SIGHUP";
  case SIGKILL:
    return "SIGKILL";
  case SIGUSR1:
    return "SIGUSR1";
  case SIGUSR2:
    return "SIGUSR2";
  case SIGCHLD:
    return "SIGCHLD";
  case SIGCONT:
    return "SIGCONT";
  case SIGSTOP:
    return "SIGSTOP";
  default:
    return "UNKNOWN_SIGNAL";
  }
}

#endif
