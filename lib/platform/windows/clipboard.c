#include <ascii-chat/platform/clipboard.h>
#include <ascii-chat/common.h>
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
    if (!files || DragQueryFileW(files, 0xffffffff, NULL, 0) != 1) {
      result = SET_ERRNO(ERROR_INVALID_PARAM, "Copy one media file at a time");
    } else {
      UINT length = DragQueryFileW(files, 0, NULL, 0);
      wchar_t *path = SAFE_CALLOC((size_t)length + 1, sizeof(wchar_t), wchar_t *);
      if (!path)
        result = SET_ERRNO(ERROR_MEMORY, "Cannot read copied file");
      else {
        if (!DragQueryFileW(files, 0, path, length + 1) ||
            !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, output, (int)capacity, NULL, NULL))
          result = SET_ERRNO(ERROR_INVALID_PARAM, "Copied path is invalid or too long");
        SAFE_FREE(path);
      }
    }
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
