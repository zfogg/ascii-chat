#include <ascii-chat/media/input.h>
#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/ui/input.h>
#include <stdlib.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)

static const unsigned char *bytes;
static size_t available;
static int raw_byte(void) {
  if (!available || !*bytes)
    return 0;
  --available;
  return *bytes++;
}
static void feed(const char *text) {
  bytes = (const unsigned char *)text;
  available = strlen(text);
}
static keyboard_key_t fake_keyboard(void) {
  return keyboard_stream_read(raw_byte);
}

#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/ui/session.h>
#include <ascii-chat/log/search.h>
#include "session/capture.h"
#include "session/keyboard_handler.h"

static int test_screen = UI_SCREEN_MEDIA;
static bool search_entering, test_covered;
static unsigned search_keys, cancel_calls;
static ui_presentation_state_t fake_state(void) {
  return (ui_presentation_state_t){.screen = test_screen, .covered = test_covered};
}
static int fake_screen(void) {
  return test_screen;
}
static asciichat_error_t fake_present(const char *prompt, const char *text, size_t cursor) {
  (void)prompt;
  (void)text;
  (void)cursor;
  test_screen = UI_SCREEN_PROMPT;
  return ASCIICHAT_OK;
}
static void fake_remove(void) {
  test_screen = UI_SCREEN_MEDIA;
}
static bool fake_search_entering(void) {
  return search_entering;
}
static bool fake_logs_key(keyboard_key_t key) {
  (void)key;
  ++search_keys;
  return true;
}
static bool fake_cancel(session_capture_ctx_t *capture) {
  (void)capture;
  ++cancel_calls;
  return true;
}

/* Exercise production input and session routing with deterministic terminal state. */
#define keyboard_read_nonblocking fake_keyboard
#define ui_controller_state fake_state
#define ui_controller_current_screen fake_screen
#define ui_prompt_present fake_present
#define ui_prompt_remove fake_remove
#define log_search_is_entering fake_search_entering
#include "../../../lib/ui/input.c"
#define ui_session_logs_key fake_logs_key
#define session_capture_cancel_open fake_cancel
#include "../../../src/common/session/keyboard_handler.c"
#undef session_capture_cancel_open
#undef ui_session_logs_key
#undef log_search_is_entering
#undef ui_prompt_remove
#undef ui_prompt_present
#undef ui_controller_current_screen
#undef ui_controller_state
#undef keyboard_read_nonblocking

static void normalize(const char *input, const char *expected) {
  char output[1024];
  CHECK(media_input_normalize(input, output, sizeof(output)) == ASCIICHAT_OK);
  CHECK(strcmp(output, expected) == 0);
}
static void invalid(const char *input) {
  char output[1024];
  CHECK(media_input_normalize(input, output, sizeof(output)) != ASCIICHAT_OK);
  CLEAR_ERRNO();
}
int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  normalize("  '/tmp/my video.mp4'  ", "/tmp/my video.mp4");
  normalize("/tmp/my\\ video.mp4", "/tmp/my video.mp4");
  normalize("\"C:\\Videos\\my video.mp4\"", "C:\\Videos\\my video.mp4");
  normalize("& 'C:\\Videos\\my video.mp4'", "C:\\Videos\\my video.mp4");
  normalize("https://example.com/watch?v=mrc&x=%20", "https://example.com/watch?v=mrc&x=%20");
  normalize("file:///tmp/caf%C3%A9%20movie.mp4", "/tmp/caf\xc3\xa9 movie.mp4");
  normalize("file://localhost/tmp/movie.mp4", "/tmp/movie.mp4");
#ifdef _WIN32
  normalize("file:///C:/Videos/my%20video.mp4", "C:/Videos/my video.mp4");
