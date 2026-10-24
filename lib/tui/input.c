#include <ascii-chat/otel.h>
#include <ascii-chat/tui/shortcuts.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/buttons.h>
#include <sodium.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/util/env.h>
#include <ascii-chat/tui/errno_stacks.h>
#include <ascii-chat/tui/error_popup.h>
#include <ascii-chat/tui/terminal_screen.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/tui/sync.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/media/playlist.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/tui/update_banner.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/platform/clipboard.h>
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
static _Atomic bool g_input_was_covered;
static keyboard_key_t g_sync_keys[256];
static unsigned g_sync_key_head, g_sync_key_count;
static bool g_switch_pending;
static _Atomic bool g_input_buffered;

static bool input_switch_key(keyboard_key_t key) {
  return key == '=' || key == KEY_BACKTICK || key == KEY_QUESTION || key == ']' || key == 'o' || key == 'O' ||
         key == 15;
}

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

// Publish only whether routing work remains; the actual queues stay serialized.
static void input_unlock(void) {
  atomic_store(&g_input_buffered,
               g_sync_key_count || g_text_count || g_paste_ready || g_pasting || g_media_editor.active);
  mutex_unlock(&g_input_mutex);
}

static bool input_ready(void) {
  return atomic_load(&g_input_buffered) || keyboard_input_available();
}

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
    g_switch_pending = false;
    lifecycle_init_commit(&g_input_lifecycle);
  }
  uint64_t deadline = time_get_ns() + NS_PER_SEC_INT;
  while (atomic_load_u64(&g_input_lifecycle.state) == LIFECYCLE_INITIALIZING) {
    if (time_get_ns() >= deadline) {
      SET_ERRNO(ERROR_THREAD, "Timed out waiting for terminal input initialization");
      return false;
    }
    platform_sleep_ms(1);
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
  sodium_memzero(g_paste, sizeof(g_paste));
  g_paste_ready = false;
  input_unlock();
  return result;
}

void ui_input_poll_sync(void) {
#ifndef NDEBUG
  if (atomic_load_u64(&g_input_lifecycle.state) == LIFECYCLE_INITIALIZING || !input_initialize() || !input_ready() ||
      mutex_trylock(&g_input_mutex) != 0)
    return;
  if (g_switch_pending) {
    input_unlock();
    return;
  }
  int screen = ui_controller_current_screen();
  if (screen >= 0 && screen <= UI_SCREEN_SYNC && !ui_controller_is_blocked()) {
    keyboard_key_t key = input_read_event();
    if (key != KEY_PASTE && !log_search_is_entering())
      key = ui_sync_handle_key(key);
    if (key && g_sync_key_count < 256) {
      g_sync_keys[(g_sync_key_head + g_sync_key_count++) % 256] = key;
      g_switch_pending = input_switch_key(key);
    }
  }
  input_unlock();
#endif
}

static keyboard_key_t input_common_key(keyboard_key_t key, int current) {
  if (key == KEY_NONE || key == KEY_PASTE || log_search_is_entering())
    return key;
  tui_shortcut_t action = tui_shortcut_action(key);
  if (action == TUI_SHORTCUT_STATS) {
    ascii_otel_ui_usage("stats", "shortcut", 0);
    stats_runtime_toggle();
    return KEY_NONE;
  }
  if (action == TUI_SHORTCUT_HELP && current != UI_SCREEN_LOGS) {
    keyboard_help_toggle_terminal(ui_controller_active_fd());
    return KEY_NONE;
  }
  if (action == TUI_SHORTCUT_LOGS) {
    ascii_otel_ui_usage("logs", "shortcut", 0);
    ui_controller_toggle_logs();
    return KEY_NONE;
  }
  if (action == TUI_SHORTCUT_ERRORS) {
    ascii_otel_ui_usage("errno", "shortcut", 0);
    ui_errno_stacks_toggle();
    return KEY_NONE;
  }
  if (key == KEY_ESCAPE && current == UI_SCREEN_HELP) {
    keyboard_help_toggle_terminal(ui_controller_active_fd());
    return KEY_NONE;
  }
  bool server = GET_OPTION(detected_mode) == MODE_SERVER || GET_OPTION(detected_mode) == MODE_DISCOVERY_SERVICE ||
                GET_OPTION(detected_mode) == MODE_MCP_SERVER;
  if (server && current == UI_SCREEN_LOGS) {
    if (ui_session_logs_key(key))
      return KEY_NONE;
    if (key == KEY_ESCAPE) {
      ui_controller_hide_logs();
      return KEY_NONE;
    }
  }
  if (server && (key == 'q' || key == 3)) {
    shutdown_request();
    return KEY_NONE;
  }
  return key;
}

