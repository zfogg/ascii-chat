#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/tooling/panic/instrument_log.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/thread.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

static void setup_quiet_logging(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  log_set_level(LOG_FATAL);
}

static void restore_logging(void) {
  log_set_terminal_output(true);
  log_set_level(LOG_DEBUG);
  log_destroy();
}

TestSuite(instrument_log, .init = setup_quiet_logging, .fini = restore_logging);

static const char *default_temp_base(void) {
  const char *env = SAFE_GETENV("TMPDIR");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  env = SAFE_GETENV("TEMP");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  env = SAFE_GETENV("TMP");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  return "/tmp";
}

static void make_unique_directory(char *buffer, size_t buffer_size) {
  const char *base = default_temp_base();
  const int pid = platform_get_pid();
  const uint64_t tid = thread_current_id();

  for (int attempt = 0; attempt < 64; ++attempt) {
    int written = snprintf(buffer, buffer_size, "%s/ascii-instr-test-%d-%" PRIu64 "-%d", base, pid, tid, attempt);
    cr_assert_lt(written, (int)buffer_size, "Temporary path truncated");
    if (written < 0) {
      continue;
    }

#ifdef _WIN32
    if (_mkdir(buffer) == 0) {
      return;
    }
#else
    if (mkdir(buffer, 0700) == 0) {
      return;
    }
#endif
    if (errno != EEXIST) {
      cr_assert(false, "Failed to create temporary directory '%s': %s", buffer, strerror(errno));
    }
  }

  cr_assert(false, "Unable to allocate unique temporary directory after multiple attempts");
}

#if defined(_WIN32)
static bool build_windows_search_pattern(char *buffer, size_t capacity, const char *directory, const char *suffix) {
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
  size_t suffix_len = strlen(suffix);
  if (length + suffix_len >= capacity) {
    return false;
  }
  memcpy(buffer + length, suffix, suffix_len + 1);
  return true;
}
#endif

static void remove_directory_recursively(const char *path) {
#if defined(_WIN32)
  char pattern[MAX_PATH];
  if (build_windows_search_pattern(pattern, sizeof(pattern), path, "*") == false) {
    (void)_rmdir(path);
    return;
  }

  struct _finddata_t data = {0};
  intptr_t handle = _findfirst(pattern, &data);
  if (handle != -1) {
    do {
      if (strcmp(data.name, ".") == 0 || strcmp(data.name, "..") == 0) {
        continue;
      }

      char target[MAX_PATH];
      int written = snprintf(target, sizeof(target), "%s/%s", path, data.name);
      if (written < 0 || written >= (int)sizeof(target)) {
        continue;
      }

      if ((data.attrib & _A_SUBDIR) != 0) {
        remove_directory_recursively(target);
      } else {
        (void)_unlink(target);
      }
    } while (_findnext(handle, &data) == 0);
    _findclose(handle);
  }

  (void)_rmdir(path);
#else
  DIR *dir = opendir(path);
  if (dir != NULL) {
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      char target[PATH_MAX];
      int written = snprintf(target, sizeof(target), "%s/%s", path, entry->d_name);
      if (written < 0 || written >= (int)sizeof(target)) {
        continue;
      }
      (void)unlink(target);
    }
    closedir(dir);
  }

  (void)rmdir(path);
#endif
}

static bool find_log_file(const char *directory, char *path_out, size_t path_capacity) {
#if defined(_WIN32)
  char pattern[MAX_PATH];
  if (!build_windows_search_pattern(pattern, sizeof(pattern), directory, "ascii-instr-*.log")) {
    return false;
  }

  struct _finddata_t data = {0};
  intptr_t handle = _findfirst(pattern, &data);
  if (handle == -1) {
    return false;
  }

  bool found = false;
  do {
    int written = snprintf(path_out, path_capacity, "%s/%s", directory, data.name);
    if (written >= 0 && written < (int)path_capacity) {
      found = true;
      break;
    }
  } while (_findnext(handle, &data) == 0);

  _findclose(handle);
  return found;
#else
  DIR *dir = opendir(directory);
  if (dir == NULL) {
    return false;
  }

  bool found = false;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strncmp(entry->d_name, "ascii-instr-", strlen("ascii-instr-")) != 0) {
      continue;
    }
    size_t name_len = strlen(entry->d_name);
    if (name_len < 4 || strcmp(entry->d_name + name_len - 4, ".log") != 0) {
      continue;
    }

    int written = snprintf(path_out, path_capacity, "%s/%s", directory, entry->d_name);
    if (written < 0 || written >= (int)path_capacity) {
      continue;
    }
    found = true;
    break;
  }

  closedir(dir);
  return found;
