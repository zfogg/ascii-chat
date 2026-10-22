#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>

#ifdef DEBUG_MEMORY
static char report_directory[PLATFORM_MAX_PATH_LENGTH];
static FILE *report_file;
static int saved_stderr = -1;
static void memory_report_cleanup(void) {
  if (saved_stderr >= 0) {
    (void)platform_dup2(saved_stderr, STDERR_FILENO);
    platform_close(saved_stderr);
    saved_stderr = -1;
  }
  if (report_file) {
    fclose(report_file);
    report_file = NULL;
  }
  if (report_directory[0])
    platform_rmdir_recursive(report_directory);
  debug_memory_set_quiet_mode(true);
}
Test(memory_concurrency, aligned_ownership_and_reallocation_are_reflected_in_live_reports,
     .fini = memory_report_cleanup, .timeout = 10) {
  cr_assert_eq(platform_mkdtemp(report_directory, sizeof(report_directory), "memory-report"), ASCIICHAT_OK);
  char path[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(path, sizeof(path), "%s/report.txt", report_directory);
  report_file = platform_fopen("memory-report-test", path, "w+b");
  cr_assert_not_null(report_file);
  saved_stderr = platform_dup(STDERR_FILENO);
  cr_assert_geq(saved_stderr, 0);
  cr_assert_eq(platform_dup2(platform_fileno(report_file), STDERR_FILENO), ASCIICHAT_OK);
  debug_memory_set_quiet_mode(false);
  unsigned char *aligned = SAFE_MALLOC_ALIGNED(256, 64, unsigned char *);
  cr_assert_not_null(aligned);
  cr_assert_eq((uintptr_t)aligned % 64, 0);
  debug_track_aligned(NULL, 256, "aligned-report-fixture.c", 9001);
  memset(aligned, 0x5a, 256);
  debug_memory_report();
  FILE *report = report_file;
  cr_assert_eq(fseek(report, 0, SEEK_SET), 0);
  char text[32768];
  size_t length = fread(text, 1, sizeof(text) - 1, report);
  text[length] = '\0';
  const char *filename = strrchr(__FILE__, '/');
  if (!filename)
    filename = strrchr(__FILE__, '\\');
  filename = filename ? filename + 1 : __FILE__;
  cr_assert_not_null(strstr(text, filename));
  cr_assert_not_null(strstr(text, "1 live 256 B total"));
  debug_free(aligned, __FILE__, __LINE__);
  aligned = NULL;
  unsigned char *block = debug_realloc(NULL, 128, "realloc-report-fixture.c", 9002);
  cr_assert_not_null(block);
  memset(block, 0x6b, 128);
  block = debug_realloc(block, 512, "realloc-report-fixture.c", 9002);
  cr_assert_not_null(block);
  for (size_t i = 0; i < 128; ++i)
    cr_assert_eq(block[i], 0x6b);
  block = debug_realloc(block, 16, "realloc-report-fixture.c", 9002);
  cr_assert_not_null(block);
  for (size_t i = 0; i < 16; ++i)
    cr_assert_eq(block[i], 0x6b);
  block = debug_realloc(block, 0, "realloc-report-fixture.c", 9002);
  cr_assert_null(block);
  debug_memory_report();
  cr_assert_eq(fseek(report, (long)length, SEEK_SET), 0);
  length = fread(text, 1, sizeof(text) - 1, report);
  text[length] = '\0';
  cr_assert_not_null(strstr(text, "=== Memory Report ==="));
  cr_assert_null(strstr(text, filename));
  cr_assert_null(strstr(text, "realloc-report-fixture.c"));
}
#endif

typedef struct {
  unsigned char value;
  bool valid;
} allocation_worker_t;

static void *allocate_concurrently(void *argument) {
  allocation_worker_t *worker = argument;
  worker->valid = true;
  for (int iteration = 0; iteration < 1000; iteration++) {
    unsigned char *blocks[4];
    for (int i = 0; i < 4; i++) {
      blocks[i] = SAFE_CALLOC(128, sizeof(unsigned char), unsigned char *);
      memset(blocks[i], worker->value, 128);
    }
    for (int i = 0; i < 4; i++) {
      blocks[i] = SAFE_REALLOC(blocks[i], 256, unsigned char *);
      for (int j = 0; j < 128; j++)
        if (blocks[i][j] != worker->value)
          worker->valid = false;
      SAFE_FREE(blocks[i]);
    }
  }
  return NULL;
}

Test(memory_concurrency, allocation_tracking_is_serialized_across_threads, .timeout = 30) {
  allocation_worker_t workers[8];
  asciichat_thread_t threads[8];
  for (int i = 0; i < 8; i++) {
    workers[i] = (allocation_worker_t){.value = (unsigned char)(i + 1)};
    cr_assert_eq(acthread_create(&threads[i], "allocation-regression", allocate_concurrently, &workers[i]), 0);
  }
  for (int i = 0; i < 8; i++) {
    cr_assert_eq(thread_join(&threads[i], NULL), 0);
    cr_assert(workers[i].valid, "Concurrent allocation/reallocation must preserve each thread's data");
  }
}
