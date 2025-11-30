// SPDX-License-Identifier: MIT
// Summarizer for ascii-chat instrumentation runtime logs

#include "common.h"
#include "logging.h"
#include "platform/internal.h"
#include "tooling/panic/instrument_log.h"

#include "util/uthash.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#include "platform/windows/getopt.h"
#else
#include <getopt.h>
#include <dirent.h>
#include <unistd.h>
#endif

typedef struct thread_filter_list {
  uint64_t *values;
  size_t count;
  size_t capacity;
} thread_filter_list_t;

typedef struct report_config {
  const char *log_dir;
  const char *log_file; // Single log file path (alternative to log_dir)
  const char *include_filter;
  const char *exclude_filter;
  thread_filter_list_t threads;
  bool emit_raw_line;
} report_config_t;

typedef struct log_record {
  uint64_t pid;
  uint64_t tid;
  uint64_t seq;
  char *timestamp;
  char *elapsed;
  char *file;
  uint32_t line;
  char *function;
  uint32_t macro_flag;
  char *snippet;
  char *raw_line;
} log_record_t;

typedef struct thread_entry {
  uint64_t thread_id;
  log_record_t record;
  UT_hash_handle hh;
} thread_entry_t;

static const char *macro_flag_label(uint32_t flag) {
  switch (flag) {
  case ASCII_INSTR_SOURCE_PRINT_MACRO_EXPANSION:
    return "expansion";
  case ASCII_INSTR_SOURCE_PRINT_MACRO_INVOCATION:
    return "invocation";
  case ASCII_INSTR_SOURCE_PRINT_MACRO_NONE:
  default:
    return "none";
  }
}

static void thread_filter_list_destroy(thread_filter_list_t *list) {
  if (list == NULL) {
    return;
  }
  SAFE_FREE(list->values);
  list->values = NULL;
  list->count = 0;
  list->capacity = 0;
}

static bool thread_filter_list_append(thread_filter_list_t *list, uint64_t value) {
  if (list == NULL) {
    return false;
  }

  if (list->count == list->capacity) {
    size_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
    uint64_t *new_values = SAFE_REALLOC(list->values, new_capacity * sizeof(uint64_t), uint64_t *);
    if (new_values == NULL) {
      return false;
    }
    list->values = new_values;
    list->capacity = new_capacity;
  }

  list->values[list->count++] = value;
  return true;
}

static bool thread_filter_list_contains(const thread_filter_list_t *list, uint64_t value) {
  if (list == NULL || list->count == 0) {
    return true;
  }

  for (size_t i = 0; i < list->count; ++i) {
    if (list->values[i] == value) {
      return true;
    }
  }
  return false;
}

static const char *resolve_default_log_dir(void) {
  const char *dir = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR");
  if (dir != NULL && dir[0] != '\0') {
    return dir;
  }
  dir = SAFE_GETENV("TMPDIR");
  if (dir != NULL && dir[0] != '\0') {
    return dir;
  }
  dir = SAFE_GETENV("TEMP");
  if (dir != NULL && dir[0] != '\0') {
    return dir;
  }
  dir = SAFE_GETENV("TMP");
  if (dir != NULL && dir[0] != '\0') {
    return dir;
  }
#if defined(_WIN32)
  // Default to current directory on Windows (trace.log is usually in cwd)
  return ".";
#else
  return "/tmp";
#endif
}

static void free_record(log_record_t *record) {
  if (record == NULL) {
    return;
  }
  SAFE_FREE(record->timestamp);
  SAFE_FREE(record->elapsed);
  SAFE_FREE(record->file);
  SAFE_FREE(record->function);
  SAFE_FREE(record->snippet);
  SAFE_FREE(record->raw_line);
  memset(record, 0, sizeof(*record));
}

static void destroy_entries(thread_entry_t **entries) {
  thread_entry_t *current;
  thread_entry_t *tmp;
  HASH_ITER(hh, *entries, current, tmp) {
    HASH_DEL(*entries, current);
    free_record(&current->record);
    SAFE_FREE(current);
  }
}

static char *duplicate_segment(const char *begin, size_t length) {
  char *copy = SAFE_MALLOC(length + 1, char *);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, begin, length);
  copy[length] = '\0';
  return copy;
}

static bool extract_token(const char *line, const char *key, char **out_value) {
  const char *position = strstr(line, key);
  if (position == NULL) {
    return false;
  }
  position += strlen(key);
  const char *end = strchr(position, ' ');
  if (end == NULL) {
    end = line + strlen(line);
  }
  size_t length = (size_t)(end - position);
  *out_value = duplicate_segment(position, length);
  return *out_value != NULL;
}

