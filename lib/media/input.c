#include <ascii-chat/media/input.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>
#include <ctype.h>
#include <string.h>

static int hex_digit(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  c = (unsigned char)tolower(c);
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

asciichat_error_t media_input_normalize(const char *text, char *output, size_t capacity) {
  if (!text || !output || capacity < 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid media input buffer");
  output[0] = '\0';
  while (isspace((unsigned char)*text))
    ++text;
  size_t length = strlen(text);
  while (length && isspace((unsigned char)text[length - 1]))
    --length;
  if (!length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Enter one media path or URL");
  if (length > 2 && text[0] == '&' && text[1] == ' ') {
    text += 2;
    length -= 2;
  }
  char quote = (*text == '\'' || *text == '"') ? *text : 0;
  if (quote) {
    if (length < 2 || text[length - 1] != quote)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Choose one file with matching quotes");
    ++text;
    length -= 2;
  }
  bool uri = length >= 7 && strncmp(text, "file://", sizeof("file://") - 1) == 0;
  bool url = !uri && (strstr(text, "://") != NULL);
  if (uri) {
    text += 7;
    length -= 7;
    if (length >= 10 && strncmp(text, "localhost/", sizeof("localhost/") - 1) == 0) {
      text += 9;
      length -= 9;
    } else if (*text != '/') {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Only local file URIs are supported");
    }
#ifdef _WIN32
    if (length >= 3 && text[0] == '/' && isalpha((unsigned char)text[1]) && text[2] == ':') {
      ++text;
      --length;
    }
#endif
  }
  bool windows_path = length >= 2 && (text[1] == ':' || (text[0] == '\\' && text[1] == '\\'));
  size_t used = 0;
  size_t i = 0;
  while (i < length) {
    unsigned char c = (unsigned char)text[i];
    if (uri && c == '%') {
      if (i + 2 >= length || hex_digit((unsigned char)text[i + 1]) < 0 || hex_digit((unsigned char)text[i + 2]) < 0)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid escape in file URI");
      c = (unsigned char)(hex_digit((unsigned char)text[i + 1]) * 16 + hex_digit((unsigned char)text[i + 2]));
      i += 2;
    } else if (!url && !uri && !quote && !windows_path && c == '\\' && i + 1 < length &&
               (text[i + 1] == ' ' || text[i + 1] == '\'' || text[i + 1] == '"' || text[i + 1] == '\\')) {
      c = (unsigned char)text[++i];
    } else if (!url && !uri && (c == '\'' || c == '"')) {
      if (quote && c == (unsigned char)quote)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Choose one media file at a time");
    }
    if (c < 32 || c == 127)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Choose one media path or URL without control characters");
    if (used + 1 >= capacity)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Media path or URL is too long");
    output[used++] = (char)c;
    ++i;
  }
  output[used] = '\0';
  if (!utf8_is_valid(output))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Media path or URL is not valid UTF-8");
  return used ? ASCIICHAT_OK : SET_ERRNO(ERROR_INVALID_PARAM, "Empty media path");
}
