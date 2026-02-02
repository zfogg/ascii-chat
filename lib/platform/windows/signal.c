/**
 * @file platform/windows/signal.c
 * @brief Windows exception handler implementation
 * @ingroup platform
 */

#ifdef _WIN32

#include <ascii-chat/signal.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/common.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

/**
 * Global crash handler callback
 */
static platform_crash_handler_t g_crash_handler = NULL;

/**
 * Previous unhandled exception filter (for restoration)
 */
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_exception_filter = NULL;

/**
 * Windows exception handler wrapper
 * Converts Windows exceptions to our callback mechanism
 */
static LONG WINAPI exception_handler_wrapper(PEXCEPTION_POINTERS exc_info) {
  if (g_crash_handler && exc_info) {
    /* Map Windows exception codes to pseudo-signal numbers */
    int signal_num = SIGABRT; /* Default */

    switch (exc_info->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
      signal_num = SIGSEGV; /* Memory access violation */
      break;
    case EXCEPTION_STACK_OVERFLOW:
      signal_num = SIGSEGV; /* Stack overflow treated as segmentation fault */
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      signal_num = SIGILL; /* Illegal instruction */
      break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      signal_num = SIGABRT; /* Non-continuable exception */
      break;
    default:
      signal_num = SIGABRT; /* Generic abort for other exceptions */
      break;
    }

    /* Invoke the handler with exception context */
    g_crash_handler(signal_num, (void *)exc_info);
  }

  /* Return EXCEPTION_CONTINUE_SEARCH to use default handler and terminate */
  return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * Install Windows crash exception handler
 */
asciichat_error_t platform_install_crash_handler(platform_crash_handler_t handler) {
  if (!handler) {
    /* Uninstalling - restore previous filter */
    return platform_uninstall_crash_handler();
  }

  /* Store the handler callback */
  g_crash_handler = handler;

  /* Save previous filter and install ours */
  g_prev_exception_filter = SetUnhandledExceptionFilter(exception_handler_wrapper);

  if (!g_prev_exception_filter) {
    log_warn("SetUnhandledExceptionFilter returned NULL");
  }

  log_debug("Installed Windows crash exception handler");
  return ASCIICHAT_OK;
}

/**
 * Uninstall Windows crash exception handler
 */
asciichat_error_t platform_uninstall_crash_handler(void) {
  /* Restore the previous exception filter */
  if (g_prev_exception_filter) {
    SetUnhandledExceptionFilter(g_prev_exception_filter);
    g_prev_exception_filter = NULL;
  } else {
    /* Restore to default handler if we don't have the previous one */
    SetUnhandledExceptionFilter(NULL);
  }

  g_crash_handler = NULL;
  log_debug("Uninstalled Windows crash exception handler");
  return ASCIICHAT_OK;
}

/**
 * Get human-readable name for Windows exception codes
 */
const char *platform_signal_name(int signal) {
  switch (signal) {
  case SIGSEGV:
    return "SIGSEGV (Access Violation)";
  case SIGILL:
    return "SIGILL (Illegal Instruction)";
  case SIGABRT:
    return "SIGABRT (Abnormal Termination)";
  case SIGFPE:
    return "SIGFPE (Floating Point)";
  case SIGTERM:
    return "SIGTERM (Termination)";
  case SIGINT:
    return "SIGINT (Interrupt)";
  default:
    return "UNKNOWN_SIGNAL";
  }
}

#endif