static bool extract_snippet(const char *line, char **out_value) {
  const char *position = strstr(line, "snippet=");
  if (position == NULL) {
    return false;
  }
  position += strlen("snippet=");
  size_t length = strlen(position);
  while (length > 0 && (position[length - 1] == '\n' || position[length - 1] == '\r')) {
    length--;
  }
  *out_value = duplicate_segment(position, length);
  return *out_value != NULL;
}

static bool extract_uint64(const char *line, const char *key, uint64_t *out_value) {
  const char *position = strstr(line, key);
  if (position == NULL) {
    return false;
  }
  position += strlen(key);
  errno = 0;
  char *endptr = NULL;
  unsigned long long value = strtoull(position, &endptr, 10);
  if (errno != 0 || endptr == position) {
    return false;
  }
  *out_value = (uint64_t)value;
  return true;
}

static bool extract_uint32(const char *line, const char *key, uint32_t *out_value) {
  uint64_t tmp_value = 0;
  if (!extract_uint64(line, key, &tmp_value)) {
    return false;
  }
  if (tmp_value > UINT32_MAX) {
    return false;
  }
  *out_value = (uint32_t)tmp_value;
  return true;
}

static bool parse_log_line(const char *line, log_record_t *record) {
  if (!extract_uint64(line, "pid=", &record->pid)) {
    return false;
  }
  if (!extract_uint64(line, "tid=", &record->tid)) {
    return false;
  }
  if (!extract_uint64(line, "seq=", &record->seq)) {
    return false;
  }
  if (!extract_token(line, "ts=", &record->timestamp)) {
    return false;
  }
  if (!extract_token(line, "elapsed=", &record->elapsed)) {
    return false;
  }
  if (!extract_token(line, "file=", &record->file)) {
    return false;
  }
  if (!extract_uint32(line, "line=", &record->line)) {
    return false;
  }
  if (!extract_token(line, "func=", &record->function)) {
    return false;
  }
  if (!extract_uint32(line, "macro=", &record->macro_flag)) {
    return false;
  }
  if (!extract_snippet(line, &record->snippet)) {
    return false;
  }

  size_t len = strlen(line);
  record->raw_line = duplicate_segment(line, len);
  return record->raw_line != NULL;
}

static bool record_matches_filters(const report_config_t *config, const log_record_t *record) {
  if (config->include_filter != NULL && config->include_filter[0] != '\0') {
    if (record->file == NULL || strstr(record->file, config->include_filter) == NULL) {
      return false;
    }
  }

  if (config->exclude_filter != NULL && config->exclude_filter[0] != '\0') {
    if (record->file != NULL && strstr(record->file, config->exclude_filter) != NULL) {
      return false;
    }
  }

  if (!thread_filter_list_contains(&config->threads, record->tid)) {
    return false;
  }

  return true;
}

static void update_entry(thread_entry_t **entries, const log_record_t *record) {
  thread_entry_t *entry = NULL;
  HASH_FIND(hh, *entries, &record->tid, sizeof(uint64_t), entry);
  if (entry == NULL) {
    entry = SAFE_CALLOC(1, sizeof(*entry), thread_entry_t *);
    entry->thread_id = record->tid;
    entry->record = *record;
    HASH_ADD(hh, *entries, thread_id, sizeof(uint64_t), entry);
    return;
  }

  if (record->seq >= entry->record.seq) {
    free_record(&entry->record);
    entry->record = *record;
  } else {
    free_record((log_record_t *)record);
  }
}

static int compare_entries(const void *lhs, const void *rhs) {
  const thread_entry_t *const *a = lhs;
  const thread_entry_t *const *b = rhs;
  if ((*a)->thread_id < (*b)->thread_id) {
    return -1;
  }
  if ((*a)->thread_id > (*b)->thread_id) {
    return 1;
  }
  return 0;
}

static void print_summary(const report_config_t *config, thread_entry_t **entries) {
  size_t count = HASH_COUNT(*entries);
  if (count == 0) {
    printf("No instrumentation records matched the given filters.\n");
    return;
  }

  thread_entry_t **sorted = SAFE_CALLOC(count, sizeof(*sorted), thread_entry_t **);
  size_t index = 0;
  thread_entry_t *entry = NULL;
  thread_entry_t *tmp = NULL;
  HASH_ITER(hh, *entries, entry, tmp) {
    sorted[index++] = entry;
  }

  qsort(sorted, count, sizeof(*sorted), compare_entries);

  printf("Latest instrumentation record per thread (%zu thread%s)\n", count, count == 1 ? "" : "s");
  printf("======================================================================\n");
  for (size_t i = 0; i < count; ++i) {
    const log_record_t *record = &sorted[i]->record;
    if (config->emit_raw_line) {
      printf("%s\n", record->raw_line);
      continue;
    }

    printf("tid=%" PRIu64 " seq=%" PRIu64 " pid=%" PRIu64 "\n", record->tid, record->seq, record->pid);
    printf("  timestamp : %s\n", record->timestamp);
    printf("  elapsed   : %s\n", record->elapsed);
    printf("  location  : %s:%u\n", record->file != NULL ? record->file : "<unknown>", record->line);
    printf("  function  : %s\n", record->function != NULL ? record->function : "<unknown>");
    printf("  macro     : %s (%u)\n", macro_flag_label(record->macro_flag), record->macro_flag);
    printf("  snippet   : %s\n", record->snippet != NULL ? record->snippet : "<missing>");
    printf("----------------------------------------------------------------------\n");
  }

  SAFE_FREE(sorted);
}

