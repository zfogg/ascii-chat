#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/ui/sync.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/media/playlist.h>
#include <ascii-chat/ui/keyboard_help.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>
#include <string.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/atomic.h>

uint64_t ui_input_deadline(unsigned seconds) {
  return time_get_ns() + (uint64_t)seconds * NS_PER_SEC_INT;
}

bool ui_input_expired(uint64_t deadline) {
  return time_get_ns() >= deadline;
}

void ui_input_timeout_report(unsigned seconds, const char *consequence) {
  char message[512];
  int length = snprintf(message, sizeof(message), "\nPrompt timed out after %u seconds; %s.\n", seconds, consequence);
  // This diagnostic must remain visible even while a background screen owns the terminal.
  if (length > 0)
    platform_write_all(STDERR_FILENO, message, (size_t)length < sizeof(message) ? (size_t)length : sizeof(message) - 1);
  SET_ERRNO(ERROR_PROMPT_TIMEOUT, "Prompt timed out after %u seconds: %s", seconds, consequence);
}

static lifecycle_t g_input_lifecycle = LIFECYCLE_INIT;
static mutex_t g_input_mutex;
static mutex_t g_input_prompt_mutex;
static bool g_input_was_covered;
static keyboard_key_t g_sync_keys[256];
static unsigned g_sync_key_head, g_sync_key_count;

static bool g_pasting, g_paste_invalid, g_paste_ready;
static char g_paste[8192];
static size_t g_paste_length;
static keyboard_key_t g_text_keys[8192];
static size_t g_text_head, g_text_count;

typedef struct {
  char buffer[8192];
  size_t length, cursor;
  uint64_t deadline;
  bool active, submitted, pasted;
} input_editor_t;
static input_editor_t g_media_editor;

static void input_editor_key(char *buffer, size_t max_len, size_t *length, size_t *position, keyboard_key_t key) {
  size_t len = *length, cursor = *position;
  if (key == KEY_HOME)
    cursor = 0;
  else if (key == KEY_END)
    cursor = len;
  else if (key == KEY_LEFT && cursor) {
    do {
      --cursor;
    } while (cursor && ((unsigned char)buffer[cursor] & 0xc0) == 0x80);
  } else if (key == KEY_RIGHT && cursor < len) {
    do {
      ++cursor;
    } while (cursor < len && ((unsigned char)buffer[cursor] & 0xc0) == 0x80);
  } else if ((key == 8 || key == 127) && cursor) {
    size_t start = cursor;
    do {
      --start;
    } while (start && ((unsigned char)buffer[start] & 0xc0) == 0x80);
    memmove(buffer + start, buffer + cursor, len - cursor + 1);
    len -= cursor - start;
    cursor = start;
  } else if (key == KEY_DELETE && cursor < len) {
    size_t end = cursor + 1;
    while (end < len && ((unsigned char)buffer[end] & 0xc0) == 0x80)
      ++end;
    memmove(buffer + cursor, buffer + end, len - end + 1);
    len -= end - cursor;
  } else if (key >= 32 && key < 256 && len < max_len - 1) {
    memmove(buffer + cursor + 1, buffer + cursor, len - cursor + 1);
    buffer[cursor++] = (char)key;
    ++len;
  }
  *length = len;
  *position = cursor;
}