#endif
}

static void clear_filter_environment(void) {
#ifdef _WIN32
  _putenv_s("ASCII_INSTR_SOURCE_PRINT_INCLUDE", "");
  _putenv_s("ASCII_INSTR_SOURCE_PRINT_EXCLUDE", "");
  _putenv_s("ASCII_INSTR_SOURCE_PRINT_THREAD", "");
  _putenv_s("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", "");
#else
  unsetenv("ASCII_INSTR_SOURCE_PRINT_INCLUDE");
  unsetenv("ASCII_INSTR_SOURCE_PRINT_EXCLUDE");
  unsetenv("ASCII_INSTR_SOURCE_PRINT_THREAD");
  unsetenv("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR");
#endif
}

static void set_env_variable(const char *key, const char *value) {
#ifdef _WIN32
  if (value == NULL) {
    _putenv_s(key, "");
  } else {
    _putenv_s(key, value);
  }
#else
  if (value == NULL) {
    unsetenv(key);
  } else {
    setenv(key, value, 1);
  }
#endif
}

static void write_sample_record(const char *file_path) {
  /* Source-print instrumentation can initialize before a test sets its filters. */
  asciichat_instr_runtime_global_destroy();
  asciichat_instr_log_line(file_path, 42, "test_function", "value = 42;", 0);
  asciichat_instr_runtime_global_destroy();
}
// Use verbose logging with debug level enabled and stdout/stderr not disabled

Test(instrument_log, writes_log_with_defaults) {
  char temp_dir[PATH_MAX];
  make_unique_directory(temp_dir, sizeof(temp_dir));

  clear_filter_environment();
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", temp_dir);

  write_sample_record("lib/runtime_test.c");

  char log_path[PATH_MAX];
  cr_assert(find_log_file(temp_dir, log_path, sizeof(log_path)), "Expected instrumentation log file to be created");

  FILE *log_file = fopen(log_path, "r");
  cr_assert_not_null(log_file, "Failed to open instrumentation log file");

  char buffer[4096];
  cr_assert_not_null(fgets(buffer, sizeof(buffer), log_file), "Instrumentation log should contain data");
  fclose(log_file);

  cr_expect(strstr(buffer, "file=lib/runtime_test.c") != NULL, "Log should include original file path");
  cr_expect(strstr(buffer, "snippet=value = 42;") != NULL, "Log should include statement snippet");

  remove_directory_recursively(temp_dir);
  clear_filter_environment();
}

Test(instrument_log, include_filter_drops_non_matching_files) {
  char temp_dir[PATH_MAX];
  make_unique_directory(temp_dir, sizeof(temp_dir));

  clear_filter_environment();
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", temp_dir);
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_INCLUDE", "server.c");

  write_sample_record("lib/client.c");

  char log_path[PATH_MAX];
  cr_expect(!find_log_file(temp_dir, log_path, sizeof(log_path)), "Include filter should suppress non-matching file");

  remove_directory_recursively(temp_dir);
  clear_filter_environment();
}

Test(instrument_log, thread_filter_blocks_unlisted_thread) {
  char temp_dir[PATH_MAX];
  make_unique_directory(temp_dir, sizeof(temp_dir));

  clear_filter_environment();
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", temp_dir);

  uint64_t tid = thread_current_id();
  char tid_buffer[64];
  snprintf(tid_buffer, sizeof(tid_buffer), "%" PRIu64, tid + 1);
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_THREAD", tid_buffer);

  write_sample_record("lib/runtime_test.c");

  char log_path[PATH_MAX];
  cr_expect(!find_log_file(temp_dir, log_path, sizeof(log_path)), "Thread filter should block non-listed thread");

  remove_directory_recursively(temp_dir);
  clear_filter_environment();
}

