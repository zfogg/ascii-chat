/**
 * @file logging_mmap.c
 * @brief High-performance memory-mapped logging implementation
 * @ingroup logging
 */

#include "log/mmap.h"
#include "log/logging.h"
#include "platform/mmap.h"
#include "platform/thread.h"
#include "platform/system.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#ifndef _WIN32
#include <unistd.h>
#endif

/* ============================================================================
 * Global State
 * ============================================================================ */

static struct {
  platform_mmap_t mmap;          /* Memory-mapped file handle */
  log_mmap_buffer_t *buffer;     /* Pointer to mmap'd buffer */
  int text_log_fd;               /* File descriptor for text log */
  asciithread_t flusher_thread;  /* Background flusher thread */
  volatile bool flusher_running; /* Flusher thread control flag */
  uint64_t read_tail;            /* Next entry to flush (flusher only) */
  uint32_t flush_interval_ms;    /* Flusher sleep interval */
  bool immediate_error_flush;    /* Flush ERROR/FATAL immediately */
  bool initialized;              /* Initialization flag */

  /* Statistics */
  _Atomic uint64_t total_entries;
  _Atomic uint64_t flushed_entries;
  _Atomic uint64_t dropped_entries;
} g_mmap_log = {
    .text_log_fd = -1,
    .flusher_running = false,
    .read_tail = 0,
    .flush_interval_ms = 100,
    .immediate_error_flush = true,
    .initialized = false,
};

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

static uint64_t get_timestamp_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  }
  return 0;
}

static void format_timestamp(uint64_t timestamp_ns, char *buf, size_t buf_size) {
  /* Get wall clock time for human-readable output */
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  struct tm tm_info;
  platform_localtime(&ts.tv_sec, &tm_info);

  size_t len = strftime(buf, buf_size, "%H:%M:%S", &tm_info);
  if (len > 0 && len < buf_size - 10) {
    snprintf(buf + len, buf_size - len, ".%06ld", ts.tv_nsec / 1000);
  }

  (void)timestamp_ns; /* Use monotonic timestamp for ordering, wall clock for display */
}

/* ============================================================================
 * Flusher Thread
 * ============================================================================ */