keyboard_key_t ui_input_read_key(ui_screen_t screen) {
  if (!input_initialize())
    return KEY_NONE;
  if (!input_ready()) {
    // Once recovery has an empty input queue, the next key is a fresh action.
    if (atomic_load(&g_input_was_covered) && !ui_controller_state().covered)
      atomic_store(&g_input_was_covered, false);
    return KEY_NONE;
  }
  mutex_lock(&g_input_mutex);
  ui_presentation_state_t state = ui_controller_state();
  // The previous event has now been dispatched by the session. Background
  // polling must not overtake a pending switch with the next shortcut.
  g_switch_pending = false;
  if (state.screen == UI_SCREEN_HELP &&
      (ui_error_popup_visible() || keyboard_help_parent_screen() == UI_SCREEN_ERROR)) {
    keyboard_key_t key = keyboard_help_check_signal_cancel() ? KEY_ESCAPE : input_next_key();
    if (key == KEY_ESCAPE || key == KEY_QUESTION)
      keyboard_help_toggle_terminal(ui_controller_active_fd());
    if (key == KEY_PASTE)
      g_paste_ready = false;
    input_unlock();
    return KEY_NONE;
  }
  if (state.screen == UI_SCREEN_ERROR && terminal_screen_logs_help_open()) {
    keyboard_key_t key = input_next_key();
    if (key != KEY_NONE)
      terminal_screen_logs_handle_key(key);
    if (key == KEY_PASTE)
      g_paste_ready = false;
    input_unlock();
    return KEY_NONE;
  }
  if (state.screen == UI_SCREEN_ERROR) {
    keyboard_key_t key = input_next_key();
    if (key == KEY_QUESTION)
      keyboard_help_toggle_terminal(ui_controller_active_fd());
    else if (key != KEY_NONE && key != KEY_PASTE && key != 3)
      ui_error_popup_key(key);
    if (key == KEY_PASTE)
      g_paste_ready = false;
    input_unlock();
    return key == 3 ? key : KEY_NONE;
  }
  if (state.screen == UI_SCREEN_NOTICE) {
    g_paste_ready = false;
    g_sync_key_count = 0;
    keyboard_key_t notice_key = input_read_event();
    if (notice_key == KEY_PASTE)
      g_paste_ready = false;
    if (notice_key == KEY_ESCAPE || notice_key == '\r' || notice_key == '\n' || notice_key == 'q' || notice_key == 3)
      ui_controller_remove(UI_SCREEN_NOTICE);
    input_unlock();
    return notice_key == 'q' || notice_key == 3 ? notice_key : KEY_NONE;
  }
  if (state.screen == UI_SCREEN_UPDATE && update_banner_is_overlay_open()) {
    keyboard_key_t update_key = input_read_event();
    if (update_key == '=' || update_key == KEY_0 || update_key == KEY_QUESTION || update_key == ']') {
      if (update_key != KEY_QUESTION)
        update_banner_toggle_overlay();
      if (update_key == KEY_QUESTION) {
        keyboard_help_toggle_terminal(ui_controller_active_fd());
      } else {
        update_key = input_common_key(update_key, UI_SCREEN_UPDATE);
        (void)ui_sync_handle_key(update_key);
      }
    } else if (update_key != KEY_NONE)
      (void)update_banner_handle_key(update_key);
    input_unlock();
    return KEY_NONE;
  }
  if (screen == UI_SCREEN_MEDIA && g_media_editor.active && state.screen == UI_SCREEN_PROMPT) {
    if (state.covered || g_input_was_covered) {
      g_paste_ready = false;
      g_sync_key_count = 0;
      for (unsigned n = 0; n < 256 && input_read_event() != KEY_NONE; ++n) {
        g_paste_ready = false;
      }
      g_input_was_covered = state.covered;
      input_unlock();
      return KEY_NONE;
    }
    keyboard_key_t key = input_next_key();
    bool switch_screen = input_switch_key(key) && key != '=' && (g_media_editor.length == 0 || key == 15);
#ifndef NDEBUG
    switch_screen |= key == KEY_0 && g_media_editor.length == 0;
#endif
    if (switch_screen) {
      // Keep the old prompt on screen until its replacement is published.
      if (key != KEY_QUESTION)
        g_media_editor.active = false;
      if (key == KEY_0)
        key = ui_sync_handle_key(key);
      key = input_common_key(key, state.screen);
      g_switch_pending = input_switch_key(key);
      input_unlock();
      return key;
    }
    if (key == KEY_ESCAPE || key == 3 || ui_input_expired(g_media_editor.deadline)) {
      if (key == KEY_PASTE)
        g_paste_ready = false;
      ascii_otel_ui_usage("media_input_prompt", ui_input_expired(g_media_editor.deadline) ? "timed_out" : "cancelled",
                          0);
      g_media_editor.active = false;
      ui_prompt_remove();
    } else if (key == '\r' || key == '\n') {
      g_media_editor.active = false;
      g_media_editor.submitted = true;
      ascii_otel_ui_usage("media_input_prompt", "submitted", 0);
      input_unlock();
      return KEY_MEDIA_SUBMIT;
    } else if (key != KEY_NONE) {
      if (key == KEY_PASTE)
        ascii_otel_ui_usage("media_input_prompt", "paste", 0);
      if (key == KEY_PASTE) {
        if (!g_paste_invalid && utf8_is_valid(g_paste)) {
          media_playlist_t items = {0}, added = {0};
          bool valid = !g_media_editor.length || media_playlist_parse(g_media_editor.buffer, &items) == ASCIICHAT_OK;
          valid = valid && media_playlist_parse(g_paste, &added) == ASCIICHAT_OK;
          if (valid) {
            for (size_t i = 0; i < added.count && valid; ++i)
              valid = media_playlist_append(&items, added.paths[i], added.urls[i]) == ASCIICHAT_OK;
            char formatted[sizeof(g_media_editor.buffer)];
            valid = valid && media_playlist_format(&items, formatted, sizeof(formatted)) == ASCIICHAT_OK;
            if (valid) {
              memcpy(g_media_editor.buffer, formatted, strlen(formatted) + 1);
              g_media_editor.length = g_media_editor.cursor = strlen(formatted);
            }
          }
          if (valid)
            ascii_otel_ui_usage("media_input_prompt", "queue_changed", 0);
          CLEAR_ERRNO();
        }
        g_paste_ready = false;
        g_media_editor.pasted = true;
      } else if (!(key == ' ' && g_media_editor.pasted && g_media_editor.cursor == g_media_editor.length)) {
        g_media_editor.pasted = false;
        ui_input_edit_key(g_media_editor.buffer, sizeof(g_media_editor.buffer), &g_media_editor.length,
                          &g_media_editor.cursor, key);
      }
      if (ui_prompt_present_kind("Open media: files or URLs\nDrop more files to queue; Enter plays; Esc cancels",
                                 g_media_editor.buffer, g_media_editor.cursor, false,
                                 UI_PROMPT_MEDIA_INPUT) != ASCIICHAT_OK) {
        g_media_editor.active = false;
        ui_prompt_remove();
        CLEAR_ERRNO();
      }
    }
    input_unlock();
    return KEY_NONE;
  }
  bool primary =
      screen == UI_SCREEN_MEDIA || screen == UI_SCREEN_SPLASH || screen == UI_SCREEN_STATUS || screen == UI_SCREEN_MDNS;
  bool server = GET_OPTION(detected_mode) == MODE_SERVER || GET_OPTION(detected_mode) == MODE_DISCOVERY_SERVICE ||
                GET_OPTION(detected_mode) == MODE_MCP_SERVER;
  bool server_owner = screen == UI_SCREEN_STATS && server && !ui_controller_screen_live(UI_SCREEN_STATUS);
  bool active = state.screen < 0 || state.screen == (int)screen ||
                ((primary || server_owner) && state.screen <= UI_SCREEN_SYNC && state.screen != UI_SCREEN_STATS);
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
      if (key == KEY_F12) {
        update_banner_handle_key(key);
        key = KEY_NONE;
      }
      if (screen != UI_SCREEN_PROMPT) {
        key = input_common_key(key, state.screen);
        if (key != KEY_PASTE && !log_search_is_entering() &&
            !(screen == UI_SCREEN_MDNS && state.screen == UI_SCREEN_MDNS))
          key = ui_sync_handle_key(key);
      }
    }
    g_input_was_covered = state.covered;
  }
  if (key == KEY_PASTE && screen != UI_SCREEN_MEDIA && screen != UI_SCREEN_PROMPT) {
    g_paste_ready = false;
    key = KEY_NONE;
  }
  if (key != KEY_NONE && update_banner_handle_key(key))
    key = KEY_NONE;
  g_switch_pending |= input_switch_key(key);
  input_unlock();
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