Test(instrument_log, thread_filter_allows_matching_thread) {
  char temp_dir[PATH_MAX];
  make_unique_directory(temp_dir, sizeof(temp_dir));

  clear_filter_environment();
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", temp_dir);

  uint64_t tid = thread_current_id();
  char tid_buffer[64];
  snprintf(tid_buffer, sizeof(tid_buffer), "%" PRIu64, tid);
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_THREAD", tid_buffer);

  write_sample_record("lib/runtime_test.c");

  char log_path[PATH_MAX];
  cr_assert(find_log_file(temp_dir, log_path, sizeof(log_path)), "Thread filter should allow listed thread");

  FILE *log_file = fopen(log_path, "r");
  cr_assert_not_null(log_file);
  char buffer[4096];
  cr_assert_not_null(fgets(buffer, sizeof(buffer), log_file));
  fclose(log_file);
  cr_expect(strstr(buffer, "tid=") != NULL, "Log should contain thread identifier");

  remove_directory_recursively(temp_dir);
  clear_filter_environment();
}

static void reset_extended_filters(void) {
  static const char *keys[] = {"ASCII_INSTR_SOURCE_PRINT_ONLY",
                               "ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE",
                               "ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE",
                               "ASCII_INSTR_SOURCE_PRINT_INCLUDE_REGEX",
                               "ASCII_INSTR_SOURCE_PRINT_EXCLUDE_REGEX",
                               "ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE_REGEX",
                               "ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE_REGEX",
                               "ASCII_INSTR_SOURCE_PRINT_ENABLE",
                               "ASCII_INSTR_SOURCE_PRINT_RATE",
                               "ASCII_INSTR_SOURCE_PRINT_ENABLE_COVERAGE",
                               "ASCII_CHAT_DEBUG_SELF_SOURCE_CODE_LOG_FILE",
                               "ASCII_CHAT_DEBUG_SELF_SOURCE_CODE_LOG_STDERR",
                               "ASCII_INSTR_SOURCE_PRINT_ECHO_STDERR"};
  asciichat_instr_runtime_global_destroy();
  clear_filter_environment();
  for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); ++i)
    set_env_variable(keys[i], NULL);
}

Test(instrument_log, selector_and_regex_filters_write_only_the_selected_records) {
  const struct {
    const char *key, *value;
    unsigned mask;
  } cases[] = {
      {"ONLY", "network", 11},
      {"ONLY", "module=network", 3},
      {"ONLY", " module = tcp : s*.c ", 1},
      {"ONLY", "network:recv.c", 2},
      {"ONLY", "module=network:", 3},
      {"ONLY", "func=send_*", 9},
      {"ONLY", "function=*packet", 3},
      {"ONLY", "file=*udp/?e?v.c", 2},
      {"ONLY", "unknown=*audio*", 4},
      {"ONLY", " func=mix_*, module=tcp ", 5},
      {"ONLY", " , , func=, module=:x ", 15},
      {"ONLY", "module=not-a-module", 0},
      {"ONLY", "file=unmatched*", 0},
      {"INCLUDE", "network", 11},
      {"EXCLUDE", "audio", 11},
      {"FUNCTION_INCLUDE", "packet", 3},
      {"FUNCTION_EXCLUDE", "packet", 12},
#ifndef _WIN32
      {"INCLUDE_REGEX", "^lib/network/.*[.]c$", 3},
      {"EXCLUDE_REGEX", "recv[.]c$", 13},
      {"FUNCTION_INCLUDE_REGEX", "^send_", 9},
      {"FUNCTION_EXCLUDE_REGEX", "_packet$", 12},
      {"INCLUDE_REGEX", "[", 15},
#endif
  };
  const char *paths[] = {"lib/network/tcp/send.c", "lib/network/udp/recv.c", "lib/audio/mixer.c",
                         "src/common/networker.c"};
  const char *functions[] = {"send_packet", "receive_packet", "mix_audio", "send_status"};
  const char *markers[] = {"marker-zero", "marker-one", "marker-two", "marker-three"};
  for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); ++c) {
    reset_extended_filters();
    char directory[PATH_MAX], key[128];
    make_unique_directory(directory, sizeof(directory));
    set_env_variable("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", directory);
    safe_snprintf(key, sizeof(key), "ASCII_INSTR_SOURCE_PRINT_%s", cases[c].key);
    set_env_variable(key, cases[c].value);
    for (size_t i = 0; i < 4; ++i)
      asciichat_instr_log_line(paths[i], (uint32_t)(10 + i), functions[i], markers[i], 0);
    asciichat_instr_runtime_global_destroy();
    char path[PATH_MAX], contents[16384] = {0};
    bool found = find_log_file(directory, path, sizeof(path));
    if (found) {
      FILE *file = fopen(path, "r");
      cr_assert_not_null(file);
      size_t length = fread(contents, 1, sizeof(contents) - 1, file);
      cr_assert_not(ferror(file));
      fclose(file);
      contents[length] = 0;
    }
    for (size_t i = 0; i < 4; ++i)
      cr_assert_eq(strstr(contents, markers[i]) != NULL, (cases[c].mask & (1u << i)) != 0, "%s=%s record %zu",
                   cases[c].key, cases[c].value, i);
    remove_directory_recursively(directory);
  }
  reset_extended_filters();
}

