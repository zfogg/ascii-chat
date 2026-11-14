#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/media/input.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/tui/input.h>
#include <stdlib.h>

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
static bool fake_input_available(void) {
  return keyboard_stream_ready(raw_byte);
}
static keyboard_key_t fake_keyboard(void) {
  return keyboard_stream_read(raw_byte);
}

#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/log/search.h>
#include "session/capture.h"
#include "session/keyboard_handler.h"
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/audio.h>
#include <signal.h>

static int test_screen = UI_SCREEN_MEDIA;
static bool search_entering, test_covered;
static unsigned search_keys, cancel_calls;
static char search_paste[256];
static ui_presentation_state_t fake_state(void) {
  return (ui_presentation_state_t){.screen = test_screen, .covered = test_covered};
}
static int fake_screen(void) {
  return test_screen;
}
static asciichat_error_t fake_present(const char *prompt, const char *text, size_t cursor, bool secret,
                                      ui_prompt_kind_t kind) {
  (void)prompt;
  (void)text;
  (void)cursor;
  (void)secret;
  (void)kind;
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
static asciichat_error_t fake_search_insert(const char *text) {
  ASCIICHAT_REQUIRE_LT(strlen(text), sizeof(search_paste));
  SAFE_STRNCPY(search_paste, text, sizeof(search_paste));
  return ASCIICHAT_OK;
}
static bool fake_cancel(session_capture_ctx_t *capture) {
  (void)capture;
  ++cancel_calls;
  return true;
}

/* Exercise production input and session routing with deterministic terminal state. */
#define keyboard_read_nonblocking fake_keyboard
#define keyboard_input_available fake_input_available
#define ui_controller_state fake_state
#define ui_controller_current_screen fake_screen
#define ui_prompt_present_kind fake_present
#define ui_prompt_remove fake_remove
#define log_search_is_entering fake_search_entering
#define log_search_insert_text fake_search_insert
#include "../../../lib/tui/input.c"
#define ui_session_logs_key fake_logs_key
#define session_capture_cancel_open fake_cancel
static double input_position, input_duration, input_seek_target;
static unsigned input_seek_calls, input_signals, input_volume_shown;
static asciichat_error_t input_seek_result;
static bool input_help;
static double input_get_position(media_source_t *source) {
  (void)source;
  return input_position;
}
static double input_get_duration(media_source_t *source) {
  (void)source;
  return input_duration;
}
static asciichat_error_t input_seek(media_source_t *source, double position) {
  (void)source;
  ++input_seek_calls;
  input_seek_target = position;
  return input_seek_result;
}
static int input_raise(int signal) {
  ASCIICHAT_REQUIRE_EQ(signal, SIGINT);
  ++input_signals;
  return 0;
}
static void input_help_toggle(session_display_ctx_t *display) {
  ASCIICHAT_REQUIRE_NOT_NULL(display);
  input_help = !input_help;
}
static bool input_help_active(session_display_ctx_t *display) {
  (void)display;
  return input_help;
}
static void input_logs_toggle(void) {
  test_screen = test_screen == UI_SCREEN_LOGS ? UI_SCREEN_MEDIA : UI_SCREEN_LOGS;
}
static void input_logs_hide(void) {
  test_screen = UI_SCREEN_MEDIA;
}
static void input_show_volume(void) {
  ++input_volume_shown;
}
#define media_source_get_position input_get_position
#define media_source_get_duration input_get_duration
#define media_source_seek input_seek
#define raise input_raise
#define keyboard_help_toggle input_help_toggle
#define keyboard_help_is_active input_help_active
#define ui_controller_toggle_logs input_logs_toggle
#define ui_controller_hide_logs input_logs_hide
#define ui_controller_show_volume input_show_volume
#include "../../../src/common/session/keyboard_handler.c"
#undef media_source_get_position
#undef media_source_get_duration
#undef media_source_seek
#undef raise
#undef keyboard_help_toggle
#undef keyboard_help_is_active
#undef ui_controller_toggle_logs
#undef ui_controller_hide_logs
#undef ui_controller_show_volume
#undef session_capture_cancel_open
#undef ui_session_logs_key
#undef log_search_insert_text
#undef log_search_is_entering
#undef ui_prompt_remove
#undef ui_prompt_present_kind
#undef ui_controller_current_screen
#undef ui_controller_state
#undef keyboard_read_nonblocking
#undef keyboard_input_available

static void normalize(const char *input, const char *expected) {
  char output[1024];
  ASCIICHAT_REQUIRE_EQ(media_input_normalize(input, output, sizeof(output)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(output, expected), 0);
}
static void invalid(const char *input) {
  char output[1024];
  ASCIICHAT_REQUIRE_NE(media_input_normalize(input, output, sizeof(output)), ASCIICHAT_OK);
  CLEAR_ERRNO();
}
TEST_CASE("media_input/normalizes_paths_and_rejects_invalid_sources", "[media_input]") {
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
  ASCIICHAT_REQUIRE_NE(media_input_normalize("movie.mp4", small, sizeof(small)), ASCIICHAT_OK);
  CLEAR_ERRNO();
}

TEST_CASE("media_input/keyboard_stream_and_media_editor", "[media_input]") {
  log_init(NULL, LOG_ERROR, false, false);
  keyboard_stream_reset();
  feed("ab\033[A");
  for (int i = 0; i < 100; ++i)
    ASCIICHAT_REQUIRE(fake_input_available());
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), 'a');
  ASCIICHAT_REQUIRE(fake_input_available());
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), 'b');
  ASCIICHAT_REQUIRE(fake_input_available());
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_UP);
  ASCIICHAT_REQUIRE(!fake_input_available());
  feed("\033");
  ASCIICHAT_REQUIRE(!fake_input_available());
  platform_sleep_ms(60);
  ASCIICHAT_REQUIRE(fake_input_available());
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_ESCAPE);
  ASCIICHAT_REQUIRE(!fake_input_available());

  keyboard_stream_reset();
  uint64_t previous_reply = keyboard_stream_terminal_reply_ns();
  feed("\033]11;rgb:ffff/");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(keyboard_stream_terminal_reply_ns(), previous_reply);
  feed("ffff/ffff\033\\o");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_NONE);
  ASCIICHAT_REQUIRE(keyboard_stream_terminal_reply_ns() > previous_reply);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), 'o');
  feed("\033]11;rgb:0000/0000/0000\a-");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), '-');
  feed("\033\033]11;rgb:1717/1717/1717\033\\o");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_ESCAPE);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), 'o');
  feed("\033]11;rgb:1717/");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_NONE);
  platform_sleep_ms(75);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_NONE);
  feed("1717/1717\a-");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), '-');
  feed("\033[A\033[3;5~");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_UP);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_CTRL_DELETE);
  feed("\033[20");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_NONE);
  feed("0~mrc?[] caf\xc3\xa9.mp4\033[201~");
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_PASTE_BEGIN);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), 'm');
  while (*bytes && strcmp((const char *)bytes, "\033[201~"))
    ASCIICHAT_REQUIRE_NE(fake_keyboard(), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(fake_keyboard(), KEY_PASTE_END);

  keyboard_stream_reset();
  feed("\033[200~https://example.com/mrc?f=x\033[201~");
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_PASTE);
  char pasted[8192];
  ASCIICHAT_REQUIRE_EQ(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "https://example.com/mrc?f=x"), 0);
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_NONE);

  feed("\"C:\\Videos\\caf\xc3\xa9 movie.mp4\"");
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_PASTE);
  ASCIICHAT_REQUIRE_EQ(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(strstr(pasted, "movie.mp4"), NULL);
  feed("https://example.com/mrc.mp4\r\n");
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_PASTE);
  ASCIICHAT_REQUIRE_EQ(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "https://example.com/mrc.mp4"), 0);
  feed("\033[200~first.mp4\033[201~\033[200~second.mp4\033[201~");
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_PASTE);
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "first.mp4"), 0);
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_PASTE);
  ASCIICHAT_REQUIRE_EQ(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "second.mp4"), 0);
  feed("mrc");
  ASCIICHAT_REQUIRE_EQ(input_read_event(), 'm');
  ASCIICHAT_REQUIRE_EQ(input_read_event(), 'r');
  ASCIICHAT_REQUIRE_EQ(input_read_event(), 'c');
  feed("x\033[200~movie.mp4\033[201~");
  ASCIICHAT_REQUIRE_EQ(input_read_event(), 'x');
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_PASTE);
  ASCIICHAT_REQUIRE_EQ(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "movie.mp4"), 0);
  feed("\033[200~one.mp4\ntwo.mp4\033[201~");
  ASCIICHAT_REQUIRE_EQ(input_read_event(), KEY_PASTE);
  ASCIICHAT_REQUIRE_EQ(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "one.mp4\ntwo.mp4"), 0);
  static char oversized[8300];
  memcpy(oversized, "\033[200~", 6);
  memset(oversized + 6, 'm', 8200);
  memcpy(oversized + 8206, "\033[201~", 7);
  feed(oversized);
  keyboard_key_t event;
  do {
    event = input_read_event();
  } while (event == KEY_NONE && *bytes);
  ASCIICHAT_REQUIRE_EQ(event, KEY_PASTE);
  ASCIICHAT_REQUIRE_NE(ui_input_take_paste(pasted, sizeof(pasted)), ASCIICHAT_OK);
  CLEAR_ERRNO();
  /* Beginning and polling an idle prompt must return immediately to the caller's session loop. */
  feed("");
  uint64_t before = time_get_ns();
  ASCIICHAT_REQUIRE_EQ(ui_input_media_begin("movie.mp4"), ASCIICHAT_OK);
  for (unsigned n = 0; n < 100; ++n)
    ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE(time_get_ns() - before < NS_PER_SEC_INT);
  test_covered = true;
  feed("discarded\r");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE(g_media_editor.active && !g_media_editor.submitted);
  test_covered = false;
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  feed("\033[D\177");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  feed("\r");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_MEDIA_SUBMIT);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_take(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "movie.m4"), 0);
  ASCIICHAT_REQUIRE_NE(ui_input_media_take(pasted, sizeof(pasted)), ASCIICHAT_OK);
  CLEAR_ERRNO();
  ASCIICHAT_REQUIRE_EQ(ui_input_media_begin("old.mp4"), ASCIICHAT_OK);
  feed("\033");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  platform_sleep_ns(60 * NS_PER_MS_INT);
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE(!g_media_editor.active && !g_media_editor.submitted);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_begin("old.mp4"), ASCIICHAT_OK);
  g_media_editor.deadline = 0;
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE(!g_media_editor.active);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_begin(""), ASCIICHAT_OK);
  g_media_editor.deadline = 0;
  feed("\033[200~late.mp4\033[201~");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE(!g_media_editor.active && !g_events.ready);
  feed("o");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), 'o');
  ASCIICHAT_REQUIRE_EQ(ui_input_media_begin(""), ASCIICHAT_OK);
  feed("\033[200~caf\xc3\xa9 video.mp4\033[201~");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  feed("\r");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_MEDIA_SUBMIT);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_take(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(strcmp(pasted, "\"caf\xc3\xa9 video.mp4\""), 0);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_begin(""), ASCIICHAT_OK);
  g_sync_keys[g_sync_key_head] = 'v';
  g_sync_key_count = 1;
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(strcmp(g_media_editor.buffer, "v"), 0);
  feed("\r");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_MEDIA_SUBMIT);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_take(pasted, sizeof(pasted)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_begin(""), ASCIICHAT_OK);
  feed("\"C:\\Videos\\first movie.mp4\" ");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(strcmp(g_media_editor.buffer, "\"C:\\Videos\\first movie.mp4\""), 0);
  feed(" "); // A terminal may deliver the drop separator in its next input record.
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE_EQ(g_media_editor.cursor, strlen(g_media_editor.buffer));
  ASCIICHAT_REQUIRE_EQ(g_media_editor.buffer[g_media_editor.cursor - 1], '"');
  feed("\"C:\\Videos\\second.mp4\" ");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE(strchr(g_media_editor.buffer, '\n'));
  feed("\r");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_MEDIA_SUBMIT);
  ASCIICHAT_REQUIRE_EQ(ui_input_media_take(pasted, sizeof(pasted)), ASCIICHAT_OK);
  media_playlist_t dropped = {0};
  ASCIICHAT_REQUIRE(media_playlist_parse(pasted, &dropped) == ASCIICHAT_OK && dropped.count == 2);
  /* Search input, including media shortcut letters and Escape, belongs to the logs screen. */
  search_entering = true;
  test_screen = UI_SCREEN_LOGS;
  const char *search = "audio VIDEO";
  for (const char *p = search; *p; ++p)
    session_handle_keyboard_input(NULL, NULL, (keyboard_key_t)*p);
  session_handle_keyboard_input(NULL, NULL, KEY_ESCAPE);
  ASCIICHAT_REQUIRE_EQ(search_keys, strlen(search) + 1);
  ASCIICHAT_REQUIRE_EQ(cancel_calls, 0);
  ASCIICHAT_REQUIRE_EQ(test_screen, UI_SCREEN_LOGS);
  feed("\033[200~video\033[201~");
  ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
  ASCIICHAT_REQUIRE_STR_EQ(search_paste, "video");
  ASCIICHAT_REQUIRE_EQ(search_keys, strlen(search) + 1);
  ASCIICHAT_REQUIRE(!g_events.ready);
  feed("");
  ASCIICHAT_REQUIRE(!input_ready());
