#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>

#define MEDIA_PLAYLIST_MAX_ITEMS 64
#define MEDIA_PLAYLIST_PATH_MAX 256

typedef struct {
  char paths[MEDIA_PLAYLIST_MAX_ITEMS][MEDIA_PLAYLIST_PATH_MAX];
  bool urls[MEDIA_PLAYLIST_MAX_ITEMS];
  size_t count, index;
} media_playlist_t;

/** Append one already separated argument; preserves insertion order. */
asciichat_error_t media_playlist_append(media_playlist_t *list, const char *path, bool url);
/** Parse shell-quoted paths, file URIs or URLs. A lone unquoted path may contain spaces. */
asciichat_error_t media_playlist_parse(const char *text, media_playlist_t *list);
/** Render one quoted artifact per line, with no trailing whitespace. */
asciichat_error_t media_playlist_format(const media_playlist_t *list, char *text, size_t capacity);
/** Find an adjacent item without wrapping at playlist boundaries. */
bool media_playlist_adjacent(const media_playlist_t *list, int direction, size_t *index);
