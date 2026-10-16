#include <ascii-chat/platform/keyboard.h>
#include <string.h>
#include <ctype.h>

keyboard_line_edit_result_t keyboard_apply_key(keyboard_line_edit_opts_t *opts) {
  // Validate options
  if (!opts || !opts->buffer || !opts->len || !opts->cursor) {
    return LINE_EDIT_NO_INPUT;
  }

  // This function MUST receive a pre-read key. It never reads from stdin.
  // The keyboard thread is the sole reader of stdin.
  if (opts->key == KEY_NONE) {
    return LINE_EDIT_NO_INPUT;
  }

  int c = opts->key;
  size_t len = *opts->len;
  size_t cursor = *opts->cursor;
  char *buffer = opts->buffer;
  size_t max_len = opts->max_len;

  // Arrow keys (already resolved by keyboard thread)
  if (c == KEY_LEFT) {
    if (cursor > 0) {
      cursor--;
      *opts->cursor = cursor;
    }
    return LINE_EDIT_CONTINUE;
  }
  if (c == KEY_RIGHT) {
    if (cursor < len) {
      cursor++;
      *opts->cursor = cursor;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Delete key (forward delete - delete character at cursor)
  if (c == 260) { // KEY_DELETE
    if (cursor < len) {
      memmove(&buffer[cursor], &buffer[cursor + 1], len - cursor - 1);
      len--;
      buffer[len] = '\0';
      *opts->len = len;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Home key (move to beginning of line)
  if (c == 261) { // KEY_HOME
    cursor = 0;
    *opts->cursor = cursor;
    return LINE_EDIT_CONTINUE;
  }

  // End key (move to end of line)
  if (c == 262) { // KEY_END
    cursor = len;
    *opts->cursor = cursor;
    return LINE_EDIT_CONTINUE;
  }

  // Ctrl+Delete - delete word forward (opposite of Ctrl+W)
  if (c == 263) { // KEY_CTRL_DELETE
    if (cursor < len) {
      size_t word_end = cursor;

      // Skip non-whitespace (word characters)
      while (word_end < len && !isspace((unsigned char)buffer[word_end])) {
        word_end++;
      }

      // Skip whitespace
      while (word_end < len && isspace((unsigned char)buffer[word_end])) {
        word_end++;
      }

      // Delete from cursor to word_end
      if (word_end > cursor) {
        memmove(&buffer[cursor], &buffer[word_end], len - word_end);
        len -= (word_end - cursor);
        buffer[len] = '\0';
        *opts->len = len;
      }
    }
    return LINE_EDIT_CONTINUE;
  }

  // Enter - accept input
  if (c == '\n' || c == '\r') {
    return LINE_EDIT_ACCEPTED;
  }

  // Ctrl+C - cancel
  if (c == 3) {
    return LINE_EDIT_CANCELLED;
  }

  // Escape (already resolved by keyboard thread - standalone ESC)
  if (c == KEY_ESCAPE) {
    return LINE_EDIT_CANCELLED;
  }

  // Backspace (BS = 8 or DEL = 127)
  if (c == 8 || c == 127) {
    if (cursor > 0) {
      memmove(&buffer[cursor - 1], &buffer[cursor], len - cursor);
      cursor--;
      len--;
      buffer[len] = '\0';
      *opts->len = len;
      *opts->cursor = cursor;
    } else if (len == 0) {
      // Backspace with empty buffer - cancel (like vim)
      return LINE_EDIT_CANCELLED;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Ctrl+W - delete word (zsh/readline style)
  if (c == 23) {
    if (cursor > 0) {
      size_t word_start = cursor;

      // Skip whitespace backwards
      while (word_start > 0 && isspace((unsigned char)buffer[word_start - 1])) {
        word_start--;
      }

      // Skip word characters backwards
      while (word_start > 0 && !isspace((unsigned char)buffer[word_start - 1])) {
        word_start--;
      }

      // Delete from word_start to cursor
      memmove(&buffer[word_start], &buffer[cursor], len - cursor);
      len -= (cursor - word_start);
      cursor = word_start;
      buffer[len] = '\0';
      *opts->len = len;
      *opts->cursor = cursor;
    } else if (len == 0) {
      // Ctrl+W with empty buffer - cancel
      return LINE_EDIT_CANCELLED;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Ignore control characters (except tab)
  if (c < 32 && c != '\t') {
    return LINE_EDIT_CONTINUE;
  }

  // Ignore non-ASCII (keyboard thread doesn't handle multi-byte UTF-8 yet)
  if (c > 127) {
    return LINE_EDIT_CONTINUE;
  }

  // Insert printable ASCII character at cursor position
  if (len < max_len - 1) {
    memmove(&buffer[cursor + 1], &buffer[cursor], len - cursor);
    buffer[cursor] = (char)c;
    len++;
    cursor++;

    *opts->len = len;
    *opts->cursor = cursor;
    buffer[len] = '\0';
  }

  return LINE_EDIT_CONTINUE;
}
