/**
 * @file platform/windows/thread.c
 * @ingroup platform
 * @brief 🧵 Windows threading API implementation for cross-platform thread management
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

// Global symbol initialization for thread exception handling (separate from system.c)
static BOOL g_thread_symbols_initialized = FALSE;
static void initialize_symbol_handler(void) {
  if (!g_thread_symbols_initialized) {
    HANDLE hProcess = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_NO_PROMPTS | SYMOPT_AUTO_PUBLICS | SYMOPT_PUBLICS_ONLY);
    SymCleanup(hProcess); // Clean any previous session
    if (SymInitialize(hProcess, NULL, TRUE)) {
      // Add the current module's directory for symbol search
      CHAR exePath[MAX_PATH];
      if (GetModuleFileNameA(NULL, exePath, sizeof(exePath))) {
        char *lastSlash = strrchr(exePath, '\\');
        if (lastSlash) {
          *lastSlash = '\0';
          SymSetSearchPath(hProcess, exePath);
        }
      }
      g_thread_symbols_initialized = TRUE;
      log_debug("Symbol handler initialized at startup");
    }
  }
}

// Unused exception handling functions - kept for potential future debugging use
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
  if (!pContext) {
    safe_snprintf(buffer, buffer_size, "Exception occurred at: <invalid context - null pointer>");
    return;
  }

  // NOTE: pContext must already be a copy on a stable stack frame
  // The caller is responsible for copying CONTEXT from g_exception_pointers
  // before calling this function to avoid stack-use-after-scope issues
  PCONTEXT ctx = pContext;
  size_t offset = 0;

  // Exception location
  if (IsBadReadPtr(ctx, sizeof(CONTEXT)) == 0) {
    offset += snprintf(buffer + offset, buffer_size - offset, "Exception occurred at:\n  RIP: 0x%016llX\n", ctx->Rip);
  } else {
    offset += snprintf(buffer + offset, buffer_size - offset, "Exception occurred at:\n  RIP: <invalid context>\n");
  }

  // Try to resolve the symbol at the crash address - wrap in try/except for safety
  __try {
    DWORD64 dwDisplacement = 0;
    char symBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symBuffer;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;

    if (g_thread_symbols_initialized && SymFromAddr(hProcess, ctx->Rip, &dwDisplacement, pSymbol)) {
      offset +=
          snprintf(buffer + offset, buffer_size - offset, "  Function: %s + 0x%llX\n", pSymbol->Name, dwDisplacement);
    } else {
      // Try to get module name - wrap in try/except
      __try {
        HMODULE hModule;
        CHAR moduleName[MAX_PATH];
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCTSTR)ctx->Rip, &hModule)) {
          if (GetModuleFileNameA(hModule, moduleName, sizeof(moduleName))) {
            char *lastSlash = strrchr(moduleName, '\\');
            char *fileName = lastSlash ? lastSlash + 1 : moduleName;
            offset += snprintf(buffer + offset, buffer_size - offset, "  Module: %s + 0x%llX\n", fileName,
                               ctx->Rip - (DWORD64)hModule);
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Module lookup failed - just skip it
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Symbol resolution failed - just skip it and continue
  }

  // Manual stack trace from RSP
  // Note: This works with AddressSanitizer enabled - we use __try/__except to safely handle
  // any access violations when reading stack memory from the exception thread
  offset += snprintf(buffer + offset, buffer_size - offset, "\nMANUAL STACK TRACE - Walking from RSP\n");

  // Validate stack pointer before attempting to read from it
  // Check if the pointer is valid and in a reasonable address range
  BOOL canReadStack = FALSE;
  if (ctx->Rsp != 0 && ctx->Rsp > 0x10000 && ctx->Rsp < 0x7FFFFFFFFFFF) {
    __try {
      // Try to verify the pointer is readable using IsBadReadPtr
      // Note: IsBadReadPtr is deprecated but still works for this check
      if (IsBadReadPtr((void *)ctx->Rsp, sizeof(DWORD64)) == 0) {
        canReadStack = TRUE;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      // If IsBadReadPtr itself causes an exception, the pointer is definitely invalid
      canReadStack = FALSE;
    }
  }

  if (!canReadStack) {
    offset += snprintf(buffer + offset, buffer_size - offset, "  Invalid stack pointer: 0x%016llX\n", ctx->Rsp);
  } else {
    // Copy stack data to local buffer to avoid ASan stack-use-after-scope errors
    // We're reading from another thread's stack, so copy it to our stack frame first
    // IMPORTANT: Use ReadProcessMemory ONLY - never use direct memcpy on ctx->Rsp
    // Direct memcpy can crash if the stack pointer is invalid or points to unmapped memory
    DWORD64 stackData[50] = {0};
    SIZE_T bytesRead = 0;
    __try {
      // Read the stack memory safely using ReadProcessMemory
      // This is safe even if ctx->Rsp points to invalid memory - ReadProcessMemory will return FALSE
      if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)ctx->Rsp, stackData, sizeof(stackData), &bytesRead)) {
        // ReadProcessMemory failed - stack pointer might be invalid or point to unmapped memory
        // Don't try direct access (memcpy) as that would crash - just skip manual walking
        bytesRead = 0;
      }
      // Only use bytesRead if ReadProcessMemory succeeded
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      // If ReadProcessMemory itself causes an exception (unlikely but possible), skip manual walking
      bytesRead = 0;
    }

    if (bytesRead > 0) {
      size_t numEntries = bytesRead / sizeof(DWORD64);
      for (size_t i = 0; i < numEntries && i < 50 && offset < buffer_size - 200; i++) {
        DWORD64 addr = stackData[i];

        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
          DWORD64 displacement = 0;
          char symBuffer2[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
          PSYMBOL_INFO pSymbol2 = (PSYMBOL_INFO)symBuffer2;
          pSymbol2->SizeOfStruct = sizeof(SYMBOL_INFO);
          pSymbol2->MaxNameLen = MAX_SYM_NAME;

          // Try to resolve symbol - check if symbols are initialized first
          BOOL symbolResolved = FALSE;
          if (g_thread_symbols_initialized && SymFromAddr(hProcess, addr, &displacement, pSymbol2)) {
            symbolResolved = TRUE;
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
              offset +=
                  snprintf(buffer + offset, buffer_size - offset, "%s RSP+0x%03zX: 0x%016llX %s + 0x%llX [%s:%lu]\n",
                           prefix, (size_t)(i * 8), addr, pSymbol2->Name, displacement, shortName, line.LineNumber);
            } else {
              offset += snprintf(buffer + offset, buffer_size - offset, "%s RSP+0x%03zX: 0x%016llX %s + 0x%llX\n",
                                 prefix, (size_t)(i * 8), addr, pSymbol2->Name, displacement);
            }
          }

          // If symbol resolution failed, try to get module name or print raw address
          if (!symbolResolved) {
            HMODULE hModule = NULL;
            CHAR moduleName[MAX_PATH] = {0};
            BOOL foundModule = FALSE;

            // First try GetModuleHandleEx (fast, works for most cases)
            if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCTSTR)addr, &hModule)) {
              foundModule = GetModuleFileNameA(hModule, moduleName, sizeof(moduleName)) > 0;
            } else if (g_thread_symbols_initialized) {
              // Fallback: Try SymGetModuleBase64 to find which module this address belongs to
              DWORD64 moduleBase = SymGetModuleBase64(hProcess, addr);
              if (moduleBase != 0) {
                // Found module base - try to get handle from base address
                HMODULE hModFromBase = (HMODULE)(uintptr_t)moduleBase;
                if (GetModuleFileNameA(hModFromBase, moduleName, sizeof(moduleName)) > 0) {
                  hModule = hModFromBase;
                  foundModule = TRUE;
                }
              }
            }

            if (foundModule) {
              char *lastSlash = strrchr(moduleName, '\\');
              char *fileName = lastSlash ? lastSlash + 1 : moduleName;
              DWORD64 offsetInModule = addr - (DWORD64)(uintptr_t)hModule;
              // Show module name with offset
              offset += snprintf(buffer + offset, buffer_size - offset, "  RSP+0x%03zX: 0x%016llX %s + 0x%llX\n",
                                 (size_t)(i * 8), addr, fileName, offsetInModule);
            } else {
              // Can't resolve symbol or module - still show address for manual resolution with llvm-symbolizer
              offset += snprintf(buffer + offset, buffer_size - offset, "  RSP+0x%03zX: 0x%016llX <unresolved>\n",
                                 (size_t)(i * 8), addr);
            }
          }
        } // Close if (addr > 0x10000...)
      } // Close for loop
    } // Close if (bytesRead > 0)
  } // Close else block

  // StackWalk64 trace
  offset += snprintf(buffer + offset, buffer_size - offset, "\nSTACK TRACE\n");
  STACKFRAME64 stackFrame = {0};
  stackFrame.AddrPC.Offset = ctx->Rip;
  stackFrame.AddrPC.Mode = AddrModeFlat;
  stackFrame.AddrStack.Offset = ctx->Rsp;
  stackFrame.AddrStack.Mode = AddrModeFlat;
  stackFrame.AddrFrame.Offset = ctx->Rbp;
  stackFrame.AddrFrame.Mode = AddrModeFlat;

  HANDLE hThread = GetCurrentThread();

  for (int frameNum = 0; frameNum < 20 && offset < buffer_size - 200; frameNum++) {
    if (stackFrame.AddrStack.Offset == 0 || stackFrame.AddrStack.Offset > 0x7FFFFFFFFFFF) {
      offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d [Stack corrupted or end reached]\n", frameNum);
      break;
    }

    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &stackFrame, ctx, NULL, SymFunctionTableAccess64,
                     SymGetModuleBase64, NULL)) {
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

    if (g_thread_symbols_initialized && SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &symDisplacement, pSym)) {
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
      // Symbol resolution failed - try to get module name as fallback
      HMODULE hMod = NULL;
      CHAR modName[MAX_PATH] = {0};
      BOOL foundModule = FALSE;

      // First try GetModuleHandleEx (fast, works for most cases)
      if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCTSTR)stackFrame.AddrPC.Offset, &hMod)) {
        foundModule = GetModuleFileNameA(hMod, modName, sizeof(modName)) > 0;
      } else if (g_thread_symbols_initialized) {
        // Fallback: Try SymGetModuleBase64 to find which module this address belongs to
        DWORD64 moduleBase = SymGetModuleBase64(hProcess, stackFrame.AddrPC.Offset);
        if (moduleBase != 0) {
          // Found module base - try to get handle from base address
          HMODULE hModFromBase = (HMODULE)(uintptr_t)moduleBase;
          if (GetModuleFileNameA(hModFromBase, modName, sizeof(modName)) > 0) {
            hMod = hModFromBase;
            foundModule = TRUE;
          }
        }
      }

      if (foundModule) {
        char *slash = strrchr(modName, '\\');
        char *name = slash ? slash + 1 : modName;
        DWORD64 offsetInModule = stackFrame.AddrPC.Offset - (DWORD64)(uintptr_t)hMod;
        offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d 0x%016llX %s!0x%llX\n", frameNum,
                           stackFrame.AddrPC.Offset, name, offsetInModule);
      } else {
        // Can't resolve symbol or module - still show address for manual resolution
        offset += snprintf(buffer + offset, buffer_size - offset, "  #%02d 0x%016llX <unresolved>\n", frameNum,
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

  // CRITICAL: Validate wrapper structure before accessing its fields
  // If the wrapper was freed or corrupted, accessing its fields will crash
  __try {
    // Test if wrapper is readable by accessing a field
    if (!wrapper->posix_func) {
      log_error("THREAD_WRAPPER: NULL function pointer!");
      SAFE_FREE(wrapper);
      return 1;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Wrapper is invalid or corrupted - exit immediately
    log_error("THREAD_WRAPPER: Invalid or corrupted wrapper structure");
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
    // CRITICAL: g_exception_pointers might point to freed stack memory if accessed from another thread
    // All accesses must be wrapped in __try/__except to prevent crashes
    if (g_exception_pointers && g_exception_pointers->ContextRecord) {
      // Use minimal output to avoid any potential crashes
      __try {
        // CRITICAL: Copy ExceptionRecord FIRST - it also points to stack memory that may be invalid
        // Both ExceptionRecord and ContextRecord point to stack memory from the exception thread
        // We must copy both before accessing them to avoid stack-use-after-scope errors
        PEXCEPTION_RECORD pExceptionRecord = g_exception_pointers->ExceptionRecord;
        EXCEPTION_RECORD exceptionRecordCopy = {0};
        DWORD exceptionCode = 0;

        __try {
          if (pExceptionRecord && IsBadReadPtr(pExceptionRecord, sizeof(EXCEPTION_RECORD)) == 0) {
            memcpy(&exceptionRecordCopy, pExceptionRecord, sizeof(EXCEPTION_RECORD));
            exceptionCode = exceptionRecordCopy.ExceptionCode;
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          exceptionCode = GetExceptionCode(); // Fallback to GetExceptionCode() if copy fails
        }

        // Step 1: Minimal exception info (using copied exception code)
        __try {
          printf("EXCEPTION: Code=0x%08lX Thread=%lu\n", (unsigned long)exceptionCode, GetCurrentThreadId());
          (void)fflush(stdout);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          // If even printf crashes, just exit
          return 1;
        }

        // Copy CONTEXT immediately to avoid stack-use-after-scope issues
        // g_exception_pointers->ContextRecord points to stack memory that may go out of scope
        // Must copy before ANY access to avoid AddressSanitizer errors
        PCONTEXT pContext = g_exception_pointers->ContextRecord;
        CONTEXT contextCopy = {0};
        BOOL contextValid = FALSE;

        __try {
          if (pContext && IsBadReadPtr(pContext, sizeof(CONTEXT)) == 0) {
            memcpy(&contextCopy, pContext, sizeof(CONTEXT));
            contextValid = TRUE;
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          contextValid = FALSE;
        }

        // Step 2: Try to get RIP address safely (using copied context)
        if (contextValid) {
          __try {
#ifdef _M_X64
            printf("RIP: 0x%016llX\n", contextCopy.Rip);
#else
            printf("EIP: 0x%08X\n", contextCopy.Eip);
#endif
            (void)fflush(stdout);
          } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("RIP/EIP access failed\n");
            (void)fflush(stdout);
          }
        }

        // Step 3: Try to build stack trace safely (using copied context)
        // Wrap the entire stack trace building in multiple try/except blocks for maximum safety
        __try {
          HANDLE hProcess = GetCurrentProcess();
          char stack_buffer[16384];
          memset(stack_buffer, 0, sizeof(stack_buffer));

          if (contextValid) {
            // Call build_stack_trace_message with try/except protection
            // This function might crash if it tries to access invalid memory
            __try {
              build_stack_trace_message(stack_buffer, sizeof(stack_buffer), &contextCopy, hProcess);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
              // If building the stack trace causes an exception, use minimal output
              (void)snprintf(stack_buffer, sizeof(stack_buffer),
                             "Stack trace generation failed (exception in trace builder)\n");
            }
          } else {
            (void)snprintf(stack_buffer, sizeof(stack_buffer), "Exception context is invalid or corrupted\n");
          }

          // Try to print the stack trace - even this could potentially fail
          __try {
            if (IsBadStringPtrA(stack_buffer, sizeof(stack_buffer))) {
              printf("Stack trace corrupted\n");
            } else {
              printf("%s", stack_buffer);
            }
            (void)fflush(stdout);
          } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("Failed to print stack trace (output error)\n");
            (void)fflush(stdout);
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          printf("Stack trace generation failed completely\n");
          (void)fflush(stdout);
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        // If accessing g_exception_pointers or building stack trace fails, skip it
        printf("Could not safely access exception context or build stack trace\n");
        (void)fflush(stdout);
      }
    } else {
      // No exception pointers available
      printf("No exception context available\n");
      (void)fflush(stdout);
    }
#else
    (void)build_stack_trace_message;
#endif // !NDEBUG

    // Use raw free to match raw malloc used in allocation (see allocation comment above for rationale)
    // Safe to use in exception handler - doesn't touch g_mem.mutex
#ifdef DEBUG_MEMORY
#undef free
    free(wrapper);
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

  // BUGFIX: Always close handle on error to prevent handle leak
  // WaitForSingleObject failed (not WAIT_OBJECT_0), so thread is in unknown state
  // We must still close the handle to prevent resource exhaustion
  CloseHandle((*thread));
  *thread = NULL;
  SET_ERRNO(ERROR_THREAD, "WaitForSingleObject failed with result %lu", result);
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
  if (!thread || (*thread) == NULL || (*thread) == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD result = WaitForSingleObject((*thread), timeout_ms);

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

  // On timeout, do NOT clear the handle - the thread might still be running or starting
  // The caller needs to be able to check if the thread is still alive using ascii_thread_is_initialized()
  // Only clear the handle when we successfully join (thread has exited)
  if (result == WAIT_TIMEOUT) {
    return -2; // Return timeout error code - thread handle remains valid
  }

  // BUGFIX: For WAIT_FAILED or other unexpected errors, close the handle to prevent leak
  // The thread is in an unknown state, but we must release the OS handle resource
  CloseHandle((*thread));
  *thread = NULL;
  SET_ERRNO(ERROR_THREAD, "WaitForSingleObject failed with result %lu", result);
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
  if (!thread || (*thread) == NULL || (*thread) == INVALID_HANDLE_VALUE) {
    SET_ERRNO(ERROR_THREAD, "Invalid thread handle for detach operation");
    return -1;
  }
  CloseHandle((*thread));
  *thread = NULL; // Clear the handle to prevent reuse
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

// ============================================================================
// Thread-Local Storage (TLS) Functions
// ============================================================================

/**
 * @brief Create a thread-local storage key
 * @param key Pointer to TLS key (output parameter)
 * @param destructor Optional destructor function called when thread exits
 * @return 0 on success, non-zero on error
 *
 * Uses Windows Fiber Local Storage (FLS) which works for both threads and fibers.
 * FLS supports destructors unlike TlsAlloc.
 */
int ascii_tls_key_create(tls_key_t *key, void (*destructor)(void *)) {
  if (!key) {
    return -1;
  }

  // FlsAlloc allocates a FLS index and registers a destructor callback
  // The destructor is called automatically when a thread terminates
  *key = FlsAlloc((PFLS_CALLBACK_FUNCTION)destructor);

  if (*key == FLS_OUT_OF_INDEXES) {
    return -1;
  }

  return 0;
}

/**
 * @brief Delete a thread-local storage key
 * @param key TLS key to delete
 * @return 0 on success, non-zero on error
 */
int ascii_tls_key_delete(tls_key_t key) {
  if (FlsFree(key)) {
    return 0;
  }
  return -1;
}

/**
 * @brief Get thread-local value for a key
 * @param key TLS key
 * @return Thread-local value, or NULL if not set
 */
void *ascii_tls_get(tls_key_t key) {
  return FlsGetValue(key);
}

/**
 * @brief Set thread-local value for a key
 * @param key TLS key
 * @param value Value to store
 * @return 0 on success, non-zero on error
 */
int ascii_tls_set(tls_key_t key, void *value) {
  if (FlsSetValue(key, value)) {
    return 0;
  }
  return -1;
}

#endif // _WIN32