static void usage(FILE *stream, const char *program) {
  fprintf(stream,
          "Usage: %s [options]\n"
          "  --log-file <path>    Single log file to analyze (e.g., trace.log)\n"
          "  --log-dir <path>     Directory containing ascii-instr-*.log files (default: resolve from environment)\n"
          "  --thread <id>        Limit to specific thread ID (repeatable)\n"
          "  --include <substr>   Include records whose file path contains substring\n"
          "  --exclude <substr>   Exclude records whose file path contains substring\n"
          "  --raw                Emit raw log lines instead of formatted summary\n"
          "  --help               Show this help and exit\n",
          program);
}

// Simple pre-scan for --log-file to avoid corrupted optarg from instrumented getopt
// This is a workaround for stack corruption caused by panic instrumentation
static const char *prescan_log_file(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
      return argv[i + 1];
    }
    // Handle --log-file=path format
    if (strncmp(argv[i], "--log-file=", 11) == 0) {
      return argv[i] + 11;
    }
  }
  return NULL;
}

// Check if only --log-file is specified (to skip getopt entirely)
static bool is_simple_log_file_invocation(int argc, char **argv) {
  // Expected patterns: program --log-file path (argc=3) or program --log-file=path (argc=2)
  if (argc == 3 && strcmp(argv[1], "--log-file") == 0) {
    return true;
  }
  if (argc == 2 && strncmp(argv[1], "--log-file=", 11) == 0) {
    return true;
  }
  return false;
}

static bool parse_arguments(int argc, char **argv, report_config_t *config) {
  // Pre-scan for --log-file before getopt (workaround for instrumentation bug)
  const char *log_file_arg = prescan_log_file(argc, argv);
  if (log_file_arg != NULL) {
    config->log_file = log_file_arg;
  }

  // If just --log-file with no other args, skip getopt entirely
  // (instrumented getopt corrupts memory)
  if (config->log_file != NULL && is_simple_log_file_invocation(argc, argv)) {
    return true;
  }

  static const struct option kLongOptions[] = {
      {"log-file", required_argument, NULL, 'f'}, {"log-dir", required_argument, NULL, 'd'},
      {"thread", required_argument, NULL, 't'},   {"include", required_argument, NULL, 'i'},
      {"exclude", required_argument, NULL, 'x'},  {"raw", no_argument, NULL, 'r'},
      {"help", no_argument, NULL, 'h'},           {0, 0, 0, 0},
  };

  int option = 0;
  while ((option = getopt_long(argc, argv, "", kLongOptions, NULL)) != -1) {
    switch (option) {
    case 'f':
      // Already handled by prescan - skip (optarg may be corrupted)
      break;
    case 'd':
      config->log_dir = optarg;
      break;
    case 't': {
      errno = 0;
      unsigned long long tid_value = strtoull(optarg, NULL, 10);
      if (errno != 0) {
        log_error("Invalid thread id: %s", optarg);
        return false;
      }
      if (!thread_filter_list_append(&config->threads, (uint64_t)tid_value)) {
        log_error("Failed to record thread filter");
        return false;
      }
      break;
    }
    case 'i':
      config->include_filter = optarg;
      break;
    case 'x':
      config->exclude_filter = optarg;
      break;
    case 'r':
      config->emit_raw_line = true;
      break;
    case 'h':
      usage(stdout, argv[0]);
      return false;
    default:
      usage(stderr, argv[0]);
      return false;
    }
  }

  if (optind < argc) {
    log_error("Unexpected positional argument: %s", argv[optind]);
    usage(stderr, argv[0]);
    return false;
  }

  // Validate --log-file and --log-dir are mutually exclusive
  if (config->log_file != NULL && config->log_dir != NULL) {
    log_error("Cannot specify both --log-file and --log-dir");
    return false;
  }

  // Fall back to default log directory only if no log file specified
  if (config->log_file == NULL && config->log_dir == NULL) {
    config->log_dir = resolve_default_log_dir();
  }

  return true;
}