/* Called only with g_input_mutex held. Payload never reaches shortcut routing. */
static keyboard_key_t input_read_event(void) {
  if (g_paste_ready)
    return KEY_NONE;
  for (unsigned i = 0; i < 1024; ++i) {
    bool buffered = g_text_count != 0;
    keyboard_key_t key = buffered ? g_text_keys[g_text_head++] : keyboard_read_nonblocking();
    if (buffered)
      --g_text_count;
    if (key == KEY_NONE)
      return KEY_NONE;
    if (key == KEY_PASTE_BEGIN) {
      g_pasting = true;
      g_paste_invalid = false;
      g_paste_length = 0;
      continue;
    }
    if (key == KEY_PASTE_END) {
      if (!g_pasting)
        continue;
      g_pasting = false;
      g_paste[g_paste_length] = '\0';
      g_paste_ready = true;
      return KEY_PASTE;
    }
    if (!g_pasting) {
      if (buffered)
        return key;
      if (key < 32 || key > 255)
        return key;
      g_text_head = 0;
      g_text_keys[0] = key;
      size_t count = 1;
      keyboard_key_t next;
      while (count < 8192 && (next = keyboard_read_nonblocking()) != KEY_NONE) {
        g_text_keys[count++] = next;
        if (next == KEY_PASTE_BEGIN || next == KEY_PASTE_END)
          break;
      }
      size_t text_count = count;
      while (text_count && (g_text_keys[text_count - 1] == '\r' || g_text_keys[text_count - 1] == '\n'))
        --text_count;
      char text[8193];
      bool printable = true;
      for (size_t n = 0; n < count; ++n) {
        if (n < text_count && (g_text_keys[n] < 32 || g_text_keys[n] > 255 || g_text_keys[n] == 127))
          printable = false;
        text[n] = (char)g_text_keys[n];
      }
      text[text_count] = '\0';
      bool path = text[0] == '.' || text[0] == '/' || text[0] == '\\' || text[0] == '~' || text[0] == '\'' ||
                  text[0] == '"' || (count > 2 && text[1] == ':') || strstr(text, "://") != NULL;
      if (text_count > 1 && path) {
        g_paste_invalid = !printable || text_count >= sizeof(g_paste);
        g_paste_length = g_paste_invalid ? 0 : text_count;
        memcpy(g_paste, text, g_paste_length);
        g_paste[g_paste_length] = '\0';
        g_paste_ready = true;
        return KEY_PASTE;
      }
      g_text_head = 1;
      g_text_count = count - 1;
      return key;
    }
    if ((key < 32 && key != '\n' && key != '\r' && key != '\t') || key > 255 || key == 127 ||
        g_paste_length + 1 >= sizeof(g_paste))
      g_paste_invalid = true;
    else
      g_paste[g_paste_length++] = (char)key;
  }
  return KEY_NONE;
}

/* Background debug polling can retain an event before a modal screen appears. */
static keyboard_key_t input_next_key(void) {
  if (g_sync_key_count) {
    keyboard_key_t key = g_sync_keys[g_sync_key_head];
    g_sync_key_head = (g_sync_key_head + 1) % 256;
    --g_sync_key_count;
    return key;
  }
  return input_read_event();
}

static bool input_initialize(void) {
  if (lifecycle_init_once(&g_input_lifecycle)) {
    if (mutex_init(&g_input_mutex, "ui_input") != 0) {
      lifecycle_init_abort(&g_input_lifecycle);
      return false;
    }
    if (mutex_init(&g_input_prompt_mutex, "ui_prompt_input") != 0) {
      mutex_destroy(&g_input_mutex);
      lifecycle_init_abort(&g_input_lifecycle);
      return false;
    }
    NAMED_REGISTER_ATOMIC(&g_input_lifecycle.state, "ui_input_lifecycle", NULL);
    g_input_was_covered = false;
    lifecycle_init_commit(&g_input_lifecycle);
  }
  return lifecycle_is_initialized(&g_input_lifecycle);
}

asciichat_error_t ui_input_take_paste(char *buffer, size_t capacity) {
  if (!buffer || !capacity || !input_initialize())
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid paste buffer");
  mutex_lock(&g_input_mutex);
  asciichat_error_t result = ASCIICHAT_OK;
  if (g_paste_invalid || g_paste_length >= capacity || !utf8_is_valid(g_paste))
    result = SET_ERRNO(ERROR_INVALID_PARAM, "Paste media paths or URLs without terminal controls (maximum 8191 bytes)");
  else
    memcpy(buffer, g_paste, g_paste_length + 1);
  g_paste_length = 0;
  g_paste[0] = '\0';
  g_paste_ready = false;
  mutex_unlock(&g_input_mutex);
  return result;
}

