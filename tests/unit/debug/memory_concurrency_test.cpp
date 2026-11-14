#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/memory.h>
#include <libplatform/thread.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>

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
TEST_CASE("memory_concurrency/aligned_ownership_and_reallocation_are_reflected_in_live_reports", "[memory_concurrency]") {
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(report_directory, sizeof(report_directory), "memory-report"), ASCIICHAT_OK);
  char path[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(path, sizeof(path), "%s/report.txt", report_directory);
  report_file = platform_fopen("memory-report-test", path, "w+b");
  ASCIICHAT_REQUIRE_NOT_NULL(report_file);
  saved_stderr = platform_dup(STDERR_FILENO);
  ASCIICHAT_REQUIRE_GE(saved_stderr, 0);
  ASCIICHAT_REQUIRE_EQ(platform_dup2(platform_fileno(report_file), STDERR_FILENO), ASCIICHAT_OK);
  debug_memory_set_quiet_mode(false);
  unsigned char *aligned = SAFE_MALLOC_ALIGNED(256, 64, unsigned char *);
  ASCIICHAT_REQUIRE_NOT_NULL(aligned);
  ASCIICHAT_REQUIRE_EQ((uintptr_t)aligned % 64, 0);
  debug_track_aligned(NULL, 256, "aligned-report-fixture.c", 9001);
  memset(aligned, 0x5a, 256);
  debug_memory_report(true);
  FILE *report = report_file;
  ASCIICHAT_REQUIRE_EQ(fseek(report, 0, SEEK_SET), 0);
  char text[32768];
  size_t length = fread(text, 1, sizeof(text) - 1, report);
  text[length] = '\0';
  const char *allocation_heading = "Allocation from tests/unit/debug/memory_concurrency_test.c:";
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, allocation_heading));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "1 live 256 B total"));
  debug_free(aligned, __FILE__, __LINE__);
  aligned = NULL;
  unsigned char *block = static_cast<unsigned char *>(debug_realloc(NULL, 128, "realloc-report-fixture.c", 9002));
  ASCIICHAT_REQUIRE_NOT_NULL(block);
  memset(block, 0x6b, 128);
  block = static_cast<unsigned char *>(debug_realloc(block, 512, "realloc-report-fixture.c", 9002));
  ASCIICHAT_REQUIRE_NOT_NULL(block);
  for (size_t i = 0; i < 128; ++i)
    ASCIICHAT_REQUIRE_EQ(block[i], 0x6b);
  block = static_cast<unsigned char *>(debug_realloc(block, 16, "realloc-report-fixture.c", 9002));
  ASCIICHAT_REQUIRE_NOT_NULL(block);
  for (size_t i = 0; i < 16; ++i)
    ASCIICHAT_REQUIRE_EQ(block[i], 0x6b);
  block = static_cast<unsigned char *>(debug_realloc(block, 0, "realloc-report-fixture.c", 9002));
  ASCIICHAT_REQUIRE_NULL(block);
  debug_memory_report(true);
  ASCIICHAT_REQUIRE_EQ(fseek(report, (long)length, SEEK_SET), 0);
  length = fread(text, 1, sizeof(text) - 1, report);
  text[length] = '\0';
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "=== Memory Report ==="));
  ASCIICHAT_REQUIRE_NULL(strstr(text, allocation_heading));
  ASCIICHAT_REQUIRE_NULL(strstr(text, "realloc-report-fixture.c"));
}
#endif

typedef struct {
  unsigned char value;
  bool valid;
} allocation_worker_t;

static void *allocate_concurrently(void *argument) {
  allocation_worker_t *worker = static_cast<allocation_worker_t *>(argument);
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

TEST_CASE("memory_concurrency/allocation_tracking_is_serialized_across_threads", "[memory_concurrency]") {
  allocation_worker_t workers[8];
  platform_thread_t threads[8];
  for (int i = 0; i < 8; i++) {
    workers[i] = (allocation_worker_t){.value = (unsigned char)(i + 1)};
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&threads[i], "allocation-regression", allocate_concurrently, &workers[i]), 0);
  }
  for (int i = 0; i < 8; i++) {
    ASCIICHAT_REQUIRE_EQ(thread_join(&threads[i], NULL), 0);
    ASCIICHAT_REQUIRE(workers[i].valid, "Concurrent allocation/reallocation must preserve each thread's data");
  }
}
