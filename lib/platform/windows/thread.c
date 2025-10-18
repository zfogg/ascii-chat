/**
 * @file thread.c
 * @brief Windows thread implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows Threading API wrappers for the platform abstraction layer,
 * enabling cross-platform thread management using a unified API.
 */

#ifdef _WIN32

#include <windows.h>
#include "../../common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "platform/thread.h"
#include "util/path.h"
#include <process.h>
#include <stdint.h>
#include <stdlib.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

// Thread wrapper structure to bridge POSIX and Windows thread APIs
typedef struct {
  void *(*posix_func)(void *);
  void *arg;
} thread_wrapper_t;

// Global to hold exception info from filter
static EXCEPTION_POINTERS *g_exception_pointers = NULL;

// Global symbol initialization
static BOOL g_symbols_initialized = FALSE;
static void initialize_symbol_handler(void) {
  if (!g_symbols_initialized) {
    HANDLE hProcess = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_NO_PROMPTS);
    SymCleanup(hProcess); // Clean any previous session
    if (SymInitialize(hProcess, NULL, TRUE)) {
      g_symbols_initialized = TRUE;
      log_debug("Symbol handler initialized at startup");
    }
  }
}

// Helper function to build consolidated exception message
static void build_exception_message(char *buffer, size_t buffer_size, DWORD exceptionCode, DWORD threadId) {
  const char *exceptionName = "UNKNOWN";
  switch (exceptionCode) {
  case EXCEPTION_ACCESS_VIOLATION:
    exceptionName = "ACCESS_VIOLATION (segfault)";
    break;
  case EXCEPTION_STACK_OVERFLOW:
    exceptionName = "STACK_OVERFLOW";
    break;
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    exceptionName = "DIVIDE_BY_ZERO";
    break;
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    exceptionName = "ILLEGAL_INSTRUCTION";
    break;
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    exceptionName = "ARRAY_BOUNDS_EXCEEDED";
    break;
  default:
    exceptionName = "UNKNOWN";
    break;
  }

  safe_snprintf(buffer, buffer_size,
                "====== EXCEPTION CAUGHT! ======\n"
                "Exception Code: 0x%lX\n"
                "Exception Type: %s\n"
                "Thread ID: %lu",
                (unsigned long)exceptionCode, exceptionName, threadId);
}

