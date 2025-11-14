// SPDX-License-Identifier: MIT
// Debug instrumentation logging runtime for ascii-chat line tracing

#ifndef ASCII_CHAT_DEBUG_INSTRUMENT_LOG_H
#define ASCII_CHAT_DEBUG_INSTRUMENT_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  ASCII_INSTR_SOURCE_PRINT_MACRO_NONE = 0u,
  ASCII_INSTR_SOURCE_PRINT_MACRO_EXPANSION = 1u,
  ASCII_INSTR_SOURCE_PRINT_MACRO_INVOCATION = 2u,
};

#if defined(__clang__)
#define ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER __attribute__((annotate("ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER")))
#else
#define ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER
#endif

typedef struct ascii_instr_runtime ascii_instr_runtime_t;

ascii_instr_runtime_t *ascii_instr_runtime_get(void);

void ascii_instr_runtime_destroy(ascii_instr_runtime_t *runtime);

void ascii_instr_runtime_global_shutdown(void);

void ascii_instr_log_line(const char *file_path, uint32_t line_number, const char *function_name, const char *snippet,
                          uint8_t is_macro_expansion);

bool ascii_instr_coverage_enabled(void);

void ascii_instr_log_pc(uintptr_t program_counter);

#ifdef __cplusplus
}
#endif

#endif // ASCII_CHAT_DEBUG_INSTRUMENT_LOG_H
