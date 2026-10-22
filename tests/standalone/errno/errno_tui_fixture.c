#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/ui/errno_stacks.h>
#include <stdatomic.h>
#include <string.h>

typedef struct {
  atomic_bool ready;
  atomic_bool stop;
} fixture_state_t;

static void *publish_long_error(void *opaque) {
  fixture_state_t *state = opaque;
  NAMED_REGISTER_ID(thread_current_id(), "websocket-error-proxy-worker-with-a-long-name", "thread", "0x%tx", NULL);
  SET_ERRNO(ERROR_CRYPTO_INIT,
            "USER_MESSAGE_MARKER_the_websocket_error_proxy_returned_a_diagnostic_payload_that_must_not_wrap");
  atomic_store(&state->ready, true);
  while (!atomic_load(&state->stop))
    platform_sleep_ms(10);
  CLEAR_ERRNO_ALL();
  NAMED_UNREGISTER_ID(thread_current_id());
  return NULL;
}

int main(int argc, char **argv) {
  if (asciichat_shared_init(NULL, false, true) != ASCIICHAT_OK)
    return 1;

  char *options[32] = {"errno-tui-fixture", "--color=true", "mirror", NULL};
  size_t count = 3;
  for (int i = 1; i < argc && count + 1 < sizeof(options) / sizeof(options[0]); ++i)
    options[count++] = argv[i];
  if (options_init((int)count, options) != ASCIICHAT_OK)
    return 2;

  fixture_state_t state = {0};
  asciichat_thread_t worker;
  if (acthread_create(&worker, "errno-tui-fixture", publish_long_error, &state) != ASCIICHAT_OK)
    return 3;
  while (!atomic_load(&state.ready))
    platform_sleep_ms(1);

  ui_errno_stacks_toggle();
  if (GET_OPTION(snapshot_mode)) {
    atomic_store(&state.stop, true);
    thread_join(&worker, NULL);
    ui_errno_stacks_hide();
    asciichat_shared_destroy();
    return 0;
  }
  for (;;) {
    platform_sleep_ms(1000);
  }
}
