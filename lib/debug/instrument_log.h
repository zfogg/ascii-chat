// SPDX-License-Identifier: MIT
// Debug instrumentation logging runtime for ascii-chat line tracing

#ifndef ASCII_CHAT_DEBUG_INSTRUMENT_LOG_H
#define ASCII_CHAT_DEBUG_INSTRUMENT_LOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ascii_instr_runtime ascii_instr_runtime_t;

ascii_instr_runtime_t *ascii_instr_runtime_get(void);

void ascii_instr_runtime_destroy(ascii_instr_runtime_t *runtime);

void ascii_instr_runtime_global_shutdown(void);

void ascii_instr_log_line(const char *file_path, uint32_t line_number, const char *function_name, const char *snippet,
                          uint8_t is_macro_expansion);

#ifdef __cplusplus
}
#endif

#endif // ASCII_CHAT_DEBUG_INSTRUMENT_LOG_H
