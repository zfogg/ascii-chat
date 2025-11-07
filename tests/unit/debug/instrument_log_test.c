#include "tests/common.h"

#include "debug/instrument_log.h"
#include "platform/system.h"
#include "platform/thread.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

static void setup_quiet_logging(void) {
  log_init(NULL, LOG_FATAL);
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
  const uint64_t tid = ascii_thread_current_id();

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
      cr_assert_fail("Failed to create temporary directory '%s': %s", buffer, strerror(errno));
    }
  }

  cr_assert_fail("Unable to allocate unique temporary directory after multiple attempts");
}

static void remove_directory_recursively(const char *path) {
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

#ifdef _WIN32
  (void)_rmdir(path);
#else
  (void)rmdir(path);
#endif
}

static bool find_log_file(const char *directory, char *path_out, size_t path_capacity) {
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
}

static void clear_filter_environment(void) {
#ifdef _WIN32
  _putenv_s("ASCII_INSTR_INCLUDE", "");
  _putenv_s("ASCII_INSTR_EXCLUDE", "");
  _putenv_s("ASCII_INSTR_THREAD", "");
  _putenv_s("ASCII_INSTR_OUTPUT_DIR", "");
#else
  unsetenv("ASCII_INSTR_INCLUDE");
  unsetenv("ASCII_INSTR_EXCLUDE");
  unsetenv("ASCII_INSTR_THREAD");
  unsetenv("ASCII_INSTR_OUTPUT_DIR");
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
  ascii_instr_log_line(file_path, 42, "test_function", "value = 42;", 0);
  ascii_instr_runtime_global_shutdown();
}

Test(instrument_log, writes_log_with_defaults) {
  char temp_dir[PATH_MAX];
  make_unique_directory(temp_dir, sizeof(temp_dir));

  clear_filter_environment();
  set_env_variable("ASCII_INSTR_OUTPUT_DIR", temp_dir);

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
  set_env_variable("ASCII_INSTR_OUTPUT_DIR", temp_dir);
  set_env_variable("ASCII_INSTR_INCLUDE", "server.c");

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
  set_env_variable("ASCII_INSTR_OUTPUT_DIR", temp_dir);

  uint64_t tid = ascii_thread_current_id();
  char tid_buffer[64];
  snprintf(tid_buffer, sizeof(tid_buffer), "%" PRIu64, tid + 1);
  set_env_variable("ASCII_INSTR_THREAD", tid_buffer);

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
  set_env_variable("ASCII_INSTR_OUTPUT_DIR", temp_dir);

  uint64_t tid = ascii_thread_current_id();
  char tid_buffer[64];
  snprintf(tid_buffer, sizeof(tid_buffer), "%" PRIu64, tid);
  set_env_variable("ASCII_INSTR_THREAD", tid_buffer);

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