#ifndef NDEBUG
  uint64_t idle_locks = g_input_mutex.lock_count;
#endif
  for (unsigned i = 0; i < 1000; ++i) {
    ASCIICHAT_REQUIRE_EQ(ui_input_read_key(UI_SCREEN_MEDIA), KEY_NONE);
    ui_input_poll_sync();
  }
#ifndef NDEBUG
  ASCIICHAT_REQUIRE_EQ(g_input_mutex.lock_count, idle_locks);
#endif
  ui_input_shutdown();
}

static session_capture_ctx_t *input_file_capture(char path[PLATFORM_MAX_PATH_LENGTH]) {
  int fd = -1;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(path, PLATFORM_MAX_PATH_LENGTH, "input-controls", &fd), 0);
  ASCIICHAT_REQUIRE_EQ(platform_close(fd), 0);
  FILE *file = platform_fopen("input-controls", path, "wb");
  ASCIICHAT_REQUIRE_NOT_NULL(file);
  for (unsigned i = 0; i < 20; ++i) {
    const char header[] = "P6\n2 2\n255\n";
    const uint8_t pixels[12] = {0};
    ASCIICHAT_REQUIRE_EQ(fwrite(header, 1, sizeof(header) - 1, file), sizeof(header) - 1);
    ASCIICHAT_REQUIRE_EQ(fwrite(pixels, 1, sizeof(pixels), file), sizeof(pixels));
  }
  ASCIICHAT_REQUIRE_EQ(fclose(file), 0);
  session_capture_config_t config = {.type = MEDIA_SOURCE_FILE, .path = path, .target_fps = 30};
  session_capture_ctx_t *capture = session_mirror_capture_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(capture);
  uint64_t deadline = time_get_ns() + NS_PER_SEC_INT;
  image_t *first = NULL;
  while (!first && time_get_ns() < deadline) {
    first = session_capture_read_frame(capture);
    if (!first)
      platform_sleep_ms(1);
  }
  ASCIICHAT_REQUIRE_NOT_NULL(first);
  return capture;
}
TEST_CASE("media_input/session_seek_pause_and_media_toggle_preserve_capture_and_flush_borrowed_audio", "[media_input]") {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  test_screen = UI_SCREEN_MEDIA;
  search_entering = false;
  char path[PLATFORM_MAX_PATH_LENGTH];
  session_capture_ctx_t *capture = input_file_capture(path);
  media_source_t *source = session_capture_get_media_source(capture);
  ASCIICHAT_REQUIRE_NOT_NULL(source);
  audio_context_t audio = {0};
  ASCIICHAT_REQUIRE_EQ(audio_init(&audio), ASCIICHAT_OK);
  session_capture_set_audio_context(capture, &audio);
  float pcm[64] = {0.25f};
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_write(audio.playback_buffer, pcm, 64), ASCIICHAT_OK);
  input_position = 10;
  input_duration = 45;
  handle_keyboard_input(capture, NULL, KEY_LEFT);
  ASCIICHAT_REQUIRE_EQ(input_seek_calls, 1);
  ASCIICHAT_REQUIRE_EQ(input_seek_target, 0);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(audio.playback_buffer), 0);
  input_position = 40;
  handle_keyboard_input(capture, NULL, KEY_LEFT);
  ASCIICHAT_REQUIRE_EQ(input_seek_target, 10);
  handle_keyboard_input(capture, NULL, KEY_RIGHT);
  ASCIICHAT_REQUIRE_EQ(input_seek_target, 45);
  input_duration = -1;
  handle_keyboard_input(capture, NULL, KEY_RIGHT);
  ASCIICHAT_REQUIRE_EQ(input_seek_target, 70);
  input_position = -1;
  unsigned before = input_seek_calls;
  handle_keyboard_input(capture, NULL, KEY_LEFT);
  handle_keyboard_input(capture, NULL, KEY_RIGHT);
  ASCIICHAT_REQUIRE_EQ(input_seek_calls, before);
  input_position = 10;
  input_seek_result = ERROR_MEDIA_INIT;
  handle_keyboard_input(capture, NULL, KEY_LEFT);
  ASCIICHAT_REQUIRE_EQ(input_seek_calls, before + 1);
  media_source_pause(source);
  handle_keyboard_input(capture, NULL, KEY_SPACE);
  ASCIICHAT_REQUIRE_NOT(media_source_is_paused(source));
  handle_keyboard_input(capture, NULL, KEY_SPACE);
  ASCIICHAT_REQUIRE(media_source_is_paused(source));
  handle_keyboard_input(capture, NULL, '[');
  ASCIICHAT_REQUIRE_NOT(GET_OPTION(media_input));
  handle_keyboard_input(capture, NULL, '[');
  ASCIICHAT_REQUIRE(GET_OPTION(media_input));
  ASCIICHAT_REQUIRE_EQ(session_capture_get_media_source(capture), source);
  session_capture_set_audio_context(capture, NULL);
  session_capture_destroy(capture);
  audio_destroy(&audio);
  platform_unlink(path);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("media_input/help_logs_volume_and_close_actions_route_to_the_owning_ui", "[media_input]") {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  test_screen = UI_SCREEN_MEDIA;
  search_entering = false;
  int opaque_display = 0;
  session_display_ctx_t *display = (session_display_ctx_t *)&opaque_display;
  handle_keyboard_input(NULL, display, KEY_QUESTION);
  ASCIICHAT_REQUIRE(input_help);
  handle_keyboard_input(NULL, display, KEY_ESCAPE);
  ASCIICHAT_REQUIRE_NOT(input_help);
  ASCIICHAT_REQUIRE_EQ(input_signals, 0);
  handle_keyboard_input(NULL, display, KEY_ESCAPE);
  ASCIICHAT_REQUIRE_EQ(input_signals, 1);
  handle_keyboard_input(NULL, NULL, KEY_QUESTION);
  ASCIICHAT_REQUIRE_NOT(input_help);
  handle_keyboard_input(NULL, NULL, ']');
  ASCIICHAT_REQUIRE_EQ(test_screen, UI_SCREEN_LOGS);
  handle_keyboard_input(NULL, NULL, KEY_ESCAPE);
  ASCIICHAT_REQUIRE_EQ(search_keys, 1);
  test_screen = UI_SCREEN_MEDIA;
  double volume = GET_OPTION(speakers_volume);
  handle_keyboard_input(NULL, NULL, KEY_DOWN);
  ASCIICHAT_REQUIRE_LE(GET_OPTION(speakers_volume), volume);
  ASCIICHAT_REQUIRE_EQ(input_volume_shown, 1);
  handle_keyboard_input(NULL, NULL, 'm');
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(speakers_volume), 0);
  ASCIICHAT_REQUIRE_EQ(input_volume_shown, 2);
  handle_keyboard_input(NULL, NULL, 'M');
  ASCIICHAT_REQUIRE_GT(GET_OPTION(speakers_volume), 0);
  ASCIICHAT_REQUIRE_EQ(input_volume_shown, 3);
  handle_keyboard_input(NULL, NULL, KEY_UP);
  ASCIICHAT_REQUIRE_EQ(input_volume_shown, 4);
  handle_keyboard_input(NULL, NULL, KEY_LEFT);
  handle_keyboard_input(NULL, NULL, KEY_RIGHT);
  handle_keyboard_input(NULL, NULL, KEY_SPACE);
  handle_keyboard_input(NULL, NULL, KEY_NONE);
  handle_keyboard_input(NULL, NULL, '~');
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
