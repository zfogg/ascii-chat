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
#include "platform/thread.h"
#include "util/path.h"
#include <process.h>
#include <stdint.h>
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

// Exception filter that captures exception information
static LONG WINAPI exception_filter(EXCEPTION_POINTERS *exception_info) {
  // Store the exception info for use in the handler
  g_exception_pointers = exception_info;

  // Return EXCEPTION_EXECUTE_HANDLER to handle the exception
  return EXCEPTION_EXECUTE_HANDLER;
}

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
    free(wrapper);
    return 1;
  }

  void *result = NULL;
  __try {
    result = wrapper->posix_func(wrapper->arg);
  } __except (exception_filter(GetExceptionInformation())) {
    DWORD exceptionCode = GetExceptionCode();
    log_error("====== EXCEPTION CAUGHT! ======");
    log_error("Exception Code: 0x%lX", (unsigned long)exceptionCode);
    log_error("Thread ID: %lu", GetCurrentThreadId());

    // Decode common exception codes
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
    }
    log_error("Exception Type: %s", exceptionName);

#ifndef NDEBUG
    // Try to get a simple backtrace (Debug builds only)
    log_info("Attempting to get stack trace...");

    // Use the captured exception info from the filter
    if (g_exception_pointers && g_exception_pointers->ContextRecord) {
      PCONTEXT pContext = g_exception_pointers->ContextRecord;

#ifdef _M_X64
      log_error("Exception occurred at:");
      log_error("  RIP: 0x%016llX", pContext->Rip);

      // Try to resolve the symbol at the crash address
      HANDLE hProcess = GetCurrentProcess();
      DWORD64 dwDisplacement = 0;
      char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
      PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

      pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      pSymbol->MaxNameLen = MAX_SYM_NAME;

      // Make sure symbols are initialized (should already be done at thread creation)
      if (!g_symbols_initialized) {
        initialize_symbol_handler();
      }

      if (g_symbols_initialized && SymFromAddr(hProcess, pContext->Rip, &dwDisplacement, pSymbol)) {
        log_error("  Function: %s + 0x%llX", pSymbol->Name, dwDisplacement);
      } else {
        // Try to at least get the module name
        HMODULE hModule;
        CHAR moduleName[MAX_PATH];
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCTSTR)pContext->Rip, &hModule)) {
          if (GetModuleFileNameA(hModule, moduleName, sizeof(moduleName))) {
            char *lastSlash = strrchr(moduleName, '\\');
            char *fileName = lastSlash ? lastSlash + 1 : moduleName;
            log_error("  Module: %s + 0x%llX", fileName, pContext->Rip - (DWORD64)hModule);
          }
        }
      }

      // Now walk the stack from the exception context
      // Also try manual stack walk from raw addresses in case StackWalk64 fails
      log_info("MANUAL STACK TRACE - Walking from RSP");
      DWORD64 *stackPtr = (DWORD64 *)pContext->Rsp;
      for (int i = 0; i < 100; i++) { // Increased to walk further up the stack
        DWORD64 addr = 0;
        // Safe memory read with exception handling
        __try {
          addr = stackPtr[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          break;
        }

        // Check if this looks like a code address
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
          // Try to resolve symbol
          DWORD64 displacement = 0;
          char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
          PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
          pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
          pSymbol->MaxNameLen = MAX_SYM_NAME;

          if (SymFromAddr(hProcess, addr, &displacement, pSymbol)) {
            // Also try to get line info
            IMAGEHLP_LINE64 line = {0};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;
            BOOL hasLineInfo = SymGetLineFromAddr64(hProcess, addr, &lineDisplacement, &line);

            // Check if this is OUR code (not system libraries)
            BOOL isOurCode = FALSE;
            const char *relPath = NULL;
            if (hasLineInfo && line.FileName) {
              // Use shared path extraction - if it returns a relative path, it's our code
              relPath = extract_project_relative_path(line.FileName);
              // If the relative path is different from the full path, we found our project root
              if (relPath != line.FileName && relPath && *relPath != '\0') {
                isOurCode = TRUE;
              }
            } else if (pSymbol->Name[0] != '\0') {
              // Check symbol name for our functions
              if (!strstr(pSymbol->Name, "ntdll") && !strstr(pSymbol->Name, "kernel32") &&
                  !strstr(pSymbol->Name, "ucrtbase") && !strstr(pSymbol->Name, "msvcrt")) {
                isOurCode = TRUE;
              }
            }

            if (hasLineInfo) {
              // Use the relative path we already extracted if available, else just get the filename
              const char *shortName =
                  relPath ? relPath : (strrchr(line.FileName, '\\') ? strrchr(line.FileName, '\\') + 1 : line.FileName);
              if (isOurCode) {
                // Highlight our code with >>> prefix
                log_info(">>> RSP+0x%03X: 0x%016llX %s + 0x%llX [%s:%lu]", i * 8, addr, pSymbol->Name, displacement,
                         shortName, line.LineNumber);
              } else {
                log_info("  RSP+0x%03X: 0x%016llX %s + 0x%llX [%s:%lu]", i * 8, addr, pSymbol->Name, displacement,
                         shortName, line.LineNumber);
              }
            } else {
              if (isOurCode) {
                log_info(">>> RSP+0x%03X: 0x%016llX %s + 0x%llX", i * 8, addr, pSymbol->Name, displacement);
              } else {
                log_info("  RSP+0x%03X: 0x%016llX %s + 0x%llX", i * 8, addr, pSymbol->Name, displacement);
              }
            }
          } else {
            // Try to at least get module name
            HMODULE hModule;
            if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCTSTR)addr, &hModule)) {
              CHAR moduleName[MAX_PATH];
              if (GetModuleFileNameA(hModule, moduleName, sizeof(moduleName))) {
                char *lastSlash = strrchr(moduleName, '\\');
                char *fileName = lastSlash ? lastSlash + 1 : moduleName;
                log_info("  RSP+0x%03X: 0x%016llX %s + 0x%llX", i * 8, addr, fileName, addr - (DWORD64)hModule);
              }
            }
          }
        }
      }

      log_info("STACK TRACE");
      STACKFRAME64 stackFrame = {0};
      stackFrame.AddrPC.Offset = pContext->Rip;
      stackFrame.AddrPC.Mode = AddrModeFlat;
      stackFrame.AddrStack.Offset = pContext->Rsp;
      stackFrame.AddrStack.Mode = AddrModeFlat;
      stackFrame.AddrFrame.Offset = pContext->Rbp;
      stackFrame.AddrFrame.Mode = AddrModeFlat;

      // Create a copy of the context for stack walking
      CONTEXT contextCopy = *pContext;
      HANDLE hThread = GetCurrentThread();

      for (int frameNum = 0; frameNum < 50; frameNum++) {
        // Validate stack frame before walking to prevent crashes on corrupted stack
        if (stackFrame.AddrStack.Offset == 0 || stackFrame.AddrStack.Offset > 0x7FFFFFFFFFFF) {
          log_error("  #%02d [Stack corrupted or end reached]", frameNum);
          break;
        }

        // Use the context copy for stack walking
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &stackFrame, &contextCopy, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
          DWORD error = GetLastError();
          if (error != ERROR_SUCCESS && error != ERROR_NO_MORE_ITEMS) {
            log_error("  #%02d [StackWalk64 failed: %lu]", frameNum, error);
          }
          break;
        }

        if (stackFrame.AddrPC.Offset == 0) {
          break;
        }

        // Try to get symbol name
        DWORD64 symDisplacement = 0;
        char symBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSym = (PSYMBOL_INFO)symBuffer;
        pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSym->MaxNameLen = MAX_SYM_NAME;

        if (g_symbols_initialized && SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &symDisplacement, pSym)) {
          // Try to get source file and line info
          IMAGEHLP_LINE64 line = {0};
          line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
          DWORD lineDisplacement = 0;
          if (SymGetLineFromAddr64(hProcess, stackFrame.AddrPC.Offset, &lineDisplacement, &line)) {
            const char *relPath = line.FileName ? extract_project_relative_path(line.FileName) : "⁉️🤔";
            log_info("  #%02d 0x%016llX %s + 0x%llX [%s:%lu]", frameNum, stackFrame.AddrPC.Offset, pSym->Name,
                     symDisplacement, relPath, line.LineNumber);
          } else {
            log_info("  #%02d 0x%016llX %s + 0x%llX", frameNum, stackFrame.AddrPC.Offset, pSym->Name, symDisplacement);
          }
        } else {
          // Try to get module name at least
          HMODULE hMod;
          CHAR modName[MAX_PATH];
          if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCTSTR)stackFrame.AddrPC.Offset, &hMod)) {
            if (GetModuleFileNameA(hMod, modName, sizeof(modName))) {
              char *slash = strrchr(modName, '\\');
              char *name = slash ? slash + 1 : modName;
              log_info("  #%02d 0x%016llX %s!0x%llX", frameNum, stackFrame.AddrPC.Offset, name,
                       stackFrame.AddrPC.Offset - (DWORD64)hMod);
            } else {
              log_info("  #%02d 0x%016llX ???", frameNum, stackFrame.AddrPC.Offset);
            }
          } else {
            log_info("  #%02d 0x%016llX ???", frameNum, stackFrame.AddrPC.Offset);
          }
        }
      }