// Helper function to build stack trace message
static void build_stack_trace_message(char *buffer, size_t buffer_size, PCONTEXT pContext, HANDLE hProcess) {
  if (!pContext || IsBadReadPtr(pContext, sizeof(CONTEXT)) != 0) {
    safe_snprintf(buffer, buffer_size, "Exception occurred at: <invalid context>");
    return;
  }

  size_t offset = 0;

  // Exception location
  if (IsBadReadPtr(pContext, sizeof(CONTEXT)) == 0) {
    offset +=
        snprintf(buffer + offset, buffer_size - offset, "Exception occurred at:\n  RIP: 0x%016llX\n", pContext->Rip);
  } else {
    offset += snprintf(buffer + offset, buffer_size - offset, "Exception occurred at:\n  RIP: <invalid context>\n");
  }

  // Try to resolve the symbol at the crash address
  DWORD64 dwDisplacement = 0;
  char symBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
  PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symBuffer;
  pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  pSymbol->MaxNameLen = MAX_SYM_NAME;

  if (g_symbols_initialized && SymFromAddr(hProcess, pContext->Rip, &dwDisplacement, pSymbol)) {
    offset +=
        snprintf(buffer + offset, buffer_size - offset, "  Function: %s + 0x%llX\n", pSymbol->Name, dwDisplacement);
  } else {
    // Try to get module name
    HMODULE hModule;
    CHAR moduleName[MAX_PATH];
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCTSTR)pContext->Rip, &hModule)) {
      if (GetModuleFileNameA(hModule, moduleName, sizeof(moduleName))) {
        char *lastSlash = strrchr(moduleName, '\\');
        char *fileName = lastSlash ? lastSlash + 1 : moduleName;
        offset += snprintf(buffer + offset, buffer_size - offset, "  Module: %s + 0x%llX\n", fileName,
                           pContext->Rip - (DWORD64)hModule);
      }
    }
  }

  // Manual stack trace from RSP
  offset += snprintf(buffer + offset, buffer_size - offset, "\nMANUAL STACK TRACE - Walking from RSP\n");

  if (pContext->Rsp == 0 || IsBadReadPtr((void *)pContext->Rsp, sizeof(DWORD64) * 100) != 0) {
    offset += snprintf(buffer + offset, buffer_size - offset, "  Invalid stack pointer: 0x%016llX\n", pContext->Rsp);
    return;
  }

  DWORD64 *stackPtr = (DWORD64 *)pContext->Rsp;
  for (int i = 0; i < 50 && offset < buffer_size - 200; i++) {
    DWORD64 addr = 0;
    __try {
      addr = stackPtr[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      break;
    }

    if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
      DWORD64 displacement = 0;
      char symBuffer2[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
      PSYMBOL_INFO pSymbol2 = (PSYMBOL_INFO)symBuffer2;
      pSymbol2->SizeOfStruct = sizeof(SYMBOL_INFO);
      pSymbol2->MaxNameLen = MAX_SYM_NAME;

      if (SymFromAddr(hProcess, addr, &displacement, pSymbol2)) {
        IMAGEHLP_LINE64 line = {0};
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisplacement = 0;
        BOOL hasLineInfo = SymGetLineFromAddr64(hProcess, addr, &lineDisplacement, &line);

        BOOL isOurCode = FALSE;
        const char *relPath = NULL;
        if (hasLineInfo && line.FileName) {
          relPath = extract_project_relative_path(line.FileName);
          if (relPath != line.FileName && relPath && *relPath != '\0') {
            isOurCode = TRUE;
          }
        } else if (pSymbol2->Name[0] != '\0') {
          if (!strstr(pSymbol2->Name, "ntdll") && !strstr(pSymbol2->Name, "kernel32") &&
              !strstr(pSymbol2->Name, "ucrtbase") && !strstr(pSymbol2->Name, "msvcrt")) {
            isOurCode = TRUE;
          }
        }

        const char *prefix = isOurCode ? ">>>" : "  ";
        if (hasLineInfo) {
          const char *shortName =
              relPath ? relPath : (strrchr(line.FileName, '\\') ? strrchr(line.FileName, '\\') + 1 : line.FileName);
          offset += snprintf(buffer + offset, buffer_size - offset, "%s RSP+0x%03X: 0x%016llX %s + 0x%llX [%s:%lu]\n",
                             prefix, i * 8, addr, pSymbol2->Name, displacement, shortName, line.LineNumber);
        } else {
          offset += snprintf(buffer + offset, buffer_size - offset, "%s RSP+0x%03X: 0x%016llX %s + 0x%llX\n", prefix,
                             i * 8, addr, pSymbol2->Name, displacement);
        }
      } else {
        HMODULE hModule;
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCTSTR)addr, &hModule)) {
          CHAR moduleName[MAX_PATH];
          if (GetModuleFileNameA(hModule, moduleName, sizeof(moduleName))) {
            char *lastSlash = strrchr(moduleName, '\\');
            char *fileName = lastSlash ? lastSlash + 1 : moduleName;
            offset += snprintf(buffer + offset, buffer_size - offset, "  RSP+0x%03X: 0x%016llX %s + 0x%llX\n", i * 8,
                               addr, fileName, addr - (DWORD64)hModule);
          }
        }
      }
    }
  }

  // StackWalk64 trace
  offset += snprintf(buffer + offset, buffer_size - offset, "\nSTACK TRACE\n");
  STACKFRAME64 stackFrame = {0};
  stackFrame.AddrPC.Offset = pContext->Rip;
  stackFrame.AddrPC.Mode = AddrModeFlat;
  stackFrame.AddrStack.Offset = pContext->Rsp;
  stackFrame.AddrStack.Mode = AddrModeFlat;
  stackFrame.AddrFrame.Offset = pContext->Rbp;
  stackFrame.AddrFrame.Mode = AddrModeFlat;

  CONTEXT contextCopy = *pContext;
  HANDLE hThread = GetCurrentThread();

  for (int frameNum = 0; frameNum < 20 && offset < buffer_size - 200; frameNum++) {
    if (stackFrame.AddrStack.Offset == 0 || stackFrame.AddrStack.Offset > 0x7FFFFFFFFFFF) {
      offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d [Stack corrupted or end reached]\n", frameNum);
      break;
    }

    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &stackFrame, &contextCopy, NULL,
                     SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
      DWORD error = GetLastError();
      if (error != ERROR_SUCCESS && error != ERROR_NO_MORE_ITEMS) {
        offset +=
            snprintf(buffer + offset, buffer_size - offset, "  #%02d [StackWalk64 failed: %lu]\n", frameNum, error);
      }
      break;
    }

    if (stackFrame.AddrPC.Offset == 0) {
      break;
    }

    DWORD64 symDisplacement = 0;
    char symBuffer3[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSym = (PSYMBOL_INFO)symBuffer3;
    pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSym->MaxNameLen = MAX_SYM_NAME;

    if (g_symbols_initialized && SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &symDisplacement, pSym)) {
      IMAGEHLP_LINE64 line = {0};
      line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
      DWORD lineDisplacement = 0;
      if (SymGetLineFromAddr64(hProcess, stackFrame.AddrPC.Offset, &lineDisplacement, &line)) {
        const char *relPath = line.FileName ? extract_project_relative_path(line.FileName) : "unknown";
        offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d 0x%016llX %s + 0x%llX [%s:%lu]\n", frameNum,
                           stackFrame.AddrPC.Offset, pSym->Name, symDisplacement, relPath, line.LineNumber);
      } else {
        offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d 0x%016llX %s + 0x%llX\n", frameNum,
                           stackFrame.AddrPC.Offset, pSym->Name, symDisplacement);
      }
    } else {
      HMODULE hMod;
      CHAR modName[MAX_PATH];
      if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCTSTR)stackFrame.AddrPC.Offset, &hMod)) {
        if (GetModuleFileNameA(hMod, modName, sizeof(modName))) {
          char *slash = strrchr(modName, '\\');
          char *name = slash ? slash + 1 : modName;
          offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d 0x%016llX %s!0x%llX\n", frameNum,
                             stackFrame.AddrPC.Offset, name, stackFrame.AddrPC.Offset - (DWORD64)hMod);
        } else {
          offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d 0x%016llX ???\n", frameNum,
                             stackFrame.AddrPC.Offset);
        }
      } else {
        offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d 0x%016llX ???\n", frameNum,
                           stackFrame.AddrPC.Offset);
      }
    }
  }
}

