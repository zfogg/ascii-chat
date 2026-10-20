#include <ascii-chat/platform/clipboard.h>
#include <ascii-chat/common.h>
#include <ascii-chat/media/playlist.h>
#include <windows.h>
#include <shellapi.h>

asciichat_error_t platform_clipboard_read(char *output, size_t capacity) {
  if (!output || capacity < 2 || capacity > INT_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid clipboard buffer");
  output[0] = '\0';
  if (!OpenClipboard(NULL))
    return SET_ERRNO_SYS(ERROR_GENERAL, "Cannot open clipboard");
  asciichat_error_t result = ASCIICHAT_OK;
  if (IsClipboardFormatAvailable(CF_HDROP)) {
    HDROP files = (HDROP)GetClipboardData(CF_HDROP);
    UINT count = files ? DragQueryFileW(files, 0xffffffff, NULL, 0) : 0;
    media_playlist_t list = {0};
    if (!count || count > MEDIA_PLAYLIST_MAX_ITEMS)
      result = SET_ERRNO(ERROR_INVALID_PARAM, "Clipboard has no files or too many media artifacts");
    for (UINT i = 0; i < count && result == ASCIICHAT_OK; ++i) {
      UINT length = DragQueryFileW(files, i, NULL, 0);
      wchar_t *path = SAFE_CALLOC((size_t)length + 1, sizeof(wchar_t), wchar_t *);
      char utf8[MEDIA_PLAYLIST_PATH_MAX];
      if (!path)
        result = SET_ERRNO(ERROR_MEMORY, "Cannot read copied file");
      else {
        if (!DragQueryFileW(files, i, path, length + 1) ||
            !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8, sizeof(utf8), NULL, NULL))
          result = SET_ERRNO(ERROR_INVALID_PARAM, "Copied path is invalid or too long");
        else
          result = media_playlist_append(&list, utf8, false);
        SAFE_FREE(path);
      }
    }
    if (result == ASCIICHAT_OK)
      result = media_playlist_format(&list, output, capacity);
  } else {
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *text = data ? GlobalLock(data) : NULL;
    if (!text)
      result = SET_ERRNO(ERROR_INVALID_PARAM, "Clipboard contains no text or copied file");
    else {
      if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, output, (int)capacity, NULL, NULL))
        result = SET_ERRNO(ERROR_INVALID_PARAM, "Clipboard text is invalid or too long");
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return result;
}