#endif
  invalid("bad\xff.mp4");
  invalid("");
  invalid("'first.mp4' 'second.mp4'");
  invalid("first.mp4\nsecond.mp4");
  invalid("file:///tmp/%00movie.mp4");
  invalid("file:///tmp/%XX");
  invalid("file://otherhost/tmp/movie.mp4");
  invalid("https://example.com/\033[31m");
  char small[4];
  CHECK(media_input_normalize("movie.mp4", small, sizeof(small)) != ASCIICHAT_OK);
  CLEAR_ERRNO();

  keyboard_stream_reset();
  uint64_t previous_reply = keyboard_stream_terminal_reply_ns();
  feed("\033]11;rgb:ffff/");
  CHECK(fake_keyboard() == KEY_NONE);
  CHECK(keyboard_stream_terminal_reply_ns() == previous_reply);
  feed("ffff/ffff\033\\o");
  CHECK(fake_keyboard() == KEY_NONE);
  CHECK(keyboard_stream_terminal_reply_ns() > previous_reply);
  CHECK(fake_keyboard() == 'o');
  feed("\033]11;rgb:0000/0000/0000\a-");
  CHECK(fake_keyboard() == KEY_NONE);
  CHECK(fake_keyboard() == '-');
  feed("\033\033]11;rgb:1717/1717/1717\033\\o");
  CHECK(fake_keyboard() == KEY_ESCAPE);
  CHECK(fake_keyboard() == 'o');
  feed("\033]11;rgb:1717/");
  CHECK(fake_keyboard() == KEY_NONE);
  platform_sleep_ms(75);
  CHECK(fake_keyboard() == KEY_NONE);
  feed("1717/1717\a-");
  CHECK(fake_keyboard() == KEY_NONE);
  CHECK(fake_keyboard() == '-');
  feed("\033[A\033[3;5~");
  CHECK(fake_keyboard() == KEY_UP);
  CHECK(fake_keyboard() == KEY_CTRL_DELETE);
  feed("\033[20");
  CHECK(fake_keyboard() == KEY_NONE);
  feed("0~mrc?[] caf\xc3\xa9.mp4\033[201~");
  CHECK(fake_keyboard() == KEY_PASTE_BEGIN);
  CHECK(fake_keyboard() == 'm');
  while (*bytes && strcmp((const char *)bytes, "\033[201~"))
    CHECK(fake_keyboard() != KEY_NONE);
  CHECK(fake_keyboard() == KEY_PASTE_END);

  keyboard_stream_reset();
  feed("\033[200~https://example.com/mrc?f=x\033[201~");
  CHECK(input_read_event() == KEY_PASTE);
  char pasted[8192];
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "https://example.com/mrc?f=x") == 0);
  CHECK(input_read_event() == KEY_NONE);

  feed("\"C:\\Videos\\caf\xc3\xa9 movie.mp4\"");
  CHECK(input_read_event() == KEY_PASTE);
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strstr(pasted, "movie.mp4") != NULL);
  feed("https://example.com/mrc.mp4\r\n");
  CHECK(input_read_event() == KEY_PASTE);
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "https://example.com/mrc.mp4") == 0);
  feed("\033[200~first.mp4\033[201~\033[200~second.mp4\033[201~");
  CHECK(input_read_event() == KEY_PASTE);
  CHECK(input_read_event() == KEY_NONE);
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "first.mp4") == 0);
  CHECK(input_read_event() == KEY_PASTE);
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "second.mp4") == 0);
  feed("mrc");
  CHECK(input_read_event() == 'm');
  CHECK(input_read_event() == 'r');
  CHECK(input_read_event() == 'c');
  feed("x\033[200~movie.mp4\033[201~");
  CHECK(input_read_event() == 'x');
  CHECK(input_read_event() == KEY_PASTE);
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "movie.mp4") == 0);
  feed("\033[200~one.mp4\ntwo.mp4\033[201~");
  CHECK(input_read_event() == KEY_PASTE);
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "one.mp4\ntwo.mp4") == 0);
  static char oversized[8300];
  memcpy(oversized, "\033[200~", 6);
  memset(oversized + 6, 'm', 8200);
  memcpy(oversized + 8206, "\033[201~", 7);
  feed(oversized);
  keyboard_key_t event;
  do {
    event = input_read_event();
  } while (event == KEY_NONE && *bytes);
  CHECK(event == KEY_PASTE);
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) != ASCIICHAT_OK);
  CLEAR_ERRNO();
  /* Beginning and polling an idle prompt must return immediately to the caller's session loop. */
  feed("");
  uint64_t before = time_get_ns();
  CHECK(ui_input_media_begin("movie.mp4") == ASCIICHAT_OK);
  for (unsigned n = 0; n < 100; ++n)
    CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(time_get_ns() - before < NS_PER_SEC_INT);
  test_covered = true;
  feed("discarded\r");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(g_media_editor.active && !g_media_editor.submitted);
  test_covered = false;
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  feed("\033[D\177");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  feed("\r");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_MEDIA_SUBMIT);
  CHECK(ui_input_media_take(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "movie.m4") == 0);
  CHECK(ui_input_media_take(pasted, sizeof(pasted)) != ASCIICHAT_OK);
  CLEAR_ERRNO();
  CHECK(ui_input_media_begin("old.mp4") == ASCIICHAT_OK);
  feed("\033");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  platform_sleep_ns(60 * NS_PER_MS_INT);
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(!g_media_editor.active && !g_media_editor.submitted);
  CHECK(ui_input_media_begin("old.mp4") == ASCIICHAT_OK);
  g_media_editor.deadline = 0;
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(!g_media_editor.active);
  CHECK(ui_input_media_begin("") == ASCIICHAT_OK);
  g_media_editor.deadline = 0;
  feed("\033[200~late.mp4\033[201~");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(!g_media_editor.active && !g_paste_ready);
  feed("o");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == 'o');
  CHECK(ui_input_media_begin("") == ASCIICHAT_OK);
  feed("\033[200~caf\xc3\xa9 video.mp4\033[201~");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  feed("\r");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_MEDIA_SUBMIT);
  CHECK(ui_input_media_take(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(strcmp(pasted, "\"caf\xc3\xa9 video.mp4\"") == 0);
  CHECK(ui_input_media_begin("") == ASCIICHAT_OK);
  g_sync_keys[g_sync_key_head] = 'v';
  g_sync_key_count = 1;
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(strcmp(g_media_editor.buffer, "v") == 0);
  feed("\r");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_MEDIA_SUBMIT);
  CHECK(ui_input_media_take(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  CHECK(ui_input_media_begin("") == ASCIICHAT_OK);
  feed("\"C:\\Videos\\first movie.mp4\" ");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(strcmp(g_media_editor.buffer, "\"C:\\Videos\\first movie.mp4\"") == 0);
  feed(" "); // A terminal may deliver the drop separator in its next input record.
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(g_media_editor.cursor == strlen(g_media_editor.buffer));
  CHECK(g_media_editor.buffer[g_media_editor.cursor - 1] == '"');
  feed("\"C:\\Videos\\second.mp4\" ");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_NONE);
  CHECK(strchr(g_media_editor.buffer, '\n'));
  feed("\r");
  CHECK(ui_input_read_key(UI_SCREEN_MEDIA) == KEY_MEDIA_SUBMIT);
  CHECK(ui_input_media_take(pasted, sizeof(pasted)) == ASCIICHAT_OK);
  media_playlist_t dropped = {0};
  CHECK(media_playlist_parse(pasted, &dropped) == ASCIICHAT_OK && dropped.count == 2);
  /* Search input, including media shortcut letters and Escape, belongs to the logs screen. */
  search_entering = true;
  test_screen = UI_SCREEN_LOGS;
  const char *search = "audio VIDEO";
  for (const char *p = search; *p; ++p)
    session_handle_keyboard_input(NULL, NULL, (keyboard_key_t)*p);
  session_handle_keyboard_input(NULL, NULL, KEY_ESCAPE);
  CHECK(search_keys == strlen(search) + 1);
  CHECK(cancel_calls == 0);
  CHECK(test_screen == UI_SCREEN_LOGS);
  feed("\033[200~video\033[201~");
  CHECK(input_read_event() == KEY_PASTE);
  session_handle_keyboard_input(NULL, NULL, KEY_PASTE);
  CHECK(search_keys == strlen(search) + 6);
  CHECK(!g_paste_ready);
  ui_input_shutdown();
  return 0;
}
