#include "common.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * Logging Implementation
 * ============================================================================
 */

static struct {
  FILE *file;
  log_level_t level;
  pthread_mutex_t mutex;
  bool initialized;
} g_log = {.file = NULL,
           .level = LOG_INFO,
           .mutex = PTHREAD_MUTEX_INITIALIZER,
           .initialized = false};

static const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR",
                                      "FATAL"};

static const char *level_colors[] = {
    "\x1b[36m", /* DEBUG: Cyan */
    "\x1b[32m", /* INFO: Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m"  /* FATAL: Magenta */
};

void log_init(const char *filename, log_level_t level) {
  pthread_mutex_lock(&g_log.mutex);

  if (g_log.initialized) {
    if (g_log.file && g_log.file != stderr) {
      fclose(g_log.file);
    }
  }

  g_log.level = level;

  if (filename) {
    g_log.file = fopen(filename, "a");
    if (!g_log.file) {
      fprintf(stderr, "Failed to open log file: %s\n", filename);
      g_log.file = stderr;
    }
  } else {
    g_log.file = stderr;
  }

  g_log.initialized = true;
  pthread_mutex_unlock(&g_log.mutex);
}

void log_destroy(void) {
  pthread_mutex_lock(&g_log.mutex);

  if (g_log.file && g_log.file != stderr) {
    fclose(g_log.file);
  }

  g_log.file = NULL;
  g_log.initialized = false;

  pthread_mutex_unlock(&g_log.mutex);
}

void log_set_level(log_level_t level) {
  pthread_mutex_lock(&g_log.mutex);
  g_log.level = level;
  pthread_mutex_unlock(&g_log.mutex);
}

void log_msg(log_level_t level, const char *file, int line, const char *func,
             const char *fmt, ...) {
  if (!g_log.initialized) {
    log_init(NULL, LOG_INFO);
  }

  if (level < g_log.level) {
    return;
  }

  pthread_mutex_lock(&g_log.mutex);

  /* Get current time */
  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

  /* Print to file (no colors) */
  if (g_log.file) {
    fprintf(g_log.file, "[%s] [%s] %s:%d in %s(): ", time_buf,
            level_strings[level], file, line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_log.file, fmt, args);
    va_end(args);

    fprintf(g_log.file, "\n");
    fflush(g_log.file);
  }

  /* Also print to stderr with colors if it's a terminal */
  if (g_log.file != stderr && isatty(fileno(stderr))) {
    fprintf(stderr, "%s[%s] [%s]\x1b[0m %s:%d in %s(): ", level_colors[level],
            time_buf, level_strings[level], file, line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
  }

  pthread_mutex_unlock(&g_log.mutex);
}

/* ============================================================================
 * Error String Conversion
 * ============================================================================
 */

const char *asciichat_error_string(asciichat_error_t error) {
  switch (error) {
  case ASCIICHAT_OK:
    return "Success";
  case ASCIICHAT_ERR_MALLOC:
    return "Memory allocation failed";
  case ASCIICHAT_ERR_NETWORK:
    return "Network error";
  case ASCIICHAT_ERR_WEBCAM:
    return "Webcam error";
  case ASCIICHAT_ERR_INVALID_PARAM:
    return "Invalid parameter";
  case ASCIICHAT_ERR_TIMEOUT:
    return "Operation timed out";
  case ASCIICHAT_ERR_BUFFER_FULL:
    return "Buffer full";
  case ASCIICHAT_ERR_JPEG:
    return "JPEG processing error";
  case ASCIICHAT_ERR_TERMINAL:
    return "Terminal error";
  default:
    return "Unknown error";
  }
}

/* ============================================================================
 * Memory Debugging (Debug builds only)
 * ============================================================================
 */

#ifdef DEBUG_MEMORY

typedef struct mem_block {
  void *ptr;
  size_t size;
  char file[256];
  int line;
  struct mem_block *next;
} mem_block_t;

static struct {
  mem_block_t *head;
  size_t total_allocated;
  size_t total_freed;
  size_t current_usage;
  size_t peak_usage;
  pthread_mutex_t mutex;
} g_mem = {.head = NULL,
           .total_allocated = 0,
           .total_freed = 0,
           .current_usage = 0,
           .peak_usage = 0,
           .mutex = PTHREAD_MUTEX_INITIALIZER};

void *debug_malloc(size_t size, const char *file, int line) {
  void *ptr = malloc(size);
  if (!ptr)
    return NULL;

  pthread_mutex_lock(&g_mem.mutex);

  mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
  if (block) {
    block->ptr = ptr;
    block->size = size;
    strncpy(block->file, file, sizeof(block->file) - 1);
    block->file[sizeof(block->file) - 1] = '\0';
    block->line = line;
    block->next = g_mem.head;
    g_mem.head = block;

    g_mem.total_allocated += size;
    g_mem.current_usage += size;
    if (g_mem.current_usage > g_mem.peak_usage) {
      g_mem.peak_usage = g_mem.current_usage;
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);

  return ptr;
}

void debug_free(void *ptr, const char *file, int line) {
  if (!ptr)
    return;

  pthread_mutex_lock(&g_mem.mutex);

  mem_block_t *prev = NULL;
  mem_block_t *curr = g_mem.head;

  while (curr) {
    if (curr->ptr == ptr) {
      if (prev) {
        prev->next = curr->next;
      } else {
        g_mem.head = curr->next;
      }

      g_mem.total_freed += curr->size;
      g_mem.current_usage -= curr->size;

      free(curr);
      break;
    }
    prev = curr;
    curr = curr->next;
  }

  if (!curr) {
    log_warn("Freeing untracked pointer %p at %s:%d", ptr, file, line);
  }

  pthread_mutex_unlock(&g_mem.mutex);

  free(ptr);
}

void debug_memory_report(void) {
  pthread_mutex_lock(&g_mem.mutex);

  printf("\n=== Memory Report ===\n");
  printf("Total allocated: %zu bytes\n", g_mem.total_allocated);
  printf("Total freed: %zu bytes\n", g_mem.total_freed);
  printf("Current usage: %zu bytes\n", g_mem.current_usage);
  printf("Peak usage: %zu bytes\n", g_mem.peak_usage);

  if (g_mem.head) {
    printf("\nLeaked allocations:\n");
    mem_block_t *curr = g_mem.head;
    while (curr) {
      printf("  - %zu bytes at %s:%d\n", curr->size, curr->file, curr->line);
      curr = curr->next;
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);
}

#endif /* DEBUG_MEMORY */