// Exception filter that captures exception information
static LONG WINAPI exception_filter(EXCEPTION_POINTERS *exception_info) {
  // Store the exception info for use in the handler
  g_exception_pointers = exception_info;

  // Return EXCEPTION_EXECUTE_HANDLER to handle the exception
  return EXCEPTION_EXECUTE_HANDLER;
}

// Global flag to prevent recursive exception handling
static volatile int g_in_exception_handler = 0;

// Windows thread wrapper function that calls POSIX-style function
static DWORD WINAPI windows_thread_wrapper(LPVOID param) {
  thread_wrapper_t *wrapper = (thread_wrapper_t *)param;

  if (!wrapper) {
    log_error("THREAD_WRAPPER: NULL wrapper");
    return 1;
  }

  // Check if function pointer is valid
  if (!wrapper->posix_func) {
    log_error("THREAD_WRAPPER: NULL function pointer!");
    SAFE_FREE(wrapper);
    return 1;
  }

  void *result = NULL;
  __try {
    result = wrapper->posix_func(wrapper->arg);
  } __except (exception_filter(GetExceptionInformation())) {
    // Prevent recursive exception handling
    if (g_in_exception_handler) {
      printf("CRASH: Recursive exception detected, aborting\n");
      (void)fflush(stdout);
      return 1;
    }
    g_in_exception_handler = 1;
    DWORD exceptionCode = GetExceptionCode();
    DWORD threadId = GetCurrentThreadId();

    // Build consolidated exception message
    char exception_buffer[8192];
    build_exception_message(exception_buffer, sizeof(exception_buffer), exceptionCode, threadId);

    // Use direct printf instead of log_error to avoid logging system corruption
    printf("%s\n", exception_buffer);
    (void)fflush(stdout);

#ifndef NDEBUG
    // Build consolidated stack trace message
    if (g_exception_pointers && g_exception_pointers->ContextRecord) {
      // Add safety check for ContextRecord before using it
      if (IsBadReadPtr(g_exception_pointers->ContextRecord, sizeof(CONTEXT))) {
        printf("Exception context is invalid or corrupted\n");
        (void)fflush(stdout);
      } else {
        // Use minimal output to avoid any potential crashes
        __try {
          // Step 1: Minimal exception info
          __try {
            printf("EXCEPTION: Code=0x%08lX Thread=%lu\n",
                   (unsigned long)g_exception_pointers->ExceptionRecord->ExceptionCode, GetCurrentThreadId());
            (void)fflush(stdout);
          } __except (EXCEPTION_EXECUTE_HANDLER) {
            // If even printf crashes, just exit
            return 1;
          }

          // Step 2: Try to get RIP address safely
          PCONTEXT pContext = g_exception_pointers->ContextRecord;
          if (pContext && IsBadReadPtr(pContext, sizeof(CONTEXT)) == 0) {
            __try {
#ifdef _M_X64
              printf("RIP: 0x%016llX\n", pContext->Rip);
#else
              printf("EIP: 0x%08X\n", pContext->Eip);
#endif
              (void)fflush(stdout);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
              printf("RIP/EIP access failed\n");
              (void)fflush(stdout);
            }
          }

          // Step 3: Try to build stack trace safely
          __try {
            HANDLE hProcess = GetCurrentProcess();
            char stack_buffer[16384];
            memset(stack_buffer, 0, sizeof(stack_buffer));

            build_stack_trace_message(stack_buffer, sizeof(stack_buffer), pContext, hProcess);
            if (IsBadStringPtrA(stack_buffer, sizeof(stack_buffer))) {
              printf("Stack trace corrupted\n");
            } else {
              printf("%s", stack_buffer);
            }
            (void)fflush(stdout);
          } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("Stack trace failed\n");
            (void)fflush(stdout);
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          // If everything fails, just exit silently
          return 1;
        }
      }

    } else {
      printf("Could not get exception context\n");
      (void)fflush(stdout);
    }
#endif // !NDEBUG

    // Use raw free to match raw malloc used in allocation (see allocation comment above for rationale)
    // Safe to use in exception handler - doesn't touch g_mem.mutex
#ifdef DEBUG_MEMORY
#undef free
    free(wrapper);
#define SAFE_FREE(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
    SAFE_FREE(wrapper);
#endif
    // Reset the recursive exception flag
    g_in_exception_handler = 0;
    return 1;
  }

  // Use raw free to match raw malloc used in allocation (see allocation comment for rationale)
#ifdef DEBUG_MEMORY
#undef free
  free(wrapper);
#define SAFE_FREE(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
  SAFE_FREE(wrapper);
#endif
  return (DWORD)(uintptr_t)result;
}