#else
      log_error("Exception stack trace not available on non-x64 platforms");
#endif // !!_M_X64
    } else {
      log_error("Could not get exception context");
    }
#endif // !NDEBUG

    log_error("================================");

    // Use raw free to match raw malloc used in allocation (see allocation comment above for rationale)
    // Safe to use in exception handler - doesn't touch g_mem.mutex
#ifdef DEBUG_MEMORY
#undef free
    free(wrapper);
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
    free(wrapper);
#endif
    return 1;
  }

  // Use raw free to match raw malloc used in allocation (see allocation comment for rationale)
#ifdef DEBUG_MEMORY
#undef free
  free(wrapper);
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
  free(wrapper);
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
  thread_wrapper_t *wrapper = (thread_wrapper_t *)malloc(sizeof(thread_wrapper_t));
#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#else
  thread_wrapper_t *wrapper;
  SAFE_MALLOC(wrapper, sizeof(thread_wrapper_t), thread_wrapper_t *);
#endif

#ifdef DEBUG_THREADS
  log_debug("malloc returned wrapper=%p", wrapper);
#endif

  if (!wrapper) {
    log_error("DEBUG: malloc failed for thread wrapper");
    return -1;
  }

  wrapper->posix_func = func;
  wrapper->arg = arg;

#ifdef DEBUG_THREADS
  log_info("DEBUG: About to call CreateThread");
#endif

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
    DWORD error = GetLastError();
    log_error("CREATE_THREAD: FAILED, error=%lu", error);
    // Use raw free - matches raw malloc (see allocation comment for rationale)
#ifdef DEBUG_MEMORY
#undef free
    free(wrapper);
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
    free(wrapper);
#endif
    return -1;
  }

#ifdef DEBUG_THREADS
  log_debug("DEBUG: CreateThread succeeded, handle=%p, thread_id=%lu", *thread, thread_id);
#endif

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