/** Own a synchronous prompt while the controller renders masked snapshots.
 * Password dialogs keep navigation in the shared editor, route Tab/Enter to
 * buttons, and reject excess newlines before insertion. Clipboard and terminal
 * paste share atomic insertion; temporary secret buffers are wiped on exit.
 */
static asciichat_error_t input_prompt(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts,
                                      bool password_dialog, bool incorrect) {
  if (!prompt || !buffer || max_len < 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid prompt buffer");
  if (keyboard_init() != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Cannot initialize prompt input");
  const char *usage_name = ui_prompt_kind_name(
      password_dialog || (opts.kind == UI_PROMPT_INPUT && !opts.echo) ? UI_PROMPT_PASSWORD : opts.kind);
  if (password_dialog && incorrect)
    ascii_otel_ui_usage(usage_name, "rejected", 0);
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
  bool newline_limit = false, cancelled = false;
  // Request modifier-aware Enter while this dialog owns terminal input.
  if (password_dialog) {
    const char *enable = "\033[>1u\033[>4;2m";
    platform_write_all(STDERR_FILENO, enable, strlen(enable));
  }
  ui_buttons_t buttons = {0};
  ui_button_t actions[] = {{"[ Confirm ]", 0, false}, {"[ Cancel ]", 0, false}};
  while (!shutdown_is_requested()) {
    if (ui_input_expired(deadline)) {
      result = ERROR_PROMPT_TIMEOUT;
      break;
    }
    size_t display_cursor = cursor;
    if (opts.echo) {
      memcpy(visible, buffer, len + 1);
    } else {
      size_t chars = 0;
      for (size_t i = 0; i < len; ++i) {
        if (((unsigned char)buffer[i] & 0xc0) != 0x80)
          visible[chars++] = password_dialog && buffer[i] == '\n' ? '\n' : opts.mask_char;
      }
      visible[opts.mask_char ? chars : 0] = '\0';
      display_cursor = 0;
      for (size_t i = 0; i < cursor; ++i)
        if (((unsigned char)buffer[i] & 0xc0) != 0x80)
          ++display_cursor;
      if (!opts.mask_char)
        display_cursor = 0;
    }
    // Presentation owns only the mask; the caller retains the credential.
    result = password_dialog
                 ? ui_password_prompt_present(prompt, visible, display_cursor, buttons.selected, incorrect,
                                              len >= MIN_PASSWORD_LENGTH, newline_limit)
                 : ui_prompt_present_kind(timed_prompt, visible, display_cursor, !opts.echo,
                                          opts.kind == UI_PROMPT_INPUT && !opts.echo ? UI_PROMPT_PASSWORD : opts.kind);
    if (result != ASCIICHAT_OK)
      break;
    keyboard_key_t key = ui_input_wait_key(UI_SCREEN_PROMPT, 100);
    if (ui_input_expired(deadline)) {
      result = ERROR_PROMPT_TIMEOUT;
      break;
    }
    if (key == KEY_NONE)
      continue;
    newline_limit = false;
    if (password_dialog && key == KEY_SHIFT_ENTER) {
      size_t newlines = 0;
      for (size_t i = 0; i < len; ++i)
        newlines += buffer[i] == '\n';
      if (newlines >= 3)
        newline_limit = true;
      else
        (void)ui_input_insert(buffer, max_len, &len, &cursor, "\n");
      continue;
    }
    actions[0].disabled = password_dialog && len < MIN_PASSWORD_LENGTH;
    if (password_dialog && (key == '\t' || key == '\r' || key == '\n')) {
      int activated = ui_buttons_key(&buttons, actions, 2, key);
      if (activated == 0)
        break;
      if (activated == 1) {
        cancelled = true;
        result = SET_ERRNO(ERROR_CRYPTO_AUTH, "Password entry cancelled");
        break;
      }
      continue;
    }
    // Validate the whole paste before editing so rejection cannot leave a partial password.
    if (key == KEY_PASTE || key == 22) {
      char pasted[8192] = {0};
      asciichat_error_t pasted_result =
          key == 22 ? platform_clipboard_read(pasted, sizeof(pasted)) : ui_input_take_paste(pasted, sizeof(pasted));
      if (pasted_result == ASCIICHAT_OK) {
        if (key == 22) {
          size_t write = 0;
          for (size_t read = 0; pasted[read]; ++read) {
            if (pasted[read] == '\r' && pasted[read + 1] == '\n')
              continue;
            pasted[write++] = pasted[read] == '\r' ? '\n' : pasted[read];
          }
          pasted[write] = '\0';
        }
        size_t newlines = 0;
        for (size_t i = 0; i < len; ++i)
          newlines += buffer[i] == '\n';
        for (size_t i = 0; pasted[i]; ++i)
          newlines += pasted[i] == '\n';
        if (password_dialog && newlines > 3)
          newline_limit = true;
        else if (!strpbrk(pasted, password_dialog ? "\r\t" : "\r\n\t") && strlen(pasted) + len < max_len)
          (void)ui_input_insert(buffer, max_len, &len, &cursor, pasted);
        else
          CLEAR_ERRNO();
      } else {
        CLEAR_ERRNO();
      }
      // Clipboard and terminal paste buffers can contain unmasked credentials.
      sodium_memzero(pasted, sizeof(pasted));
      continue;
    }
    if (key == '\r' || key == '\n') {
      if (!password_dialog || len >= MIN_PASSWORD_LENGTH)
        break;
      continue;
    }
    if (key == KEY_ESCAPE || key == 3) {
      cancelled = true;
      result = SET_ERRNO(ERROR_GENERAL, "Prompt cancelled");
      break;
    }
    ui_input_edit_key(buffer, max_len, &len, &cursor, key);
  }
  if (shutdown_is_requested())
    result = SET_ERRNO(ERROR_GENERAL, "Prompt interrupted");
  ascii_otel_ui_usage(usage_name,
                      result == ASCIICHAT_OK           ? "submitted"
                      : result == ERROR_PROMPT_TIMEOUT ? "timed_out"
                      : shutdown_is_requested()        ? "interrupted"
                      : cancelled                      ? "cancelled"
                                                       : "failed",
                      0);
  ui_prompt_remove();
  if (password_dialog) {
    const char *disable = "\033[<u\033[>4;0m";
    platform_write_all(STDERR_FILENO, disable, strlen(disable));
  }
  mutex_unlock(&g_input_prompt_mutex);
  SAFE_FREE(visible);
  if (result == ERROR_PROMPT_TIMEOUT)
    ui_input_timeout_report(seconds, opts.echo ? "answer declined" : "password entry cancelled");
  if (result != ASCIICHAT_OK)
    sodium_memzero(buffer, max_len);
  return result;
}

asciichat_error_t ui_input_prompt_prefilled(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts) {
  return input_prompt(prompt, buffer, max_len, opts, false, false);
}

asciichat_error_t ui_input_password(const char *target, char *buffer, size_t capacity, bool incorrect) {
  if (!target || !buffer || capacity < 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid password dialog");
  sodium_memzero(buffer, capacity);
  if (env_pop_prompt_response(buffer, capacity))
    return ASCIICHAT_OK;
  if (!platform_is_interactive())
    return SET_ERRNO(ERROR_CRYPTO_AUTH, "Server password requires an interactive terminal or --password");
  asciichat_error_t result = input_prompt(target, buffer, capacity, PROMPT_OPTS_PASSWORD, true, incorrect);
  if (result != ASCIICHAT_OK) {
    shutdown_request();
    APP_CALLBACK_VOID(signal_exit);
    return SET_ERRNO(ERROR_CRYPTO_AUTH, "Password entry cancelled");
  }
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
      ui_prompt_present_kind("Open media: files or URLs\nDrop more files to queue; Enter plays; Esc cancels",
                             g_media_editor.buffer, g_media_editor.cursor, false, UI_PROMPT_MEDIA_INPUT);
  g_media_editor.active = result == ASCIICHAT_OK;
  input_unlock();
  return result;
}

asciichat_error_t ui_input_media_take(char *output, size_t capacity) {
  if (!output || !input_initialize())
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid media prompt destination");
  mutex_lock(&g_input_mutex);
  asciichat_error_t result = ASCIICHAT_OK;
  if (!g_media_editor.submitted || g_media_editor.length >= capacity)
    result = SET_ERRNO(ERROR_INVALID_STATE, "No submitted media prompt");
  else {
    memcpy(output, g_media_editor.buffer, g_media_editor.length + 1);
    asciichat_errno_scope_t scope = errno_scope_begin();
    media_playlist_t playlist = {0};
    if (media_playlist_parse(output, &playlist) == ASCIICHAT_OK) {
      bool file = false, url = false;
      for (size_t i = 0; i < playlist.count; ++i) {
        file |= !playlist.urls[i];
        url |= playlist.urls[i];
      }
      if (file)
        ascii_otel_ui_usage("media_input_prompt", "source_file", 0);
      if (url)
        ascii_otel_ui_usage("media_input_prompt", "source_url", 0);
      ascii_otel_ui_usage("media_input_prompt", "queue_submitted", 0);
    } else {
      ascii_otel_ui_usage("media_input_prompt", "rejected", 0);
    }
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  }
  g_media_editor.submitted = false;
  input_unlock();
  return result;
}
