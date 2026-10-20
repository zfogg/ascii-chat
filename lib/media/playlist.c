#include <ascii-chat/media/playlist.h>
#include <ascii-chat/media/input.h>
#include <ascii-chat/common.h>
#include <ctype.h>
#include <string.h>

asciichat_error_t media_playlist_append(media_playlist_t *list, const char *path, bool url) {
  if (!list || !path || list->count >= MEDIA_PLAYLIST_MAX_ITEMS)
    return SET_ERRNO(ERROR_INVALID_PARAM, "A playlist supports at most %d artifacts", MEDIA_PLAYLIST_MAX_ITEMS);
  char normalized[MEDIA_PLAYLIST_PATH_MAX];
  asciichat_error_t result = media_input_normalize(path, normalized, sizeof(normalized));
  if (result != ASCIICHAT_OK)
    return result;
  if (url && !strstr(normalized, "://")) {
    char absolute[MEDIA_PLAYLIST_PATH_MAX];
    if (strlen(normalized) + 7 >= sizeof(absolute))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Media URL is too long");
    snprintf(absolute, sizeof(absolute), "http://%s", normalized);
    memcpy(normalized, absolute, strlen(absolute) + 1);
  }
  if (strcmp(normalized, "-") == 0 && list->count)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Stdin must be the only playlist artifact");
  if (list->count && strcmp(list->paths[0], "-") == 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Stdin must be the only playlist artifact");
  memcpy(list->paths[list->count], normalized, strlen(normalized) + 1);
  list->urls[list->count++] = url;
  return ASCIICHAT_OK;
}

asciichat_error_t media_playlist_parse(const char *text, media_playlist_t *list) {
  if (!text || !list)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid playlist input");
  media_playlist_t parsed = {0};
  while (isspace((unsigned char)*text))
    ++text;
  if (text[0] == '&' && text[1] == ' ')
    text += 2;
  while (*text) {
    while (isspace((unsigned char)*text))
      ++text;
    if (!*text)
      break;
    const char *start = text;
    if (*text == '\'' || *text == '"') {
      char quote = *text++;
      while (*text && *text != quote)
        ++text;
      if (!*text || (text[1] && !isspace((unsigned char)text[1])))
        return SET_ERRNO(ERROR_INVALID_PARAM, "Separate media artifacts with whitespace and match their quotes");
      ++text;
    } else {
      // Preserve a manually typed path containing spaces. Newline-delimited
      // clipboard URI lists remain separate artifacts.
      while (*text && *text != '\n' && *text != '\r') {
        if (isspace((unsigned char)*text)) {
          const char *next = text;
          while (*next == ' ' || *next == '\t')
            ++next;
          bool path_start = *next == '\'' || *next == '"' || *next == '/' || *next == '\\' ||
                            (isalpha((unsigned char)next[0]) && next[1] == ':');
          const char *scheme = strstr(next, "://");
          const char *space = strpbrk(next, " \t\r\n");
          if (path_start || (scheme && (!space || scheme < space)))
            break;
        }
        ++text;
      }
    }
    size_t bytes = (size_t)(text - start);
    char argument[MEDIA_PLAYLIST_PATH_MAX + 2];
    if (bytes >= sizeof(argument))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Media artifact is too long");
    memcpy(argument, start, bytes);
    argument[bytes] = '\0';
    bool url =
        strstr(argument, "://") && strncmp(argument + (argument[0] == '\'' || argument[0] == '"'), "file://", 7) != 0;
    asciichat_error_t result = media_playlist_append(&parsed, argument, url);
    if (result != ASCIICHAT_OK)
      return result;
  }
  if (!parsed.count)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Enter at least one media path or URL");
  *list = parsed;
  return ASCIICHAT_OK;
}

asciichat_error_t media_playlist_format(const media_playlist_t *list, char *text, size_t capacity) {
  if (!list || !text || !capacity)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid playlist display buffer");
  size_t used = 0;
  text[0] = '\0';
  for (size_t i = 0; i < list->count; ++i) {
    const char *path = list->paths[i];
    char quote = strchr(path, '"') ? '\'' : '"';
    if (strchr(path, quote))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot quote media artifact");
    size_t bytes = strlen(path);
    if (used + bytes + 3 + (i != 0) > capacity)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Playlist display is too long");
    if (i)
      text[used++] = '\n';
    text[used++] = quote;
    memcpy(text + used, path, bytes);
    used += bytes;
    text[used++] = quote;
    text[used] = '\0';
  }
  return ASCIICHAT_OK;
}

bool media_playlist_adjacent(const media_playlist_t *list, int direction, size_t *index) {
  if (!list || !index || !list->count || list->index >= list->count || (direction != -1 && direction != 1))
    return false;
  if (direction < 0 && list->index) {
    *index = list->index - 1;
    return true;
  }
  if (direction > 0 && list->index + 1 < list->count) {
    *index = list->index + 1;
    return true;
  }
  return false;
}
