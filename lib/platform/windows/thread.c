/**
 * @file thread.c
 * @brief Windows thread implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows Threading API wrappers for the platform abstraction layer,
 * enabling cross-platform thread management using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "../../common.h"
#include <windows.h>
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
static EXCEPTION_POINTERS* g_exception_pointers = NULL;

// Global symbol initialization
static BOOL g_symbols_initialized = FALSE;
static void initialize_symbol_handler(void) {
  if (!g_symbols_initialized) {
    HANDLE hProcess = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES |
                 SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    SymCleanup(hProcess);  // Clean any previous session
    if (SymInitialize(hProcess, NULL, TRUE)) {
      g_symbols_initialized = TRUE;
      printf("[DEBUG] Symbol handler initialized at startup\n");
      fflush(stdout);
    }
  }
}

// Exception filter that captures exception information
static LONG WINAPI exception_filter(EXCEPTION_POINTERS* exception_info) {
  // Store the exception info for use in the handler
  g_exception_pointers = exception_info;

  // Return EXCEPTION_EXECUTE_HANDLER to handle the exception
  return EXCEPTION_EXECUTE_HANDLER;
}

// Windows thread wrapper function that calls POSIX-style function
static DWORD WINAPI windows_thread_wrapper(LPVOID param) {
  thread_wrapper_t *wrapper = (thread_wrapper_t *)param;

  if (!wrapper) {
    printf("[THREAD_WRAPPER] ERROR: NULL wrapper\n");
    fflush(stdout);
    return 1;
  }

  printf("[THREAD_WRAPPER] wrapper=%p, func=%p, arg=%p, thread_id=%lu\n", wrapper, wrapper->posix_func, wrapper->arg,
         GetCurrentThreadId());
  fflush(stdout);

  // Check if function pointer is valid
  if (!wrapper->posix_func) {
    printf("[THREAD_WRAPPER] ERROR: NULL function pointer!\n");
    fflush(stdout);
    free(wrapper);
    return 1;
  }

  printf("[THREAD_WRAPPER] About to call POSIX function...\n");
  fflush(stdout);

  void *result = NULL;
  __try {
    result = wrapper->posix_func(wrapper->arg);
  } __except (exception_filter(GetExceptionInformation())) {
    DWORD exceptionCode = GetExceptionCode();
    printf("\n[THREAD_WRAPPER] ====== EXCEPTION CAUGHT! ======\n");
    printf("[THREAD_WRAPPER] Exception Code: 0x%lX\n", (unsigned long)exceptionCode);
    printf("[THREAD_WRAPPER] Thread ID: %lu\n", GetCurrentThreadId());

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
    printf("[THREAD_WRAPPER] Exception Type: %s\n", exceptionName);

    // Try to get a simple backtrace
    printf("[THREAD_WRAPPER] Attempting to get stack trace...\n");
    fflush(stdout);

    // Use the captured exception info from the filter
    if (g_exception_pointers && g_exception_pointers->ContextRecord) {
      PCONTEXT pContext = g_exception_pointers->ContextRecord;

      printf("[THREAD_WRAPPER] Exception occurred at:\n");
#ifdef _M_X64
      printf("  RIP: 0x%016llX\n", pContext->Rip);

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
        printf("  Function: %s + 0x%llX\n", pSymbol->Name, dwDisplacement);
      } else {
        // Try to at least get the module name
        HMODULE hModule;
        CHAR moduleName[MAX_PATH];
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             (LPCTSTR)pContext->Rip, &hModule)) {
          if (GetModuleFileNameA(hModule, moduleName, sizeof(moduleName))) {
            char* lastSlash = strrchr(moduleName, '\\');
            char* fileName = lastSlash ? lastSlash + 1 : moduleName;
            printf("  Module: %s + 0x%llX\n", fileName, pContext->Rip - (DWORD64)hModule);
          }
        }
      }

      // Now walk the stack from the exception context
      // Also try manual stack walk from raw addresses in case StackWalk64 fails
      printf("\n[MANUAL STACK TRACE - Walking from RSP]\n");
      DWORD64* stackPtr = (DWORD64*)pContext->Rsp;
      for (int i = 0; i < 100; i++) {  // Increased to walk further up the stack
        DWORD64 addr = 0;
        // Safe memory read with exception handling
        __try {
          addr = stackPtr[i];
        } __except(EXCEPTION_EXECUTE_HANDLER) {
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
            if (hasLineInfo && line.FileName) {
              const char* fileName = line.FileName;
              // Check if path contains our source directories
              if (strstr(fileName, "\\src\\") || strstr(fileName, "\\lib\\") ||
                  strstr(fileName, "ascii-chat")) {
                isOurCode = TRUE;
              }
            } else if (pSymbol->Name) {
              // Check symbol name for our functions
              if (!strstr(pSymbol->Name, "ntdll") && !strstr(pSymbol->Name, "kernel32") &&
                  !strstr(pSymbol->Name, "ucrtbase") && !strstr(pSymbol->Name, "msvcrt")) {
                isOurCode = TRUE;
              }
            }

            if (hasLineInfo) {
              const char* shortName = strrchr(line.FileName, '\\') ? strrchr(line.FileName, '\\') + 1 : line.FileName;
              if (isOurCode) {
                // Highlight our code with >>> prefix
                printf(">>> RSP+0x%03X: 0x%016llX %s + 0x%llX [%s:%lu]\n",
                       i*8, addr, pSymbol->Name, displacement, shortName, line.LineNumber);
              } else {
                printf("  RSP+0x%03X: 0x%016llX %s + 0x%llX [%s:%lu]\n",
                       i*8, addr, pSymbol->Name, displacement, shortName, line.LineNumber);
              }
            } else {
              if (isOurCode) {
                printf(">>> RSP+0x%03X: 0x%016llX %s + 0x%llX\n", i*8, addr, pSymbol->Name, displacement);
              } else {
                printf("  RSP+0x%03X: 0x%016llX %s + 0x%llX\n", i*8, addr, pSymbol->Name, displacement);
              }
            }
          } else {
            // Try to at least get module name
            HMODULE hModule;
            if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 (LPCTSTR)addr, &hModule)) {
              CHAR moduleName[MAX_PATH];
              if (GetModuleFileNameA(hModule, moduleName, sizeof(moduleName))) {
                char* lastSlash = strrchr(moduleName, '\\');
                char* fileName = lastSlash ? lastSlash + 1 : moduleName;
                printf("  RSP+0x%03X: 0x%016llX %s + 0x%llX\n",
                       i*8, addr, fileName, addr - (DWORD64)hModule);
              }
            }
          }
        }
      }

      printf("\n[STACK TRACE]\n");
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
          printf("  #%02d [Stack corrupted or end reached]\n", frameNum);
          break;
        }

        // Use the context copy for stack walking
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &stackFrame, &contextCopy,
                        NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
          DWORD error = GetLastError();
          if (error != ERROR_SUCCESS && error != ERROR_NO_MORE_ITEMS) {
            printf("  #%02d [StackWalk64 failed: %lu]\n", frameNum, error);
          }
          break;
        }

        if (stackFrame.AddrPC.Offset == 0) {
          break;
        }

        printf("  #%02d 0x%016llX ", frameNum, stackFrame.AddrPC.Offset);

        // Try to get symbol name
        DWORD64 symDisplacement = 0;
        char symBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSym = (PSYMBOL_INFO)symBuffer;
        pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSym->MaxNameLen = MAX_SYM_NAME;

        if (g_symbols_initialized && SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &symDisplacement, pSym)) {
          printf("%s + 0x%llX", pSym->Name, symDisplacement);

          // Try to get source file and line info
          IMAGEHLP_LINE64 line = {0};
          line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
          DWORD lineDisplacement = 0;
          if (SymGetLineFromAddr64(hProcess, stackFrame.AddrPC.Offset, &lineDisplacement, &line)) {
            printf(" [%s:%lu]", line.FileName ? strrchr(line.FileName, '\\') + 1 : "??", line.LineNumber);
          }
        } else {
          // Try to get module name at least
          HMODULE hMod;
          CHAR modName[MAX_PATH];
          if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCTSTR)stackFrame.AddrPC.Offset, &hMod)) {
            if (GetModuleFileNameA(hMod, modName, sizeof(modName))) {
              char* slash = strrchr(modName, '\\');
              char* name = slash ? slash + 1 : modName;
              printf("%s!0x%llX", name, stackFrame.AddrPC.Offset - (DWORD64)hMod);
            } else {
              printf("???");
            }
          } else {
            printf("???");
          }
        }
        printf("\n");
      }
      printf("\n");
#endif
    } else {
      printf("[THREAD_WRAPPER] Could not get exception context\n");
    }

    printf("[THREAD_WRAPPER] ================================\n");
    fflush(stdout);

    // Use raw free to match raw malloc used in allocation
#ifdef DEBUG_MEMORY
#undef free
    free(wrapper);
#define free(ptr) debug_free(ptr, __FILE__, __LINE__)
#else
    free(wrapper);
#endif
    return 1;
  }

  printf("[THREAD_WRAPPER] Function returned: %p\n", result);
  fflush(stdout);

  // Use raw free to match raw malloc used in allocation
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

  printf("ENTER ascii_thread_create: thread=%p, func=%p, arg=%p\n", thread, func, arg);
  fflush(stdout);

#ifdef DEBUG_THREADS
  OutputDebugStringA("DEBUG: ascii_thread_create() called\n");
#endif

  printf("DEBUG: About to malloc wrapper (size=%zu)\n", sizeof(thread_wrapper_t));
  fflush(stdout);

  // CRITICAL: Use real malloc, not debug_malloc to avoid deadlock during thread creation
#ifdef DEBUG_MEMORY
#undef malloc
  thread_wrapper_t *wrapper = (thread_wrapper_t *)malloc(sizeof(thread_wrapper_t));
#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#else
  thread_wrapper_t *wrapper;
  SAFE_MALLOC(wrapper, sizeof(thread_wrapper_t), thread_wrapper_t *);
#endif

  printf("DEBUG: malloc returned wrapper=%p\n", wrapper);
  fflush(stdout);
  if (!wrapper) {
#ifdef DEBUG_THREADS
    OutputDebugStringA("DEBUG: malloc failed for thread wrapper\n");
#endif
    return -1;
  }

  wrapper->posix_func = func;
  wrapper->arg = arg;

#ifdef DEBUG_THREADS
  OutputDebugStringA("DEBUG: About to call CreateThread\n");
#endif

  DWORD thread_id;

  printf("[CREATE_THREAD] Before CreateThread: wrapper=%p, func=%p, arg=%p\n", wrapper, wrapper->posix_func,
         wrapper->arg);
  fflush(stdout);

  (*thread) = CreateThread(NULL, 0, windows_thread_wrapper, wrapper, 0, &thread_id);

  printf("[CREATE_THREAD] After CreateThread: handle=%p, thread_id=%lu\n", *thread, thread_id);
  fflush(stdout);

  if (*thread == NULL) {
    DWORD error = GetLastError();
    printf("[CREATE_THREAD] FAILED, error=%lu\n", error);
    fflush(stdout);
#ifdef DEBUG_THREADS
    char debug_msg[256];
    SAFE_SNPRINTF(debug_msg, 256, "DEBUG: CreateThread failed, error=%lu\n", error);
    OutputDebugStringA(debug_msg);
#endif
    free(wrapper);
    return -1;
  }

  printf("[CREATE_THREAD] SUCCESS: handle=%p, thread_id=%lu\n", *thread, thread_id);
  fflush(stdout);

#ifdef DEBUG_THREADS
  char debug_msg[256];
  SAFE_SNPRINTF(debug_msg, 256, "DEBUG: CreateThread succeeded, handle=%p, thread_id=%lu\n", *thread, thread_id);
  OutputDebugStringA(debug_msg);
#endif

  printf("DEBUG: ascii_thread_create about to return 0\n");
  fflush(stdout);

  printf("DEBUG: Actually returning from ascii_thread_create now\n");
  fflush(stdout);

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
  } else if (result == WAIT_TIMEOUT) {
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
    *thread = NULL;  // On Windows, NULL is the uninitialized state
  }
}

#endif // _WIN32