Test(instrument_log, sampling_and_disabled_values_control_persisted_records) {
  const char *values[] = {"0", "false", "OFF", "  no"};
  for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
    reset_extended_filters();
    char directory[PATH_MAX], path[PATH_MAX];
    make_unique_directory(directory, sizeof(directory));
    set_env_variable("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", directory);
    set_env_variable("ASCII_INSTR_SOURCE_PRINT_ENABLE", values[i]);
    asciichat_instr_log_line("sample.c", 1, "sample", "must-not-appear", 0);
    asciichat_instr_runtime_global_destroy();
    cr_assert_not(find_log_file(directory, path, sizeof(path)));
    remove_directory_recursively(directory);
  }
  reset_extended_filters();
  char directory[PATH_MAX], path[PATH_MAX];
  make_unique_directory(directory, sizeof(directory));
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", directory);
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_RATE", "  3");
  const char *markers[] = {"sample-A", "sample-B", "sample-C", "sample-D", "sample-E", "sample-F", "sample-G"};
  for (size_t i = 0; i < 7; ++i)
    asciichat_instr_log_line("sample.c", 1, "sample", markers[i], 0);
  asciichat_instr_runtime_global_destroy();
  cr_assert(find_log_file(directory, path, sizeof(path)));
  FILE *file = fopen(path, "r");
  cr_assert_not_null(file);
  char contents[16384] = {0};
  size_t length = fread(contents, 1, sizeof(contents) - 1, file);
  fclose(file);
  contents[length] = 0;
  for (size_t i = 0; i < 7; ++i)
    cr_assert_eq(strstr(contents, markers[i]) != NULL, i % 3 == 0, "sample %zu", i);
  remove_directory_recursively(directory);
  reset_extended_filters();
}

Test(instrument_log, custom_trace_files_append_escape_snippets_and_record_program_counters) {
  reset_extended_filters();
  char directory[PATH_MAX], path[PATH_MAX];
  make_unique_directory(directory, sizeof(directory));
  safe_snprintf(path, sizeof(path), "%s/selected.log", directory);
  set_env_variable("ASCII_CHAT_DEBUG_SELF_SOURCE_CODE_LOG_FILE", path);
  set_env_variable("ASCII_INSTR_SOURCE_PRINT_ENABLE_COVERAGE", "yes");
  cr_assert(asciichat_instr_coverage_enabled());
  asciichat_instr_log_line(NULL, 17, NULL, "first\nsecond\rthird\tend", ASCII_INSTR_SOURCE_PRINT_MACRO_EXPANSION);
  asciichat_instr_log_pc((uintptr_t)0x1234);
  asciichat_instr_runtime_global_destroy();
  asciichat_instr_log_line("append.c", 18, "append", "append-marker", ASCII_INSTR_SOURCE_PRINT_MACRO_INVOCATION);
  asciichat_instr_runtime_global_destroy();
  FILE *file = fopen(path, "r");
  cr_assert_not_null(file);
  char line[4096];
  unsigned count = 0;
  while (fgets(line, sizeof(line), file)) {
    if (count == 0) {
      cr_assert_not_null(strstr(line, "file=<unknown> line=17 func=<unknown> macro=1"));
      cr_assert_not_null(strstr(line, "snippet=first\\nsecond\\rthird\\tend\n"));
    } else if (count == 1) {
      cr_assert_not_null(strstr(line, "file=__coverage__"));
      cr_assert_not_null(strstr(line, "snippet=pc=0x1234"));
    } else if (count == 2) {
      cr_assert_not_null(strstr(line, "file=append.c line=18 func=append macro=2 snippet=append-marker"));
    }
    ++count;
  }
  cr_assert_not(ferror(file));
  fclose(file);
  cr_assert_eq(count, 3);
  remove_directory_recursively(directory);
  reset_extended_filters();
  cr_assert_not(asciichat_instr_coverage_enabled());
  asciichat_instr_runtime_global_destroy();
}
