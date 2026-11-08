/** Browser-facing C interface implemented in errors.c. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t wasm_error_snapshot(void);
int wasm_error_code(size_t index);
const char *wasm_error_message(size_t index);
const char *wasm_error_file(size_t index);
const char *wasm_error_full_file(size_t index);
const char *wasm_error_function(size_t index);
int wasm_error_line(size_t index);

void wasm_error_dismiss(void);

#ifdef __cplusplus
}
#endif