static void flush_entries_to_text_log(void) {
  if (!g_mmap_log.buffer || g_mmap_log.text_log_fd < 0) {
    return;
  }

  static const char *level_names[] = {"DEV", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
  uint32_t entry_count = g_mmap_log.buffer->header.entry_count;
  uint64_t write_head = atomic_load(&g_mmap_log.buffer->header.write_head);

  while (g_mmap_log.read_tail < write_head) {
    uint32_t idx = (uint32_t)(g_mmap_log.read_tail % entry_count);
    log_mmap_entry_t *entry = &g_mmap_log.buffer->entries[idx];

    /* Wait for entry to be written (sequence != 0) */
    uint64_t seq = atomic_load(&entry->sequence);
    if (seq == 0) {
      /* Entry not yet written, flusher is ahead of writer */
      break;
    }

    /* Format and write to text log */
    char time_buf[32];
    format_timestamp(entry->timestamp_ns, time_buf, sizeof(time_buf));

    const char *level_name = (entry->level < 6) ? level_names[entry->level] : "???";

    char line[LOG_MMAP_MAX_MESSAGE + 64];
    int len = snprintf(line, sizeof(line), "[%s] [%s] %s\n", time_buf, level_name, entry->message);

    if (len > 0) {
      (void)write(g_mmap_log.text_log_fd, line, (size_t)len);
    }

    /* Mark entry as flushed (optional: could clear sequence) */
    atomic_fetch_add(&g_mmap_log.flushed_entries, 1);
    g_mmap_log.read_tail++;
  }
}

static void *flusher_thread_func(void *arg) {
  (void)arg;

  while (g_mmap_log.flusher_running) {
    flush_entries_to_text_log();
    platform_sleep_ms(g_mmap_log.flush_interval_ms);
  }

  /* Final flush on shutdown */
  flush_entries_to_text_log();
  return NULL;
}

/* ============================================================================
 * Signal Handlers for Crash Safety
 * ============================================================================ */

static volatile sig_atomic_t g_crash_in_progress = 0;

static void crash_signal_handler(int sig) {
  /* Prevent recursive crashes */
  if (g_crash_in_progress) {
    _exit(128 + sig);
  }
  g_crash_in_progress = 1;

  /* Write crash marker */
  if (g_mmap_log.text_log_fd >= 0) {
    const char *msg = "\n=== CRASH DETECTED - FLUSHING MMAP LOG ===\n";
    (void)write(g_mmap_log.text_log_fd, msg, strlen(msg));
  }

  /* Flush remaining entries */
  flush_entries_to_text_log();

  if (g_mmap_log.text_log_fd >= 0) {
    const char *msg = "=== CRASH FLUSH COMPLETE ===\n";
    (void)write(g_mmap_log.text_log_fd, msg, strlen(msg));
    fsync(g_mmap_log.text_log_fd);
  }

  /* Sync mmap */
  platform_mmap_sync(&g_mmap_log.mmap, false);

  /* Re-raise with default handler for core dump */
  signal(sig, SIG_DFL);
  raise(sig);
}

void log_mmap_install_crash_handlers(void) {
#ifndef _WIN32
  struct sigaction sa = {0};
  sa.sa_handler = crash_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = (int)SA_RESETHAND; /* One-shot (cast silences UBSan implicit conversion warning) */

  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
#else
  /* Windows: Use SetUnhandledExceptionFilter or similar */
  /* TODO: Implement Windows crash handler */
#endif
}

/* ============================================================================
 * Public API
 * ============================================================================ */

asciichat_error_t log_mmap_init(const log_mmap_config_t *config) {
  if (!config || !config->mmap_path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mmap log: config or mmap_path is NULL");
  }

  if (g_mmap_log.initialized) {
    log_warn("mmap log: already initialized, destroying first");
    log_mmap_destroy();
  }

  /* Calculate sizes */
  uint32_t entry_count = config->entry_count > 0 ? config->entry_count : LOG_MMAP_DEFAULT_ENTRIES;
  size_t mmap_size = sizeof(log_mmap_header_t) + (size_t)entry_count * sizeof(log_mmap_entry_t);

  /* Open mmap file */
  platform_mmap_init(&g_mmap_log.mmap);
  asciichat_error_t result = platform_mmap_open(config->mmap_path, mmap_size, &g_mmap_log.mmap);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  g_mmap_log.buffer = (log_mmap_buffer_t *)g_mmap_log.mmap.addr;

  /* Initialize or validate header */
  if (g_mmap_log.buffer->header.magic != LOG_MMAP_MAGIC) {
    /* New file - initialize */
    memset(g_mmap_log.buffer, 0, mmap_size);
    g_mmap_log.buffer->header.magic = LOG_MMAP_MAGIC;
    g_mmap_log.buffer->header.version = LOG_MMAP_VERSION;
    g_mmap_log.buffer->header.entry_count = entry_count;
    g_mmap_log.buffer->header.entry_size = sizeof(log_mmap_entry_t);
    g_mmap_log.buffer->header.start_timestamp_ns = get_timestamp_ns();
    atomic_store(&g_mmap_log.buffer->header.write_head, 0);
    log_info("mmap log: created new log file with %u entries", entry_count);
  } else {
    /* Existing file - recover */
    if (g_mmap_log.buffer->header.version != LOG_MMAP_VERSION) {
      log_warn("mmap log: version mismatch (file=%u, expected=%u), reinitializing", g_mmap_log.buffer->header.version,
               LOG_MMAP_VERSION);
      memset(g_mmap_log.buffer, 0, mmap_size);
      g_mmap_log.buffer->header.magic = LOG_MMAP_MAGIC;
      g_mmap_log.buffer->header.version = LOG_MMAP_VERSION;
      g_mmap_log.buffer->header.entry_count = entry_count;
      g_mmap_log.buffer->header.entry_size = sizeof(log_mmap_entry_t);
      g_mmap_log.buffer->header.start_timestamp_ns = get_timestamp_ns();
      atomic_store(&g_mmap_log.buffer->header.write_head, 0);
    } else {
      uint64_t existing_entries = atomic_load(&g_mmap_log.buffer->header.write_head);
      log_info("mmap log: recovered existing log with %lu entries", existing_entries);
    }
  }

  /* Open text log file if requested */
  if (config->text_log_path) {
    g_mmap_log.text_log_fd = open(config->text_log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (g_mmap_log.text_log_fd < 0) {
      log_warn("mmap log: failed to open text log %s, flusher disabled", config->text_log_path);
    }
  }

  /* Configure options */
  g_mmap_log.flush_interval_ms = config->flush_interval_ms > 0 ? config->flush_interval_ms : 100;
  g_mmap_log.immediate_error_flush = config->immediate_error_flush;
  g_mmap_log.read_tail = 0; /* Start from beginning on fresh init */

  /* Reset statistics */
  atomic_store(&g_mmap_log.total_entries, 0);
  atomic_store(&g_mmap_log.flushed_entries, 0);
  atomic_store(&g_mmap_log.dropped_entries, 0);

  /* Start flusher thread if text log is configured */
  if (g_mmap_log.text_log_fd >= 0) {
    g_mmap_log.flusher_running = true;
    if (ascii_thread_create(&g_mmap_log.flusher_thread, flusher_thread_func, NULL) != 0) {
      log_warn("mmap log: failed to start flusher thread");
      g_mmap_log.flusher_running = false;
    } else {
      log_debug("mmap log: flusher thread started (interval=%ums)", g_mmap_log.flush_interval_ms);
    }
  }

  /* Install crash handlers */
  log_mmap_install_crash_handlers();

  g_mmap_log.initialized = true;
  log_info("mmap log: initialized at %s (%zu bytes)", config->mmap_path, mmap_size);

  return ASCIICHAT_OK;
}

asciichat_error_t log_mmap_init_simple(const char *mmap_path, const char *text_log_path) {
  log_mmap_config_t config = {
      .mmap_path = mmap_path,
      .text_log_path = text_log_path,
      .entry_count = 0,       /* Use default */
      .flush_interval_ms = 0, /* Use default */
      .immediate_error_flush = true,
  };
  return log_mmap_init(&config);
}

void log_mmap_destroy(void) {
  if (!g_mmap_log.initialized) {
    return;
  }

  /* Stop flusher thread */
  if (g_mmap_log.flusher_running) {
    g_mmap_log.flusher_running = false;
    ascii_thread_join(&g_mmap_log.flusher_thread, NULL);
  }

  /* Final flush */
  flush_entries_to_text_log();

  /* Close text log */
  if (g_mmap_log.text_log_fd >= 0) {
    fsync(g_mmap_log.text_log_fd);
    close(g_mmap_log.text_log_fd);
    g_mmap_log.text_log_fd = -1;
  }

  /* Sync and close mmap */
  platform_mmap_sync(&g_mmap_log.mmap, false);
  platform_mmap_close(&g_mmap_log.mmap);
  g_mmap_log.buffer = NULL;

  g_mmap_log.initialized = false;
  log_info("mmap log: destroyed");
}

void log_mmap_write(int level, const char *file, int line, const char *func, const char *fmt, ...) {
  if (!g_mmap_log.initialized || !g_mmap_log.buffer) {
    return;
  }

  /* Claim a slot atomically - this is the fast path, no mutex! */
  uint64_t slot = atomic_fetch_add(&g_mmap_log.buffer->header.write_head, 1);
  uint32_t idx = (uint32_t)(slot % g_mmap_log.buffer->header.entry_count);

  log_mmap_entry_t *entry = &g_mmap_log.buffer->entries[idx];

  /* Check for overwrite (ring buffer full) */
  uint64_t old_seq = atomic_load(&entry->sequence);
  /* Only check for overwrite if slot >= old_seq to avoid underflow.
   * If old_seq > slot, it's from a previous session and can be overwritten. */
  if (old_seq != 0 && slot >= old_seq && (slot - old_seq) < g_mmap_log.buffer->header.entry_count) {
    /* Would overwrite unflushed entry - count as dropped */
    atomic_fetch_add(&g_mmap_log.dropped_entries, 1);
  }

  /* Fill entry */
  entry->timestamp_ns = get_timestamp_ns();
  entry->level = (uint8_t)level;

  /* Format message */
  va_list args;
  va_start(args, fmt);
  if (file && func) {
    /* Debug format with location */
    int prefix_len = snprintf(entry->message, sizeof(entry->message), "%s:%d %s(): ", file, line, func);
    if (prefix_len > 0 && prefix_len < (int)sizeof(entry->message)) {
      vsnprintf(entry->message + prefix_len, sizeof(entry->message) - (size_t)prefix_len, fmt, args);
    }
  } else {
    /* Simple format */
    vsnprintf(entry->message, sizeof(entry->message), fmt, args);
  }
  va_end(args);

  /* Mark entry as written by setting sequence - this is the "ready" signal */
  atomic_store(&entry->sequence, slot + 1);

  atomic_fetch_add(&g_mmap_log.total_entries, 1);

  /* Immediate flush for errors if configured */
  if (g_mmap_log.immediate_error_flush && level >= 4 /* LOG_ERROR */) {
    flush_entries_to_text_log();
    if (g_mmap_log.text_log_fd >= 0) {
      fsync(g_mmap_log.text_log_fd);
    }
  }
}

bool log_mmap_is_active(void) {
  return g_mmap_log.initialized;
}

void log_mmap_flush(void) {
  flush_entries_to_text_log();
  if (g_mmap_log.text_log_fd >= 0) {
    fsync(g_mmap_log.text_log_fd);
  }
}

void log_mmap_get_stats(uint64_t *total_entries, uint64_t *flushed_entries, uint64_t *dropped_entries) {
  if (total_entries) {
    *total_entries = atomic_load(&g_mmap_log.total_entries);
  }
  if (flushed_entries) {
    *flushed_entries = atomic_load(&g_mmap_log.flushed_entries);
  }
  if (dropped_entries) {
    *dropped_entries = atomic_load(&g_mmap_log.dropped_entries);
  }
}