void ui_input_poll_sync(void) {
#ifndef NDEBUG
  if (!input_initialize() || mutex_trylock(&g_input_mutex) != 0)
    return;
  int screen = ui_controller_current_screen();
  if (screen >= 0 && screen <= UI_SCREEN_SYNC && !ui_controller_is_blocked()) {
    keyboard_key_t key = input_read_event();
    if (key != KEY_PASTE && !log_search_is_entering())
      key = ui_sync_handle_key(key);
    if (key && g_sync_key_count < 256)
      g_sync_keys[(g_sync_key_head + g_sync_key_count++) % 256] = key;
  }
  mutex_unlock(&g_input_mutex);
#endif
}

keyboard_key_t ui_input_read_key(ui_screen_t screen) {
  if (!input_initialize())
    return KEY_NONE;
  ui_presentation_state_t state = ui_controller_state();
  mutex_lock(&g_input_mutex);
  if (state.screen == UI_SCREEN_NOTICE) {
    g_paste_ready = false;
    g_sync_key_count = 0;
    keyboard_key_t notice_key = input_read_event();
    if (notice_key == KEY_PASTE)
      g_paste_ready = false;
    if (notice_key == KEY_ESCAPE || notice_key == '\r' || notice_key == '\n' || notice_key == 'q' || notice_key == 3)
      ui_controller_remove(UI_SCREEN_NOTICE);
    mutex_unlock(&g_input_mutex);
    return notice_key == 'q' || notice_key == 3 ? notice_key : KEY_NONE;
  }
  if (screen == UI_SCREEN_MEDIA && g_media_editor.active && state.screen == UI_SCREEN_PROMPT) {
    if (state.covered || g_input_was_covered) {
      g_paste_ready = false;
      g_sync_key_count = 0;
      for (unsigned n = 0; n < 256 && input_read_event() != KEY_NONE; ++n) {
        g_paste_ready = false;
      }
      g_input_was_covered = state.covered;
      mutex_unlock(&g_input_mutex);
      return KEY_NONE;
    }
    keyboard_key_t key = input_next_key();
    if (key == KEY_ESCAPE || key == 3 || ui_input_expired(g_media_editor.deadline)) {
      if (key == KEY_PASTE)
        g_paste_ready = false;
      g_media_editor.active = false;
      ui_prompt_remove();
    } else if (key == '\r' || key == '\n') {
      g_media_editor.active = false;
      g_media_editor.submitted = true;
      mutex_unlock(&g_input_mutex);
      return KEY_MEDIA_SUBMIT;
    } else if (key != KEY_NONE) {
      if (key == KEY_PASTE) {
        if (!g_paste_invalid && utf8_is_valid(g_paste)) {
          media_playlist_t items = {0}, added = {0};
          bool valid = !g_media_editor.length || media_playlist_parse(g_media_editor.buffer, &items) == ASCIICHAT_OK;
          if (valid && media_playlist_parse(g_paste, &added) == ASCIICHAT_OK) {
            for (size_t i = 0; i < added.count && valid; ++i)
              valid = media_playlist_append(&items, added.paths[i], added.urls[i]) == ASCIICHAT_OK;
            char formatted[sizeof(g_media_editor.buffer)];
            if (valid && media_playlist_format(&items, formatted, sizeof(formatted)) == ASCIICHAT_OK) {
              memcpy(g_media_editor.buffer, formatted, strlen(formatted) + 1);
              g_media_editor.length = g_media_editor.cursor = strlen(formatted);
            }
          }
          CLEAR_ERRNO();
        }
        g_paste_ready = false;
        g_media_editor.pasted = true;
      } else if (!(key == ' ' && g_media_editor.pasted && g_media_editor.cursor == g_media_editor.length)) {
        g_media_editor.pasted = false;
        input_editor_key(g_media_editor.buffer, sizeof(g_media_editor.buffer), &g_media_editor.length,
                         &g_media_editor.cursor, key);
      }
      if (ui_prompt_present("Open media: files or URLs\nDrop more files to queue; Enter plays; Esc cancels",
                            g_media_editor.buffer, g_media_editor.cursor) != ASCIICHAT_OK) {
        g_media_editor.active = false;
        ui_prompt_remove();
        CLEAR_ERRNO();
      }
    }
    mutex_unlock(&g_input_mutex);
    return KEY_NONE;
  }
  bool active = state.screen < 0 || state.screen == (int)screen ||
                (screen == UI_SCREEN_MEDIA && state.screen == UI_SCREEN_RENDER_PROGRESS) ||
                (screen == UI_SCREEN_MEDIA && state.screen == UI_SCREEN_HELP) ||
                ((screen == UI_SCREEN_MEDIA || screen == UI_SCREEN_SPLASH) && state.screen == UI_SCREEN_LOGS) ||
                (screen == UI_SCREEN_SPLASH && state.screen == UI_SCREEN_HELP);
  keyboard_key_t key = KEY_NONE;
  if (active) {
    if (state.covered || g_input_was_covered) {
      // Drain on recovery too, including keys buffered since the last poll.
      g_sync_key_count = 0;
      g_paste_ready = false;
      for (int i = 0; i < 256 && input_read_event() != KEY_NONE; ++i) {
      }
    } else {
      if (g_sync_key_count) {
        key = g_sync_keys[g_sync_key_head];
        g_sync_key_head = (g_sync_key_head + 1) % 256;
        --g_sync_key_count;
      }
      if (key == KEY_NONE)
        key = state.screen == UI_SCREEN_HELP && keyboard_help_check_signal_cancel() ? KEY_QUESTION : input_read_event();
      if (screen != UI_SCREEN_PROMPT && key != KEY_PASTE && !log_search_is_entering())
        key = ui_sync_handle_key(key);
    }
    g_input_was_covered = state.covered;
  }
  if (key == KEY_PASTE && screen != UI_SCREEN_MEDIA && screen != UI_SCREEN_PROMPT) {
    g_paste_ready = false;
    key = KEY_NONE;
  }
  if ((screen == UI_SCREEN_MEDIA || screen == UI_SCREEN_STATUS || screen == UI_SCREEN_SPLASH) &&
      state.screen != UI_SCREEN_HELP && key == '=' && !log_search_is_entering()) {
    ui_controller_hide_logs();
    stats_runtime_toggle();
    key = KEY_NONE;
  }
  mutex_unlock(&g_input_mutex);
  return key;
}

