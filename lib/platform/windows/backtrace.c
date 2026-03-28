/**
 * @file platform/windows/backtrace.c
 * @brief Windows backtrace implementation using StackWalk64 and DbgHelp
 * @ingroup platform
 */

#include <ascii-chat/platform/backtrace.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <windows.h>
#include <dbghelp.h>

int platform_backtrace(void **buffer, int size) {
  if (!buffer || size <= 0) {
    return 0;
  }

  // Try the simpler CaptureStackBackTrace first (faster and more reliable)
  USHORT captured = CaptureStackBackTrace(1, size, buffer, NULL);
  if (captured > 0) {
    return (int)captured;
  }

  // Fall back to the more complex StackWalk64 approach
  // Capture current context
  CONTEXT context;
  RtlCaptureContext(&context);

  // Initialize symbol handler for current process
  HANDLE process = GetCurrentProcess();
  if (!SymInitialize(process, NULL, TRUE)) {
    DWORD error = GetLastError();
    log_error("[ERROR] platform_backtrace: SymInitialize failed with error %lu", error);
    return 0;
  }

  // Set up stack frame based on architecture
  STACKFRAME64 frame;
  ZeroMemory(&frame, sizeof(frame));

#ifdef _WIN64
  frame.AddrPC.Offset = context.Rip;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrStack.Offset = context.Rsp;
#else
  frame.AddrPC.Offset = context.Eip;
  frame.AddrFrame.Offset = context.Ebp;
  frame.AddrStack.Offset = context.Esp;
#endif
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;

  // Walk the stack
  int count = 0;
  while (count < size) {
    BOOL result = StackWalk64(
#ifdef _WIN64
        IMAGE_FILE_MACHINE_AMD64,
#else
        IMAGE_FILE_MACHINE_I386,
#endif
        process, GetCurrentThread(), &frame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);

    if (!result) {
      DWORD error = GetLastError();
      if (count == 0) {
        log_error("[ERROR] platform_backtrace: StackWalk64 failed with error %lu", error);
      }
      break;
    }

    if (frame.AddrPC.Offset == 0) {
      break;
    }

    buffer[count++] = (void *)frame.AddrPC.Offset;
  }

  SymCleanup(process);
  return count;
}

char **platform_backtrace_symbols(void *const *buffer, int size) {
  if (!buffer || size <= 0) {
    return NULL;
  }

  // Allocate symbol array (size + 1 for terminator)
  char **symbols = SAFE_MALLOC((size + 1) * sizeof(char *), char **);
  if (!symbols) {
    return NULL;
  }

  // Try to resolve each address
  for (int i = 0; i < size; i++) {
    void *addr = buffer[i];

    // First, try the symbol cache (llvm-symbolizer / addr2line)
    char **cache_result = symbol_cache_resolve_batch(&addr, 1);
    if (cache_result && cache_result[0]) {
      symbols[i] = platform_strdup(cache_result[0]);
      symbol_cache_free_symbols(cache_result);
      if (symbols[i]) {
        continue;
      }
    }

    // Fall back to Windows SymFromAddr
    HANDLE process = GetCurrentProcess();
    if (!SymInitialize(process, NULL, TRUE)) {
      symbols[i] = platform_strdup("[?] (SymInitialize failed)");
      continue;
    }

    // Set up symbol info structure
    char buffer_space[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *symbol_info = (SYMBOL_INFO *)buffer_space;
    ZeroMemory(symbol_info, sizeof(SYMBOL_INFO) + 256);
    symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol_info->MaxNameLen = 255;

    // Try to get symbol name
    DWORD64 displacement = 0;
    BOOL sym_result = SymFromAddr(process, (DWORD64)addr, &displacement, symbol_info);

    // Try to get file/line info (optional)
    IMAGEHLP_LINE64 line_info;
    ZeroMemory(&line_info, sizeof(line_info));
    line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD line_displacement = 0;
    BOOL line_result = SymGetLineFromAddr64(process, (DWORD64)addr, &line_displacement, &line_info);

    // Format the result
    char symbol_buffer[512];
    if (sym_result) {
      if (line_result) {
        snprintf(symbol_buffer, sizeof(symbol_buffer), "%s() (%s:%ld)", symbol_info->Name,
                 line_info.FileName, line_info.LineNumber);
      } else {
        snprintf(symbol_buffer, sizeof(symbol_buffer), "%s()", symbol_info->Name);
      }
    } else {
      snprintf(symbol_buffer, sizeof(symbol_buffer), "[0x%p]", addr);
    }

    symbols[i] = platform_strdup(symbol_buffer);
    SymCleanup(process);
  }

  // Null-terminate the array
  symbols[size] = NULL;

  return symbols;
}

void platform_backtrace_symbols_destroy(char **strings) {
  if (!strings) {
    return;
  }

  // Free each individual string
  for (int i = 0; strings[i] != NULL; i++) {
    SAFE_FREE(strings[i]);
  }

  // Free the array itself
  SAFE_FREE(strings);
}

void backtrace_print_simple(int skip_frames) {
  void *buffer[64];
  int depth = platform_backtrace(buffer, 64);

  if (depth > 0) {
    char **symbols = platform_backtrace_symbols((void *const *)buffer, depth);
    if (symbols) {
      int start = 1 + skip_frames;
      fprintf(stderr, "Backtrace:\n");
      for (int i = start; i < depth; i++) {
        fprintf(stderr, "  #%d %s\n", i - start, symbols[i] ? symbols[i] : "???");
      }
      platform_backtrace_symbols_destroy(symbols);
    }
  }
}
