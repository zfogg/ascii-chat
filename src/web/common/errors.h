/** Browser-facing C interface implemented in errors.c. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t browser_error_snapshot(void);
int browser_error_code(size_t index);
const char *browser_error_message(size_t index);
const char *browser_error_file(size_t index);
const char *browser_error_full_file(size_t index);
const char *browser_error_function(size_t index);
int browser_error_line(size_t index);

void browser_error_dismiss(void);

#ifdef __cplusplus
}
#endif