/**
 * @brief Create a new thread
 * @param thread Pointer to thread structure to initialize
 * @param func Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, -1 on failure
 */
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg) {
  // Initialize symbol handler on first thread creation
  initialize_symbol_handler();

#ifdef DEBUG_THREADS
  log_debug("ENTER ascii_thread_create: thread=%p, func=%p, arg=%p", thread, func, arg);
  log_debug("About to malloc wrapper (size=%zu)", sizeof(thread_wrapper_t));
#endif

  // CRITICAL: Use raw malloc for the thread wrapper, not debug_malloc
  //
  // WHY: The wrapper must be freed in exception handlers (see __except blocks below).
  // If the thread crashes while holding g_mem.mutex (during any debug_malloc/debug_free),
  // the exception handler cannot safely call debug_free() as it would deadlock trying
  // to acquire the same mutex. Raw malloc/free bypass the debug tracking and are safe
  // to use in exception contexts.
  //
  // This is the ONLY allocation in the codebase that needs this special handling.
#ifdef DEBUG_MEMORY
#undef malloc
  thread_wrapper_t *wrapper = malloc(sizeof(thread_wrapper_t));
#define SAFE_MALLOC(size, type) debug_malloc(size, __FILE__, __LINE__)
#else
  thread_wrapper_t *wrapper = SAFE_MALLOC(sizeof(thread_wrapper_t), thread_wrapper_t *);
#endif

#ifdef DEBUG_THREADS
  log_debug("malloc returned wrapper=%p", wrapper);
#endif

  if (!wrapper) {
    log_error("malloc failed for thread wrapper");
    return -1;
  }

  wrapper->posix_func = func;
  wrapper->arg = arg;

  DWORD thread_id;

#ifdef DEBUG_THREADS
  log_debug("CREATE_THREAD: Before CreateThread: wrapper=%p, func=%p, arg=%p", wrapper, wrapper->posix_func,
            wrapper->arg);
#endif
  (*thread) = CreateThread(NULL, 0, windows_thread_wrapper, wrapper, 0, &thread_id);
#ifdef DEBUG_THREADS
  log_debug("CREATE_THREAD: After CreateThread: handle=%p, thread_id=%lu", *thread, thread_id);
#endif

  if (*thread == NULL) {
    SET_ERRNO_SYS(ERROR_THREAD, "CreateThread failed");
    // Use raw free - matches raw SAFE_MALLOC(see allocation comment for rationale, void *)
#ifdef DEBUG_MEMORY
#undef free
    free(wrapper);
#define SAFE_FREE(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
    SAFE_FREE(wrapper);
#endif
    return -1;
  }

  // IMPORTANT: Add a memory barrier to ensure all writes complete before returning
  MemoryBarrier();

  return 0;
}

