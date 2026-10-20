/** Exercise native clipboard formats without changing the user's clipboard. */
#include <ascii-chat/platform/clipboard.h>
#include <ascii-chat/common.h>
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)
static bool opened, files;
static UINT file_count;
static const wchar_t *text;
static BOOL fake_open(HWND owner) {
  (void)owner;
  opened = true;
  return TRUE;
}
static BOOL fake_close(void) {
  CHECK(opened);
  opened = false;
  return TRUE;
}
static BOOL fake_has(UINT format) {
  return format == CF_HDROP && files;
}
static HANDLE fake_data(UINT format) {
  CHECK(opened);
  return format == CF_HDROP || text ? (HANDLE)1 : NULL;
}
static LPVOID fake_lock(HGLOBAL data) {
  (void)data;
  return (LPVOID)text;
}
static BOOL fake_unlock(HGLOBAL data) {
  (void)data;
  return TRUE;
}
static UINT fake_query(HDROP drop, UINT index, LPWSTR output, UINT capacity) {
  (void)drop;
  if (index == 0xffffffff)
    return file_count;
  UINT length = (UINT)wcslen(text);
  if (output && capacity > length)
    memcpy(output, text, ((size_t)length + 1) * sizeof(wchar_t));
  return length;
}
#define OpenClipboard fake_open
#define CloseClipboard fake_close
#define IsClipboardFormatAvailable fake_has
#define GetClipboardData fake_data
#define GlobalLock fake_lock
#define GlobalUnlock fake_unlock
#define DragQueryFileW fake_query
#include "../../../lib/platform/windows/clipboard.c"

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  char output[1024];
  text = L"https://example.com/video.mp4";
  CHECK(platform_clipboard_read(output, sizeof(output)) == ASCIICHAT_OK);
  CHECK(strcmp(output, "https://example.com/video.mp4") == 0 && !opened);
  text = L"C:\\Videos\\caf\u00e9 movie.mp4";
  files = true;
  file_count = 1;
  CHECK(platform_clipboard_read(output, sizeof(output)) == ASCIICHAT_OK);
  CHECK(strcmp(output, "\"C:\\Videos\\caf\xc3\xa9 movie.mp4\"") == 0 && !opened);
  file_count = 2;
  CHECK(platform_clipboard_read(output, sizeof(output)) == ASCIICHAT_OK && !opened);
  CHECK(strchr(output, '\n'));
  files = false;
  CHECK(platform_clipboard_read(output, 4) != ASCIICHAT_OK && !opened);
  CLEAR_ERRNO();
  text = NULL;
  CHECK(platform_clipboard_read(output, sizeof(output)) != ASCIICHAT_OK && !opened);
  CLEAR_ERRNO();
  return 0;
}
