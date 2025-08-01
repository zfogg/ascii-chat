#include "common.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ============================================================================
 * Logging Implementation
 * ============================================================================
 */

#define MAX_LOG_SIZE (3 * 1024 * 1024) /* 3MB max log file size */

static struct {
  int file;
  log_level_t level;
  pthread_mutex_t mutex;
  bool initialized;
  char filename[256];  /* Store filename for rotation */
  size_t current_size; /* Track current file size */
} g_log = {.file = 0,
           .level = LOG_INFO,
           .mutex = PTHREAD_MUTEX_INITIALIZER,
           .initialized = false,
           .filename = {0},
           .current_size = 0};

static const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static const char *level_colors[] = {
    "\x1b[36m", /* DEBUG: Cyan */
    "\x1b[32m", /* INFO: Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m"  /* FATAL: Magenta */
};

/* Log rotation function - keeps the tail (recent entries) */
static void rotate_log_if_needed(void) {
  if (!g_log.file || g_log.file == STDERR_FILENO || strlen(g_log.filename) == 0) {
    return;
  }

  if (g_log.current_size >= MAX_LOG_SIZE) {
    close(g_log.file);

    /* Open file for reading to get the tail */
    int read_file = open(g_log.filename, O_RDONLY);
    if (read_file < 0) {
      fprintf(stderr, "Failed to open log file for tail rotation: %s\n", g_log.filename);
      /* Fall back to regular truncation */
      int fd = open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Seek to position where we want to start keeping data (keep last 2MB) */
    size_t keep_size = MAX_LOG_SIZE * 2 / 3; /* Keep last 2MB of 3MB file */
    if (lseek(read_file, (off_t)(g_log.current_size - keep_size), SEEK_SET) == (off_t)-1) {
      close(read_file);
      /* Fall back to truncation */
      int fd = open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Skip to next line boundary to avoid partial lines */
    char c;
    ssize_t read_result;
    while ((read_result = read(read_file, &c, 1)) > 0 && c != '\n') {
      /* Skip characters until newline */
    }

    /* Read the tail into a temporary file */
    char temp_filename[512];
    snprintf(temp_filename, sizeof(temp_filename), "%s.tmp", g_log.filename);
    int temp_file = open(temp_filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (temp_file < 0) {
      close(read_file);
      /* Fall back to truncation */
      int fd = open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Copy tail to temp file */
    char buffer[8192];
    ssize_t bytes_read;
    size_t new_size = 0;
    while ((bytes_read = read(read_file, buffer, sizeof(buffer))) > 0) {
      ssize_t written = write(temp_file, buffer, bytes_read);
      if (written != bytes_read) {
        close(read_file);
        close(temp_file);
        unlink(temp_filename);
        /* Fall back to truncation */
        int fd = open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
        g_log.file = fd;
        g_log.current_size = 0;
        return;
      }
      new_size += (size_t)bytes_read;
    }

    close(read_file);
    close(temp_file);

    /* Replace original with temp file */
    if (rename(temp_filename, g_log.filename) != 0) {
      unlink(temp_filename); /* Clean up temp file */
      /* Fall back to truncation */
      int fd = open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Reopen for appending */
    g_log.file = open(g_log.filename, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
    if (g_log.file < 0) {
      fprintf(stderr, "Failed to reopen rotated log file: %s\n", g_log.filename);
      g_log.file = STDERR_FILENO;
      g_log.filename[0] = '\0';
    } else {
      g_log.current_size = new_size;
      /* Log the rotation event */
      {
        char log_msg[256];
        int log_msg_len = snprintf(log_msg, sizeof(log_msg), "[%s] [INFO] Log tail-rotated (kept %zu bytes)\n",
                                   "1970-01-01 00:00:00.000", new_size);
        if (log_msg_len > 0) {
          ssize_t written = write(g_log.file, log_msg, (size_t)log_msg_len);
          (void)written; // suppress unused warning
        }
      }
    }
  }
}

void log_init(const char *filename, log_level_t level) {
  pthread_mutex_lock(&g_log.mutex);

  if (g_log.initialized) {
    if (g_log.file && g_log.file != STDERR_FILENO) {
      close(g_log.file);
    }
  }

  g_log.level = level;
  g_log.current_size = 0;

  if (filename) {
    /* Store filename for rotation */
    strncpy(g_log.filename, filename, sizeof(g_log.filename) - 1);
    g_log.filename[sizeof(g_log.filename) - 1] = '\0';
    int fd = open(filename, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
    g_log.file = fd;
    if (!g_log.file) {
      fprintf(stderr, "Failed to open log file: %s\n", filename);
      g_log.file = STDERR_FILENO;
      g_log.filename[0] = '\0'; /* Clear filename on failure */
    } else {
      /* Get current file size */
      struct stat st;
      if (fstat(g_log.file, &st) == 0) {
        g_log.current_size = (size_t)st.st_size;
      }
    }
  } else {
    g_log.file = STDERR_FILENO;
    g_log.filename[0] = '\0';
  }

  g_log.initialized = true;
  pthread_mutex_unlock(&g_log.mutex);
}

void log_destroy(void) {
  pthread_mutex_lock(&g_log.mutex);

  if (g_log.file && g_log.file != STDERR_FILENO) {
    close(g_log.file);
  }

  g_log.file = 0;
  g_log.initialized = false;

  pthread_mutex_unlock(&g_log.mutex);
}

void log_set_level(log_level_t level) {
  pthread_mutex_lock(&g_log.mutex);
  g_log.level = level;
  pthread_mutex_unlock(&g_log.mutex);
}

void log_truncate_if_large(void) {
  pthread_mutex_lock(&g_log.mutex);

  if (g_log.file && g_log.file != STDERR_FILENO && strlen(g_log.filename) > 0) {
    /* Check if current log is too large */
    struct stat st;
    if (fstat(g_log.file, &st) == 0 && st.st_size > MAX_LOG_SIZE) {
      /* Save the current size and trigger rotation logic */
      g_log.current_size = (size_t)st.st_size;

      /* Use the same tail-keeping rotation logic */
      rotate_log_if_needed();
    }
  }

  pthread_mutex_unlock(&g_log.mutex);
}

void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  if (!g_log.initialized) {
    log_init(NULL, LOG_INFO);
  }

  if (level < g_log.level) {
    return;
  }

  pthread_mutex_lock(&g_log.mutex);

  /* Get current time using clock_gettime (avoids localtime) */
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  struct tm tm_info;
  gmtime_r(&ts.tv_sec, &tm_info); /* UTC time; gmtime_r is thread-safe */

  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

  char time_buf_ms[40];
  snprintf(time_buf_ms, sizeof(time_buf_ms), "%s.%03ld", time_buf, ts.tv_nsec / 1000000);

  /* Check if log rotation is needed */
  rotate_log_if_needed();

  FILE *log_file = NULL;
  if (g_log.file == STDERR_FILENO) {
    log_file = stderr;
  } else {
    log_file = fdopen(g_log.file, "a");
    if (!log_file) {
      log_file = stderr;
    }
  }

  /* Print to file (no colors) */
  if (g_log.file && g_log.file != STDERR_FILENO && log_file != stderr) {
    int written = fprintf(log_file, "[%s] [%s] %s:%d in %s(): ", time_buf_ms, level_strings[level], file, line, func);
    g_log.current_size += (written > 0) ? (size_t)written : 0;

    va_list args;
    va_start(args, fmt);
    written = vfprintf(log_file, fmt, args);
    va_end(args);
    g_log.current_size += (written > 0) ? (size_t)written : 0;

    written = fprintf(log_file, "\n");
    g_log.current_size += (written > 0) ? (size_t)written : 0;

    fflush(log_file);
  } else if (log_file == stderr) {
    fprintf(log_file, "[%s] [%s] %s:%d in %s(): ", time_buf_ms, level_strings[level], file, line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
  }

  /* Also print to stderr with colors if it's a terminal */
  if (g_log.file != STDERR_FILENO && isatty(STDERR_FILENO)) {
    fprintf(stderr, "%s[%s] [%s]\x1b[0m %s:%d in %s(): ", level_colors[level], time_buf_ms, level_strings[level], file,
            line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
  }

  pthread_mutex_unlock(&g_log.mutex);
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