keyboard_key_t ui_input_wait_key(ui_screen_t screen, unsigned timeout_ms) {
  uint64_t deadline = time_get_ns() + (uint64_t)timeout_ms * NS_PER_MS_INT;
  do {
    keyboard_key_t key = ui_input_read_key(screen);
    if (key != KEY_NONE || shutdown_is_requested())
      return key;
    platform_sleep_ns(NS_PER_MS_INT);
  } while (time_get_ns() < deadline);
  return KEY_NONE;
}

void ui_input_shutdown(void) {
  if (!lifecycle_destroy_once(&g_input_lifecycle))
    return;
  mutex_destroy(&g_input_prompt_mutex);
  mutex_destroy(&g_input_mutex);
  NAMED_UNREGISTER(&g_input_lifecycle.state);
  lifecycle_destroy_commit(&g_input_lifecycle);
}

asciichat_error_t ui_input_prompt_prefilled(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts) {
  if (!prompt || !buffer || max_len < 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid prompt buffer");
  if (keyboard_init() != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Cannot initialize prompt input");
  char *visible = SAFE_CALLOC(max_len, 1, char *);
  if (!visible)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate prompt display");
  if (!input_initialize()) {
    SAFE_FREE(visible);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize prompt input lock");
  }
  mutex_lock(&g_input_prompt_mutex);
  const char *end = memchr(buffer, '\0', max_len);
  if (!end) {
    mutex_unlock(&g_input_prompt_mutex);
    SAFE_FREE(visible);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Prompt text is too long");
  }
  size_t len = (size_t)(end - buffer), cursor = len;
  unsigned seconds = opts.timeout_seconds ? opts.timeout_seconds : (opts.echo ? 30 : 60);
  uint64_t deadline = ui_input_deadline(seconds);
  char timed_prompt[8192];
  snprintf(timed_prompt, sizeof(timed_prompt), "%s\n(Timeout: %us; cancels without an answer)", prompt, seconds);
  asciichat_error_t result = ASCIICHAT_OK;
  while (!shutdown_is_requested()) {
    if (ui_input_expired(deadline)) {
      result = ERROR_PROMPT_TIMEOUT;
      break;
    }
    size_t display_cursor = cursor;
    if (opts.echo) {
      memcpy(visible, buffer, len + 1);
    } else {
      size_t chars = utf8_char_count(buffer);
      // A UTF-8 character can be incomplete between byte events.
      if (chars == SIZE_MAX)
        chars = len;
      memset(visible, opts.mask_char, chars);
      visible[opts.mask_char ? chars : 0] = '\0';
      display_cursor = 0;
      for (size_t i = 0; i < cursor; ++i)
        if (((unsigned char)buffer[i] & 0xc0) != 0x80)
          ++display_cursor;
      if (!opts.mask_char)
        display_cursor = 0;
    }
    result = ui_prompt_present(timed_prompt, visible, display_cursor);
    if (result != ASCIICHAT_OK)
      break;
    keyboard_key_t key = ui_input_wait_key(UI_SCREEN_PROMPT, 100);
    if (ui_input_expired(deadline)) {
      result = ERROR_PROMPT_TIMEOUT;
      break;
    }
    if (key == KEY_NONE)
      continue;
    if (key == KEY_PASTE) {
      char pasted[8192];
      if (ui_input_take_paste(pasted, sizeof(pasted)) == ASCIICHAT_OK && !strpbrk(pasted, "\r\n\t") &&
          strlen(pasted) + len < max_len) {
        size_t bytes = strlen(pasted);
        memmove(buffer + cursor + bytes, buffer + cursor, len - cursor + 1);
        memcpy(buffer + cursor, pasted, bytes);
        cursor += bytes;
        len += bytes;
      } else {
        CLEAR_ERRNO();
      }
      continue;
    }
    if (key == '\r' || key == '\n')
      break;
    if (key == KEY_ESCAPE || key == 3) {
      result = SET_ERRNO(ERROR_GENERAL, "Prompt cancelled");
      break;
    }
    input_editor_key(buffer, max_len, &len, &cursor, key);
  }
  if (shutdown_is_requested())
    result = SET_ERRNO(ERROR_GENERAL, "Prompt interrupted");
  ui_prompt_remove();
  mutex_unlock(&g_input_prompt_mutex);
  SAFE_FREE(visible);
  if (result == ERROR_PROMPT_TIMEOUT)
    ui_input_timeout_report(seconds, opts.echo ? "answer declined" : "password entry cancelled");
  if (result != ASCIICHAT_OK)
    memset(buffer, 0, max_len);
  return result;
}

asciichat_error_t ui_input_prompt(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts) {
  if (buffer && max_len)
    buffer[0] = '\0';
  return ui_input_prompt_prefilled(prompt, buffer, max_len, opts);
}

asciichat_error_t ui_input_media_begin(const char *text) {
  if (!text || strlen(text) >= sizeof(g_media_editor.buffer) || !input_initialize())
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid media prompt text");
  mutex_lock(&g_input_mutex);
  memset(&g_media_editor, 0, sizeof(g_media_editor));
  memcpy(g_media_editor.buffer, text, strlen(text) + 1);
  g_media_editor.length = g_media_editor.cursor = strlen(text);
  g_media_editor.deadline = ui_input_deadline(120);
  asciichat_error_t result =
      ui_prompt_present("Open media: files or URLs\nDrop more files to queue; Enter plays; Esc cancels",
                        g_media_editor.buffer, g_media_editor.cursor);
  g_media_editor.active = result == ASCIICHAT_OK;
  mutex_unlock(&g_input_mutex);
  return result;
}

asciichat_error_t ui_input_media_take(char *output, size_t capacity) {
  if (!output || !input_initialize())
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid media prompt destination");
  mutex_lock(&g_input_mutex);
  asciichat_error_t result = ASCIICHAT_OK;
  if (!g_media_editor.submitted || g_media_editor.length >= capacity)
    result = SET_ERRNO(ERROR_INVALID_STATE, "No submitted media prompt");
  else
    memcpy(output, g_media_editor.buffer, g_media_editor.length + 1);
  g_media_editor.submitted = false;
  mutex_unlock(&g_input_mutex);
  return result;
}
