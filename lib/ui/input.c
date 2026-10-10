#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/ui/sync.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/prompt.h>
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
  int length = snprintf(message, sizeof(message),
                        "\nPrompt timed out after %u seconds; %s.\n",
                        seconds, consequence);
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

void ui_input_poll_sync(void) {
#ifndef NDEBUG
  if (!input_initialize() || mutex_trylock(&g_input_mutex) != 0)
    return;
  int screen = ui_controller_current_screen();
  if (screen >= 0 && screen <= UI_SCREEN_SYNC && !ui_controller_is_blocked()) {
    keyboard_key_t key = keyboard_read_nonblocking();
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
    keyboard_key_t notice_key = keyboard_read_nonblocking();
    if (notice_key == KEY_ESCAPE || notice_key == '\r' || notice_key == '\n' || notice_key == 'q' || notice_key == 3)
      ui_controller_remove(UI_SCREEN_NOTICE);
    mutex_unlock(&g_input_mutex);
    return notice_key == 'q' || notice_key == 3 ? notice_key : KEY_NONE;
  }
  bool active =
      state.screen < 0 || state.screen == (int)screen || (screen == UI_SCREEN_MEDIA && state.screen == UI_SCREEN_HELP);
  keyboard_key_t key = KEY_NONE;
  if (active) {
    if (state.covered || g_input_was_covered) {
      // Drain on recovery too, including keys buffered since the last poll.
      g_sync_key_count = 0;
      for (int i = 0; i < 256 && keyboard_read_nonblocking() != KEY_NONE; ++i) {
      }
    } else {
      if (g_sync_key_count) {
        key = g_sync_keys[g_sync_key_head];
        g_sync_key_head = (g_sync_key_head + 1) % 256;
        --g_sync_key_count;
      }
      if (key == KEY_NONE)
        key = state.screen == UI_SCREEN_HELP && keyboard_help_check_signal_cancel() ? KEY_QUESTION
                                                                                  : keyboard_read_nonblocking();
      key = ui_sync_handle_key(key);
    }
    g_input_was_covered = state.covered;
  }
  if ((screen == UI_SCREEN_MEDIA || screen == UI_SCREEN_STATUS || screen == UI_SCREEN_SPLASH) &&
      state.screen != UI_SCREEN_HELP && key == '=' && !log_search_is_entering()) {
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

asciichat_error_t ui_input_prompt(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts) {
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
  size_t len = 0, cursor = 0;
  buffer[0] = '\0';
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
    if (key == '\r' || key == '\n')
      break;
    if (key == KEY_ESCAPE || key == 3) {
      result = SET_ERRNO(ERROR_GENERAL, "Prompt cancelled");
      break;
    }
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