/**
 * @brief Wait for a thread to complete and retrieve its return value
 * @param thread Pointer to thread to join
 * @param retval Pointer to store thread return value (can be NULL)
 * @return 0 on success, -1 on failure
 */
int ascii_thread_join(asciithread_t *thread, void **retval) {
  if (!thread || (*thread) == NULL || (*thread) == INVALID_HANDLE_VALUE) {
    SET_ERRNO(ERROR_THREAD, "Invalid thread handle for join operation");
    return -1;
  }

  DWORD result = WaitForSingleObject((*thread), INFINITE);

  if (result == WAIT_OBJECT_0) {
    if (retval) {
      DWORD exit_code;
      GetExitCodeThread((*thread), &exit_code);
      *retval = (void *)(uintptr_t)exit_code;
    }
    CloseHandle((*thread));
    *thread = NULL; // Clear the handle to prevent reuse
    return 0;
  }
  return -1;
}

/**
 * @brief Join a thread with timeout
 * @param thread Thread handle to join
 * @param retval Optional return value from thread
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on timeout/error
 */
int ascii_thread_join_timeout(asciithread_t *thread, void **retval, uint32_t timeout_ms) {
  DWORD result = WaitForSingleObject((*thread), timeout_ms);

  if (result == WAIT_OBJECT_0) {
    if (retval) {
      DWORD exit_code;
      GetExitCodeThread((*thread), &exit_code);
      *retval = (void *)(uintptr_t)exit_code;
    }
    CloseHandle((*thread));
    return 0;
  }

  if (result == WAIT_TIMEOUT) {
    return -2; // Special return code for timeout
  }

  return -1;
}

/**
 * @brief Exit the current thread with a return value
 * @param retval Return value for the thread
 */
void ascii_thread_exit(void *retval) {
  ExitThread((DWORD)(uintptr_t)retval);
}

/**
 * @brief Detach a thread, allowing it to run independently
 * @param thread Pointer to thread to detach
 * @return 0 on success, -1 on failure
 */
int ascii_thread_detach(asciithread_t *thread) {
  CloseHandle((*thread));
  return 0;
}

/**
 * @brief Get the current thread's ID
 * @return Thread ID structure for current thread
 */
thread_id_t ascii_thread_self(void) {
  thread_id_t id;
  id = GetCurrentThreadId();
  return id;
}

/**
 * @brief Compare two thread IDs for equality
 * @param t1 First thread ID
 * @param t2 Second thread ID
 * @return Non-zero if equal, 0 if different
 */
int ascii_thread_equal(thread_id_t t1, thread_id_t t2) {
  return t1 == t2;
}

/**
 * @brief Get current thread ID as a 64-bit integer
 * @return Current thread ID as uint64_t
 */
uint64_t ascii_thread_current_id(void) {
  return (uint64_t)GetCurrentThreadId();
}

bool ascii_thread_is_initialized(asciithread_t *thread) {
  if (!thread)
    return false;
  // On Windows, check if thread handle is valid (not NULL and not INVALID_HANDLE_VALUE)
  return (*thread != NULL && *thread != INVALID_HANDLE_VALUE);
}

void ascii_thread_init(asciithread_t *thread) {
  if (thread) {
    *thread = NULL; // On Windows, NULL is the uninitialized state
  }
}

#endif // _WIN32
