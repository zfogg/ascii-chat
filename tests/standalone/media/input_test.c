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

/* Exercise the production UI event collector without a terminal or shortcut side effects. */
#define keyboard_read_nonblocking fake_keyboard
#include "../../../lib/ui/input.c"
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
  CHECK(ui_input_take_paste(pasted, sizeof(pasted)) != ASCIICHAT_OK);
  CLEAR_ERRNO();
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
  ui_input_shutdown();
  return 0;
}