static bool process_file(const report_config_t *config, const char *path, thread_entry_t **entries) {
  FILE *handle = platform_fopen(path, "r");
  if (handle == NULL) {
    log_warn("Cannot open log file '%s': %s", path, SAFE_STRERROR(errno));
    return false;
  }

  char buffer[8192];
  while (fgets(buffer, sizeof(buffer), handle) != NULL) {
    log_record_t record = {0};
    if (!parse_log_line(buffer, &record)) {
      free_record(&record);
      continue;
    }
    if (!record_matches_filters(config, &record)) {
      free_record(&record);
      continue;
    }
    update_entry(entries, &record);
  }

  fclose(handle);
  return true;
}

#if defined(_WIN32)
static bool build_windows_glob_pattern(char *buffer, size_t capacity, const char *directory, const char *suffix) {
  if (buffer == NULL || directory == NULL || suffix == NULL) {
    return false;
  }

  size_t length = 0;
  for (const char *cursor = directory; *cursor != '\0'; ++cursor) {
    char ch = *cursor == '/' ? '\\' : *cursor;
    if (length + 1 >= capacity) {
      return false;
    }
    buffer[length++] = ch;
  }
  if (length == 0) {
    return false;
  }
  if (buffer[length - 1] != '\\') {
    if (length + 1 >= capacity) {
      return false;
    }
    buffer[length++] = '\\';
  }
  const size_t suffix_length = strlen(suffix);
  if (length + suffix_length >= capacity) {
    return false;
  }
  memcpy(buffer + length, suffix, suffix_length + 1);
  return true;
}
#endif

static bool collect_entries(const report_config_t *config, thread_entry_t **entries) {
  // If a single log file is specified, just process that file
  if (config->log_file != NULL) {
    return process_file(config, config->log_file, entries);
  }

#if defined(_WIN32)
  char pattern[MAX_PATH];
  if (!build_windows_glob_pattern(pattern, sizeof(pattern), config->log_dir, "ascii-instr-*.log")) {
    log_error("Instrumentation log directory path is too long: %s", config->log_dir);
    return false;
  }

  struct _finddata_t data = {0};
  intptr_t handle = _findfirst(pattern, &data);
  if (handle == -1) {
    log_error("Unable to open instrumentation log directory '%s': %s", config->log_dir, SAFE_STRERROR(errno));
    return false;
  }

  bool success = true;
  do {
    if ((data.attrib & _A_SUBDIR) != 0) {
      continue;
    }
    size_t name_len = strlen(data.name);
    if (name_len < 4 || strcmp(data.name + name_len - 4, ".log") != 0) {
      continue;
    }

    char path_buffer[MAX_PATH];
    int written = snprintf(path_buffer, sizeof(path_buffer), "%s/%s", config->log_dir, data.name);
    if (written < 0 || written >= (int)sizeof(path_buffer)) {
      log_warn("Skipping path that exceeds buffer: %s/%s", config->log_dir, data.name);
      continue;
    }
    if (!process_file(config, path_buffer, entries)) {
      success = false;
    }
  } while (_findnext(handle, &data) == 0);

  _findclose(handle);
  return success;
#else
  DIR *directory = opendir(config->log_dir);
  if (directory == NULL) {
    log_error("Unable to open instrumentation log directory '%s': %s", config->log_dir, SAFE_STRERROR(errno));
    return false;
  }

  struct dirent *entry = NULL;
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    if (strncmp(entry->d_name, "ascii-instr-", strlen("ascii-instr-")) != 0) {
      continue;
    }
    size_t name_length = strlen(entry->d_name);
    if (name_length < 5 || strcmp(entry->d_name + name_length - 4, ".log") != 0) {
      continue;
    }

    char path_buffer[PATH_MAX];
    int written = snprintf(path_buffer, sizeof(path_buffer), "%s/%s", config->log_dir, entry->d_name);
    if (written < 0 || written >= (int)sizeof(path_buffer)) {
      log_warn("Skipping path that exceeds buffer: %s/%s", config->log_dir, entry->d_name);
      continue;
    }

    process_file(config, path_buffer, entries);
  }

  closedir(directory);
  return true;
#endif
}

int main(int argc, char **argv) {
  report_config_t config = {
      .log_dir = NULL,
      .log_file = NULL,
      .include_filter = NULL,
      .exclude_filter = NULL,
      .threads = {.values = NULL, .count = 0, .capacity = 0},
      .emit_raw_line = false,
  };

  log_init(NULL, LOG_INFO);

  int exit_code = EXIT_SUCCESS;
  if (!parse_arguments(argc, argv, &config)) {
    exit_code = ERROR_USAGE;
    goto cleanup;
  }

  thread_entry_t *entries = NULL;
  if (!collect_entries(&config, &entries)) {
    exit_code = ERROR_GENERAL;
    destroy_entries(&entries);
    goto cleanup;
  }

  print_summary(&config, &entries);
  destroy_entries(&entries);

cleanup:
  thread_filter_list_destroy(&config.threads);
  log_destroy();
  return exit_code;
}
