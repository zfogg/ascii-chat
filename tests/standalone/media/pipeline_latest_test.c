#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>

#define g_snapshot_first_frame_rendered test_pipeline_snapshot_first_frame_rendered
bool g_snapshot_first_frame_rendered = false;
#include "../../../src/common/session/pipeline.c"

#define CHECK(value)                                                                                                   \
  do {                                                                                                                 \
    if (!(value))                                                                                                      \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #value);                                                  \
  } while (0)

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  frame_queue_t *queue = frame_queue_create(1, "latest_frame_test");
  CHECK(queue);
  unsigned frames[1000];
  for (unsigned i = 0; i < 1000; ++i) {
    frames[i] = i;
    void *retired = frame_queue_replace_latest(queue, &frames[i]);
    CHECK(retired == (i ? &frames[i - 1] : NULL));
    CHECK(frame_queue_count(queue) == 1);
  }
  CHECK(frame_queue_pop(queue, 1) == &frames[999]);
  CHECK(frame_queue_count(queue) == 0);
  CHECK(frame_queue_pop(queue, 1) == NULL);
  CHECK(frame_queue_replace_latest(queue, &frames[0]) == NULL);
  CHECK(frame_queue_pop(queue, 1) == &frames[0]);
  frame_queue_destroy(queue);
  return 0;
}
