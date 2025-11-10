// SPDX-License-Identifier: MIT
// Memory debugging helpers for tracking allocations in debug builds

#pragma once

#include <stdbool.h>
#include <stddef.h>

#if defined(DEBUG_MEMORY)

void debug_memory_set_quiet_mode(bool quiet);

void debug_memory_report(void);

void *debug_malloc(size_t size, const char *file, int line);

void *debug_calloc(size_t count, size_t size, const char *file, int line);

void *debug_realloc(void *ptr, size_t size, const char *file, int line);

void debug_free(void *ptr, const char *file, int line);

void debug_track_aligned(void *ptr, size_t size, const char *file, int line);

#endif /* DEBUG_MEMORY */
