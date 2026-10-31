#include <ascii-chat/otel.h>
#include <ascii-chat/ui/text.h>
#include <ascii-chat/ui/progress.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/tui/invitation.h>
#include <ascii-chat/ui/output.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/tui/sync.h>
#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/tui/notice.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/tui/terminal_screen.h>
#include <ascii-chat/tui/errno_stacks.h>
#include <ascii-chat/tui/error_popup.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/tui/too_small.h>
#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/tui/update_banner.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <stdatomic.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/runtime.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/video/ascii/rle.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/debug/named.h>

typedef struct {
  _Atomic unsigned references;
  int fd;
} screen_terminal_t;

typedef struct media_snapshot_slot media_snapshot_slot_t;

typedef struct {
  _Atomic unsigned references;
  media_snapshot_slot_t *slot;
  screen_terminal_t *terminal;
  uint64_t frame_id, submitted_ns, acquisition_started_ns;
  _Alignas(max_align_t) char data[];
} screen_snapshot_t;

struct media_snapshot_slot {
  _Atomic bool busy;
  screen_snapshot_t *snapshot;
  size_t capacity;
};
static media_snapshot_slot_t g_media_snapshots[3];

typedef struct {
  int fd;
  bool media;
  terminal_size_t dimensions;
  size_t len;
  const char *text;
} text_snapshot_t;
static void render_text(terminal_size_t size, const void *data);

typedef struct screen {
  struct screen *next;
  screen_snapshot_t *owned;
  void *snapshot;
  ui_render_fn render;
  terminal_size_t minimum;
  int fd;
  bool dirty;
} screen_t;

static screen_t g_screens[UI_SCREEN_COUNT];
static ui_session_t *g_session;
static _Thread_local frame_buffer_t *g_capture;
static _Thread_local const animation_sample_t *g_animation;
static lifecycle_t g_lifecycle = LIFECYCLE_INIT;
static mutex_t g_mutex;
static mutex_t g_title_packet_mutex;
static cond_t g_render_done;
static cond_t g_render_requested;
static int g_rendering_screen = -1;
static uint32_t g_rendering_layers;
static uint64_t g_render_started;
static uint64_t g_render_completed;
static asciichat_thread_t g_thread;
static atomic_t g_presentation_stop_requested = {0};
static _Thread_local bool g_owner;
static _Thread_local int g_render_fd = -1;
static _Thread_local screen_snapshot_t *g_render_snapshot;
static _Thread_local terminal_size_t g_render_size;
// Input readers need geometry and minimums, never a borrowed screen snapshot.
static _Atomic uint64_t g_size = UINT64_C(80) | (UINT64_C(24) << 32);
// Lower bits identify published slots; upper bits identify covered slots.
static _Atomic uint64_t g_input_state;
_Static_assert(UI_SCREEN_COUNT <= 32, "Input publication reserves 32 bits per screen mask");

static uint64_t pack_size(terminal_size_t size) {
  return (uint32_t)size.cols | ((uint64_t)(uint32_t)size.rows << 32);
}

static terminal_size_t unpack_size(uint64_t packed) {
  return (terminal_size_t){.cols = (int)(uint32_t)packed, .rows = (int)(uint32_t)(packed >> 32)};
}

static void publish_input_state_locked(void) {
  terminal_size_t size = unpack_size(atomic_load(&g_size));
  uint64_t state = 0;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    if (!g_screens[i].render)
      continue;
    terminal_size_t minimum = g_screens[i].minimum;
    if (i == UI_SCREEN_PROMPT)
      minimum.rows = ui_prompt_required_rows(g_screens[i].snapshot, size.cols);
    state |= UINT64_C(1) << i;
    if (size.cols <= 0 || size.rows <= 0 || ui_too_small(size, minimum))
      state |= UINT64_C(1) << (i + 32);
  }
  if (atomic_load(&g_input_state) != state)
    atomic_store(&g_input_state, state);
}

static int g_active = -1;
static _Atomic int g_interactive_screen = -1;
static _Atomic int g_background_screen = -1;
static _Atomic uint32_t g_screen_mask;
static atomic_t g_volume_until = {0};
static _Thread_local bool g_render_volume;
static _Thread_local bool g_volume_caps_valid;
static _Thread_local terminal_capabilities_t g_volume_caps;
static _Thread_local terminal_color_mode_t g_volume_color_mode;
static _Thread_local color_setting_t g_volume_color_setting;
static _Thread_local utf8_setting_t g_volume_utf8_setting;
static _Thread_local render_mode_t g_volume_render_mode;

static terminal_capabilities_t volume_capabilities(void) {
  terminal_color_mode_t mode = GET_OPTION(color_mode);
  color_setting_t color = GET_OPTION(color);
  utf8_setting_t utf8 = GET_OPTION(force_utf8);
  render_mode_t render = GET_OPTION(render_mode);
  if (!g_volume_caps_valid || mode != g_volume_color_mode || color != g_volume_color_setting ||
      utf8 != g_volume_utf8_setting || render != g_volume_render_mode) {
    g_volume_caps = apply_color_mode_override(detect_terminal_capabilities());
    g_volume_color_mode = mode;
    g_volume_color_setting = color;
    g_volume_utf8_setting = utf8;
    g_volume_render_mode = render;
    g_volume_caps_valid = true;
  }
  return g_volume_caps;
}

void ui_controller_show_volume(void) {
  if (!GET_OPTION(snapshot_mode))
    atomic_store_u64(&g_volume_until, time_get_ns() + 1500 * NS_PER_MS_INT);
}

static void append_volume(frame_buffer_t *buffer, terminal_size_t size) {
  if (size.cols < 8 || size.rows < 6)
    return;
  double volume = GET_OPTION(speakers_volume);
  if (volume < 0.0)
    volume = 0.0;
  if (volume > 1.0)
    volume = 1.0;
  int height = size.rows - 4;
  if (height > 10)
    height = 10;
  int filled = (int)(volume * height + 0.5);
  int top = (size.rows - height - 1) / 2 + 1;
  int col = size.cols - 5;
  terminal_capabilities_t caps = volume_capabilities();
  bool color = caps.color_level != TERM_COLOR_NONE && GET_OPTION(color) != COLOR_SETTING_FALSE;
  bool utf8 = terminal_should_use_utf8();
  frame_buffer_append(buffer, "\0337\033[0m", 6);
  for (int row = 0; row < height; ++row) {
    int level = height - row;
    bool lit = level <= filled;
    log_color_t shade = level * 3 <= height       ? LOG_COLOR_ERROR
                        : level * 3 <= height * 2 ? LOG_COLOR_WARN
                                                  : LOG_COLOR_INFO;
    frame_buffer_printf(buffer, "\033[%d;%dH", top + row, col);
    if (color)
      frame_buffer_printf(buffer, "%s", ui_text_color_prefix(lit ? shade : LOG_COLOR_GREY));
    const char *cells = utf8 ? (lit ? "██" : "░░") : (lit ? "##" : "..");
    frame_buffer_printf(buffer, " %s \033[0m", cells);
  }
  frame_buffer_printf(buffer, "\033[%d;%dH", top + height, col);
  if (volume <= 0.01)
    frame_buffer_append(buffer, "MUTE", 4);
  else
    frame_buffer_printf(buffer, "%3d%%", (int)(volume * 100.0 + 0.5));
  frame_buffer_append(buffer, "\033[0m\0338", 6);
}

int ui_controller_current_screen(void) {
  int background = atomic_load(&g_background_screen);
  int interactive = atomic_load(&g_interactive_screen);
  if (interactive == UI_SCREEN_HELP && (atomic_load(&g_screen_mask) & (1u << UI_SCREEN_HELP)) &&
      (background != UI_SCREEN_UPDATE || keyboard_help_parent_screen() == UI_SCREEN_UPDATE ||
       keyboard_help_parent_screen() == UI_SCREEN_ERROR))
    return UI_SCREEN_HELP;
  if ((background >= 0 || interactive >= 0) && ui_error_popup_visible())
    return UI_SCREEN_ERROR;
  if (interactive < 0 && background < UI_SCREEN_MDNS && (atomic_load(&g_screen_mask) & (1u << UI_SCREEN_MDNS)))
    return UI_SCREEN_MDNS;
  return background > interactive ? background : interactive;
}

static atomic_int g_default_fd = STDOUT_FILENO;

int ui_controller_active_fd(void) {
  int fd = atomic_load(&g_default_fd);
  if (!lifecycle_is_initialized(&g_lifecycle))
    return fd;
  mutex_lock(&g_mutex);
  if (g_active >= 0 && g_active < UI_SCREEN_COUNT && g_screens[g_active].render)
    fd = g_screens[g_active].fd;
  mutex_unlock(&g_mutex);
  return fd;
}

static bool interactive_screen(int screen) {
  return screen == UI_SCREEN_HELP || screen == UI_SCREEN_LOGS || screen == UI_SCREEN_ERRNO ||
         screen == UI_SCREEN_SYNC || screen == UI_SCREEN_STATS || screen == UI_SCREEN_PROMPT ||
         screen == UI_SCREEN_MDNS;
}

int ui_controller_interactive_screen(void) {
  return atomic_load(&g_interactive_screen);
}

void ui_controller_select_interactive(int screen) {
  atomic_store(&g_interactive_screen, screen);
}

static int selected_screen_excluding_locked(int excluded) {
  uint32_t visible = 0;
  int interactive = ui_controller_interactive_screen();
  if (interactive == UI_SCREEN_HELP)
    interactive = keyboard_help_previous_interactive();
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    if (i != excluded && g_screens[i].render &&
        (!interactive_screen(i) || i == interactive || (interactive < 0 && i == UI_SCREEN_MDNS)))
      visible |= 1u << i;
  if (excluded != UI_SCREEN_HELP && ui_controller_interactive_screen() == UI_SCREEN_HELP &&
      g_screens[UI_SCREEN_HELP].render) {
    // F12 covers existing help; help opened from the update checker covers it in turn.
    if (excluded != UI_SCREEN_UPDATE && g_screens[UI_SCREEN_UPDATE].render &&
        keyboard_help_parent_screen() != UI_SCREEN_UPDATE && keyboard_help_parent_screen() != UI_SCREEN_ERROR)
      return UI_SCREEN_UPDATE;
    return UI_SCREEN_HELP;
  }
  int selected = ui_session_select_screen(visible);
  // Sync presents from its independent mailbox. Never fall through to media
  // if it was selected after the presentation loop checked that mailbox.
  return interactive == UI_SCREEN_SYNC && selected < UI_SCREEN_SYNC ? -1 : selected;
}

static int selected_screen_locked(void) {
  return selected_screen_excluding_locked(-1);
}

static void publish_screen_locked(void) {
  publish_input_state_locked();
  uint32_t background = 0, live = 0;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    if (g_screens[i].render) {
      live |= 1u << i;
      if (!interactive_screen(i))
        background |= 1u << i;
    }
  }
  atomic_store(&g_screen_mask, live);
  int selected = ui_session_select_screen(background);
  if (atomic_load(&g_background_screen) != selected)
    atomic_store(&g_background_screen, selected);
  cond_signal(&g_render_requested);
}
static bool g_redraw;
static bool g_finished;
static uint64_t g_media_frame_id;
static _Thread_local uint64_t g_acquisition_started_ns;
static _Thread_local bool g_probe_frame;
static _Thread_local uint64_t g_probe_requested_ns;

void ui_controller_frame_acquired_at(uint64_t started_ns) {
  g_acquisition_started_ns = started_ns;
}
static atomic_t g_blocked = {0};
static atomic_t g_live = {0};
static _Thread_local fps_counter_t *g_fps;
static _Thread_local bool g_stats_media, g_stats_failed;
static _Thread_local uint64_t g_stats_write_ns, g_stats_writes, g_stats_max_chunk_ns, g_stats_chunks;
static void stats_media_begin(bool media) {
  g_stats_media = media;
  g_stats_failed = false;
  g_stats_write_ns = g_stats_writes = g_stats_max_chunk_ns = g_stats_chunks = 0;
  g_probe_frame = false;
  g_probe_requested_ns = 0;
}
static void stats_media_end(void) {
  if (g_stats_media && g_stats_writes) {
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_TERMINAL_WRITE, g_stats_write_ns);
    stats_counter_add(stats_runtime_scope(),
                      g_stats_failed ? STATS_COUNTER_FRAMES_DROPPED : STATS_COUNTER_FRAMES_PRESENTED, 1);
  }
  g_stats_media = false;
}

static size_t write_output_chunk(int fd, const void *data, size_t length) {
  uint64_t started_ns = g_stats_media ? time_get_ns() : 0;
  size_t written = platform_write_all(fd, data, length);
  if (g_stats_media) {
    uint64_t elapsed_ns = time_get_ns() - started_ns;
    if (elapsed_ns > g_stats_max_chunk_ns)
      g_stats_max_chunk_ns = elapsed_ns;
    ++g_stats_chunks;
  }
  return written;
}

asciichat_error_t ui_controller_write(int fd, const char *data, size_t len) {
  if (!data || !len)
    return ASCIICHAT_OK;
  if (g_capture) {
    frame_buffer_append(g_capture, data, len);
    return ASCIICHAT_OK;
  }
  // Logging may call this while holding subsystem locks; never wait for a
  // renderer callback here. Live screens already capture logs in their model.
  if (!g_owner && ui_controller_is_presenting())
    return ASCIICHAT_OK;
  if (g_owner && g_render_fd >= 0)
    fd = g_render_fd;
  fps_counter_write_begin(g_fps);
  uint64_t stats_start = g_stats_media ? time_get_ns() : 0;
  // Terminals supporting synchronized output present the art and its overlays
  // together, even when a large UTF-8 frame requires several writes.
  bool synchronized = g_render_snapshot != NULL;
  bool started = !synchronized || write_output_chunk(fd, "\033[?2026h", 8) == 8;
  size_t written = 0;
  bool superseded = false;
  while (started && written < len) {
    // A pending editor or overlay supersedes the remainder of a media frame.
    // Bound each console write so a large frame cannot delay that transition.
    if (g_stats_media && ui_controller_current_screen() != UI_SCREEN_MEDIA) {
      superseded = true;
      break;
    }
    size_t count = len - written;
    if (g_stats_media && count > 65536) {
      count = 65536;
      while (count && ((unsigned char)data[written + count] & 0xc0) == 0x80)
        --count;
    }
    size_t sent = write_output_chunk(fd, data + written, count);
    written += sent;
    if (sent != count)
      break;
  }
  // End the update even when input superseded the frame halfway through.
  static const char end_with_query[] = "\033[?2026l\033]11;?\033\\";
  bool probe = synchronized && g_probe_frame && written == len && started;
  if (probe)
    g_probe_requested_ns = time_get_ns();
  size_t end_length = probe ? sizeof(end_with_query) - 1 : 8;
  bool ended = !synchronized || write_output_chunk(fd, end_with_query, end_length) == end_length;
  if (!ended)
    g_probe_requested_ns = 0;
  // Switching screens intentionally cancels the remainder of an old media frame.
  bool complete = (written == len || superseded) && started && ended;
  if (g_stats_media) {
    g_stats_write_ns += time_get_ns() - stats_start;
    g_stats_writes++;
    g_stats_failed |= !complete;
  }
  fps_counter_write_end(g_fps, complete);
  return complete ? ASCIICHAT_OK
                  : SET_ERRNO(ERROR_FILE_OPERATION, "Incomplete terminal frame write (%zu/%zu bytes)", written, len);
}

static void release_terminal(screen_terminal_t *terminal) {
  if (terminal && atomic_fetch_sub(&terminal->references, 1) == 1) {
    platform_close(terminal->fd);
    SAFE_FREE(terminal);
  }
}

static void release_snapshot(screen_snapshot_t *snapshot) {
  if (snapshot && atomic_fetch_sub(&snapshot->references, 1) == 1) {
    release_terminal(snapshot->terminal);
    snapshot->terminal = NULL;
    if (snapshot->slot)
      atomic_store_explicit(&snapshot->slot->busy, false, memory_order_release);
    else
      SAFE_FREE(snapshot);
  }
}

static screen_snapshot_t *allocate_snapshot(ui_screen_t screen, size_t bytes) {
  media_snapshot_slot_t *slot = NULL;
  if (screen == UI_SCREEN_MEDIA) {
    for (size_t i = 0; i < 3; ++i) {
      bool expected = false;
      if (atomic_compare_exchange_strong_explicit(&g_media_snapshots[i].busy, &expected, true, memory_order_acquire,
                                                  memory_order_relaxed)) {
        slot = &g_media_snapshots[i];
        break;
      }
    }
  }
  screen_snapshot_t *copy;
  if (slot) {
    if (slot->capacity < bytes) {
      size_t capacity = slot->capacity ? slot->capacity : 4096;
      while (capacity < bytes && capacity <= SIZE_MAX / 2)
        capacity *= 2;
      if (capacity < bytes)
        capacity = bytes;
      slot->snapshot = SAFE_REALLOC(slot->snapshot, capacity, screen_snapshot_t *);
      slot->capacity = capacity;
    }
    copy = slot->snapshot;
  } else {
    copy = SAFE_MALLOC(bytes, screen_snapshot_t *);
  }
  if (!copy) {
    if (slot)
      atomic_store(&slot->busy, false);
    return NULL;
  }
  atomic_init(&copy->references, 1);
  copy->slot = slot;
  copy->terminal = NULL;
  return copy;
}

static void destroy_media_snapshots(void) {
  for (size_t i = 0; i < 3; ++i) {
    bool expected = false;
    if (atomic_compare_exchange_strong(&g_media_snapshots[i].busy, &expected, true)) {
      SAFE_FREE(g_media_snapshots[i].snapshot);
      g_media_snapshots[i].capacity = 0;
      atomic_store(&g_media_snapshots[i].busy, false);
    }
  }
}

static void release_screen(screen_t *screen, bool flush_notices) {
  screen_t *current = screen;
  while (current) {
    screen_t *next = current->next;
    if (current->render) {
      if (flush_notices)
        ui_notice_flush_snapshot(current->fd, current->snapshot);
    }
    release_snapshot(current->owned);
    if (current != screen)
      SAFE_FREE(current);
    current = next;
  }
  memset(screen, 0, sizeof(*screen));
}

void ui_controller_finish(int fd, const char *data, size_t len) {
  errno_notifications_enable(false);
  screen_t retired[UI_SCREEN_COUNT] = {0};
  bool locked = lifecycle_is_initialized(&g_lifecycle);
  if (locked) {
    mutex_lock(&g_mutex);
    g_finished = true;
    memcpy(retired, g_screens, sizeof(retired));
    memset(g_screens, 0, sizeof(g_screens));
    atomic_store(&g_background_screen, -1);
    atomic_store(&g_screen_mask, 0);
    ui_controller_select_interactive(-1);
    cond_signal(&g_render_requested);
    uint64_t pending = g_render_started;
    while (!g_render_snapshot && g_render_completed < pending)
      cond_wait(&g_render_done, &g_mutex);
    mutex_unlock(&g_mutex);
  }
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    release_screen(&retired[i], i == UI_SCREEN_NOTICE);
  platform_write_all(fd, data, len);
  atomic_store_bool(&g_live, false);
}

bool ui_controller_is_blocked(void) {
  return atomic_load_bool(&g_blocked);
}

bool ui_controller_is_presenting(void) {
  return ui_controller_current_screen() >= 0;
}

bool ui_controller_is_owner(void) {
  return g_owner;
}

terminal_size_t ui_controller_size(void) {
  if (g_owner)
    return g_render_size;
  terminal_size_t size = {.cols = 80, .rows = 24};
  if (lifecycle_is_initialized(&g_lifecycle)) {
    size = unpack_size(atomic_load(&g_size));
  }
  // A title-only controller has no screen to poll geometry yet.
  if (!lifecycle_is_initialized(&g_lifecycle) || atomic_load(&g_screen_mask) == 0) {
    terminal_size_t detected = {0};
    asciichat_errno_scope_t scope = errno_scope_begin();
    int fd = atomic_load(&g_default_fd);
    if (platform_isatty(fd) && terminal_get_size_fd(fd, &detected) == ASCIICHAT_OK && detected.cols > 0 &&
        detected.rows > 0)
      size = detected;
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  }
  return size;
}

const animation_sample_t *ui_controller_animation_sample(void) {
  return g_animation;
}

typedef struct {
  screen_t *screen;
  screen_t *layers;
  size_t layer_count;
  int active;
  bool volume, drew, fresh, errors, sync_backdrop;
} native_view_t;

static void draw_native_view(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation,
                             void *context) {
  native_view_t *view = context;
  g_capture = output;
  g_animation = animation;
  g_render_volume = view->volume;
  bool previous = errno_notifications_suppress(true);
  if (view->errors || view->layer_count > 1)
    frame_buffer_clear_screen(output);
  if (view->sync_backdrop)
    ui_sync_render(size);
  for (size_t i = 0; i < view->layer_count; ++i) {
    screen_t *layer = &view->layers[i];
    bool help = i + 1 == view->layer_count && view->active == UI_SCREEN_HELP;
    if (help && view->errors)
      ui_error_popup_render(output, size);
    if (ui_too_small(size, layer->minimum)) {
      ui_too_small_render(output, size, layer->minimum);
      continue;
    }
    screen_snapshot_t *previous_snapshot = g_render_snapshot;
    g_render_snapshot = layer->owned;
    layer->render(size, layer->snapshot);
    g_render_snapshot = previous_snapshot;
  }
  view->fresh = fps_counter_frame_begin_id(
      g_fps, view->active == UI_SCREEN_MEDIA && !view->errors ? view->screen->owned->frame_id : 0);
  fps_counter_render(g_fps, view->screen->fd, size.cols, true);
  if (view->errors && view->active != UI_SCREEN_HELP)
    ui_error_popup_render(output, size);
  if (view->errors && terminal_screen_logs_help_open() &&
      (view->active == UI_SCREEN_LOGS || view->active == UI_SCREEN_STATUS || view->active == UI_SCREEN_SPLASH))
    terminal_screen_logs_render_help(output, size);
  errno_notifications_suppress(previous);
  g_render_volume = false;
  g_animation = NULL;
  g_capture = NULL;
  view->drew = true;
}

static atomic_t g_title_active = {0};
static bool g_title_saved;
static bool g_title_written;
static bool g_title_startup_finished;
static int g_title_fd = -1;
static uint64_t g_title_started;
static atomic_t g_title_render = {0};
static atomic_t g_title_ready = {0};
static char g_last_title[512];
static const char *g_title_mode;
static atomic_t g_title_activity = {0};

void ui_controller_title_ready(void) {
  atomic_store_bool(&g_title_ready, true);
}
void ui_controller_title_render(ui_title_render_t phase) {
  atomic_store_int(&g_title_render, phase);
  if (phase != UI_TITLE_IDLE)
    ui_controller_title_ready();
}

void ui_controller_title_activity(ui_title_activity_t activity) {
  atomic_store_int(&g_title_activity, activity);
}
void ui_controller_title_discovery(bool has_session, bool joining) {
  ui_controller_title_activity(joining       ? UI_TITLE_JOINING_SESSION
                               : has_session ? UI_TITLE_WAITING_PEERS
                                             : UI_TITLE_CREATING_SESSION);
}

static const char *title_screen_label(int screen, const void *snapshot) {
  switch (screen) {
  case UI_SCREEN_LOGS:
    return "log viewer";
  case UI_SCREEN_ERRNO:
    return "errors";
  case UI_SCREEN_STATS:
    return "statistics";
  case UI_SCREEN_SYNC:
    return "sync primitives";
  case UI_SCREEN_MDNS:
    return "LAN server discovery";
  case UI_SCREEN_UPDATE:
    return "updates";
  case UI_SCREEN_PROMPT:
    return snapshot ? ui_prompt_title(snapshot) : "input";
  case UI_SCREEN_NOTICE:
    return snapshot ? ui_notice_title(snapshot) : "notice";
  default:
    return NULL;
  }
}

static void title_update(int screen, const void *snapshot, bool covered) {
  if (!atomic_load_bool(&g_title_active))
    return;
  // Preserve the shell's title once for application exit, never for view navigation.
  if (!g_title_saved) {
    asciichat_errno_scope_t scope = errno_scope_begin();
    mutex_lock(&g_title_packet_mutex);
    (void)terminal_title_save(g_title_fd);
    mutex_unlock(&g_title_packet_mutex);
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
    g_title_saved = true;
  }
  char title[512] = {0};
  if (terminal_should_use_utf8()) {
    int phase = atomic_load_int(&g_title_render);
    int activity = atomic_load_int(&g_title_activity);
    if (phase != UI_TITLE_IDLE || activity != UI_TITLE_ACTIVITY_IDLE || screen >= UI_SCREEN_LOGS ||
        (atomic_load_bool(&g_title_ready) && time_get_ns() - g_title_started >= 3 * NS_PER_SEC_INT))
      g_title_startup_finished = true;
    bool startup = !g_title_startup_finished;
    const char *status = phase == UI_TITLE_RENDERING             ? "rendering file..."
                         : phase == UI_TITLE_FINALIZING          ? "finalizing file..."
                         : phase == UI_TITLE_FAILED              ? "rendering failed"
                         : activity == UI_TITLE_CONNECTING       ? "connecting..."
                         : activity == UI_TITLE_RECONNECTING     ? "reconnecting..."
                         : activity == UI_TITLE_CREATING_SESSION ? "creating session..."
                         : activity == UI_TITLE_JOINING_SESSION  ? "joining session..."
                         : activity == UI_TITLE_WAITING_PEERS    ? "waiting for peers..."
                                                                 : NULL;
    bool spinning = phase == UI_TITLE_RENDERING || phase == UI_TITLE_FINALIZING ||
                    (phase == UI_TITLE_IDLE && activity != UI_TITLE_ACTIVITY_IDLE);
    // Spinner glyphs use one column; reserve the emoji's second column with padding.
    const char *icon = spinning ? ui_spinner_glyph(time_get_ns() - g_title_started, false) : "🔠";
    snprintf(title, sizeof(title), "%s%s ascii-chat • %s%s%s", icon, spinning ? " " : "",
             startup ? "video chat in your terminal" : g_title_mode, status ? " • " : "", status ? status : "");
    int parent = screen == UI_SCREEN_HELP ? keyboard_help_parent_screen() : screen;
    const char *label;
    if (screen == UI_SCREEN_HELP) {
      mutex_lock(&g_mutex);
      label = title_screen_label(parent, parent >= 0 && parent < UI_SCREEN_COUNT ? g_screens[parent].snapshot : NULL);
      mutex_unlock(&g_mutex);
    } else {
      label = title_screen_label(parent, snapshot);
    }
    if (parent == UI_SCREEN_RENDER_PROGRESS && phase == UI_TITLE_IDLE)
      label = "load file/url";
    size_t used = strlen(title);
    if (label)
      snprintf(title + used, sizeof(title) - used, " • %s", label);
    used = strlen(title);
    if (screen == UI_SCREEN_HELP || (screen == UI_SCREEN_LOGS && terminal_screen_logs_help_open()))
      snprintf(title + used, sizeof(title) - used, " • help");
    used = strlen(title);
    if (covered)
      snprintf(title + used, sizeof(title) - used, " • terminal too small");
  }
  if (!g_title_written || strcmp(title, g_last_title) != 0) {
    asciichat_errno_scope_t scope = errno_scope_begin();
    mutex_lock(&g_title_packet_mutex);
    (void)terminal_set_title_fd(g_title_fd, title);
    mutex_unlock(&g_title_packet_mutex);
    // Retry cosmetic failures only when the desired text changes.
    snprintf(g_last_title, sizeof(g_last_title), "%s", title);
    g_title_written = true;
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  }
}

static const char *usage_screen_name(int screen) {
  static const char *names[UI_SCREEN_COUNT] = {
      [UI_SCREEN_STATUS] = "status", [UI_SCREEN_MEDIA] = "media",
      [UI_SCREEN_SPLASH] = "splash", [UI_SCREEN_RENDER_PROGRESS] = "render_progress",
      [UI_SCREEN_LOGS] = "logs",     [UI_SCREEN_ERRNO] = "errno",
      [UI_SCREEN_HELP] = "help",     [UI_SCREEN_STATS] = "stats",
      [UI_SCREEN_SYNC] = "sync",     [UI_SCREEN_MDNS] = "mdns",
      [UI_SCREEN_UPDATE] = "update", [UI_SCREEN_NOTICE] = "notice",
      [UI_SCREEN_PROMPT] = "prompt", [UI_SCREEN_ERROR] = "error"};
  if (screen == UI_SCREEN_PROMPT) {
    mutex_lock(&g_mutex);
    const char *name = ui_prompt_usage_name(g_screens[UI_SCREEN_PROMPT].snapshot);
    mutex_unlock(&g_mutex);
    return name;
  }
  if (screen == UI_SCREEN_HELP)
    return keyboard_help_screen_name(keyboard_help_parent_screen());
  if ((screen == UI_SCREEN_LOGS || screen == UI_SCREEN_ERROR) && terminal_screen_logs_help_open())
    return "logs_help";
  return screen >= 0 && screen < UI_SCREEN_COUNT ? names[screen] : NULL;
}

// Only the presentation thread owns this state. Repaints do not create visits.
static void usage_transition(const char *next, const char **previous, uint64_t *since) {
  if (next == *previous || (next && *previous && strcmp(next, *previous) == 0))
    return;
  uint64_t now = time_get_ns();
  if (*previous)
    ascii_otel_ui_usage(*previous, "leave", now - *since);
  if (next)
    ascii_otel_ui_usage(next, "enter", 0);
  *previous = next;
  *since = now;
}

static void *presentation_main(void *unused) {
  (void)unused;
  g_owner = true;
  g_fps = fps_counter_create();
  bool was_sync = false;
  uint64_t last_volume_until = 0;
  bool volume_was_visible = false;
  uint64_t help_revision = 0;
  uint64_t reply_requested_ns = 0, reply_frame_ns = 0, reply_frame_id = 0, reply_written_ns = 0;
  int reply_support = 0;
  bool awaiting_reply = false;
  const char *usage_screen = NULL;
  uint64_t usage_since = 0;
  const char *stats_page = NULL, *covered_screen = NULL;
  uint64_t stats_since = 0, covered_since = 0;
  while (!atomic_load_bool(&g_presentation_stop_requested)) {
    int usage_current = ui_controller_current_screen();
    usage_transition(usage_screen_name(usage_current), &usage_screen, &usage_since);
    ui_presentation_state_t usage_state = ui_controller_state();
    const char *next_page = NULL;
    if (usage_current == UI_SCREEN_STATS && !usage_state.covered) {
      mutex_lock(&g_mutex);
      const stats_view_t *view = g_screens[UI_SCREEN_STATS].snapshot;
      if (view)
        next_page = view->page ? "stats_details" : "stats_overview";
      mutex_unlock(&g_mutex);
    }
    usage_transition(next_page, &stats_page, &stats_since);
    const char *next_covered = usage_state.covered ? usage_screen_name(usage_state.screen) : NULL;
    if (next_covered != covered_screen && (!next_covered || !covered_screen || strcmp(next_covered, covered_screen))) {
      uint64_t now = time_get_ns();
      if (covered_screen) {
        ascii_otel_ui_usage(covered_screen, "size_warning_ended", now - covered_since);
        if (!next_covered && usage_screen && !strcmp(covered_screen, usage_screen))
          ascii_otel_ui_usage(covered_screen, "size_warning_resolved", 0);
      }
      if (next_covered)
        ascii_otel_ui_usage(next_covered, "size_warning_started", 0);
      covered_screen = next_covered;
      covered_since = now;
    }
    uint64_t iteration_started_ns = time_get_ns();
    if (ui_controller_current_screen() < 0)
      title_update(-1, NULL, false);
    uint64_t refresh_deadline = iteration_started_ns + 16 * NS_PER_MS_INT;
    uint64_t replied_ns = keyboard_stream_terminal_reply_ns();
    if (reply_requested_ns && replied_ns >= reply_requested_ns) {
      reply_support = 1;
      if (awaiting_reply) {
        fps_counter_frame_end(g_fps, replied_ns);
        log_debug("TERMINAL_ACK: id=%llu response_ms=%.2f capture_to_terminal_ms=%.2f",
                  (unsigned long long)reply_frame_id, (double)(replied_ns - reply_requested_ns) / NS_PER_MS_INT,
                  reply_frame_ns ? (double)(replied_ns - reply_frame_ns) / NS_PER_MS_INT : 0);
      }
      awaiting_reply = false;
    }
    if (awaiting_reply && reply_support == 0 && iteration_started_ns - reply_requested_ns > 500 * NS_PER_MS_INT) {
      // Classic consoles need no remote acknowledgement and may not answer OSC.
      reply_support = -1;
      awaiting_reply = false;
      fps_counter_frame_end(g_fps, reply_written_ns);
    }
    if (awaiting_reply && !ui_error_popup_visible() && ui_controller_current_screen() == UI_SCREEN_MEDIA &&
        !shutdown_is_requested()) {
      // Keep only the latest mailbox frame while the frontend parses output.
      // Input and modal screens remain independent of this backpressure.
      platform_sleep_ms(2);
      continue;
    }
    // This path must remain independent of producers, including one stuck
    // holding the controller mutex. It reads only the collector's mailbox.
    if (ui_sync_is_visible() && ui_controller_current_screen() >= 0 &&
        (ui_controller_current_screen() <= UI_SCREEN_SYNC || ui_controller_current_screen() == UI_SCREEN_ERROR) &&
        !shutdown_is_requested()) {
      if (!was_sync) {
        const ui_session_view_t hidden = {.screen = -1};
        ui_session_render(g_session, &hidden, time_get_ns());
      }
      int fd = ui_sync_output_fd();
      terminal_size_t size = {0};
      g_render_fd = fd;
      if (terminal_get_size_fd(fd, &size) == ASCIICHAT_OK && size.cols > 0 && size.rows > 0) {
        g_render_size = size;
        title_update(UI_SCREEN_SYNC, NULL, false);
        atomic_store_bool(&g_blocked, false);
        if (ui_error_popup_visible()) {
          frame_buffer_t *composed = frame_buffer_create(size.rows, size.cols);
          if (composed) {
            bool previous = errno_notifications_suppress(true);
            g_capture = composed;
            ui_sync_render(size);
            ui_error_popup_render(composed, size);
            g_capture = NULL;
            ui_controller_write(fd, frame_buffer_get_content(composed), frame_buffer_get_length(composed));
            errno_notifications_suppress(previous);
            frame_buffer_destroy(composed);
          }
        } else {
          ui_sync_render(size);
        }
        atomic_store(&g_size, pack_size(size));
      }
      g_render_fd = -1;
      was_sync = true;
      platform_sleep_ns(50 * NS_PER_MS_INT);
      continue;
    }
    if (was_sync) {
      g_active = -1;
      ui_session_redraw(g_session);
      was_sync = false;
    }
    mutex_lock(&g_mutex);
    int active = selected_screen_locked();
    bool changed = active != g_active;
    g_active = active;
    bool redraw = g_redraw;
    g_redraw = false;
    terminal_size_t previous_size = unpack_size(atomic_load(&g_size));
    if ((active == UI_SCREEN_MEDIA || active == UI_SCREEN_HELP || active == UI_SCREEN_STATS) &&
        time_get_ns() >= atomic_load_u64(&g_volume_until))
      refresh_deadline = iteration_started_ns + 100 * NS_PER_MS_INT;
    screen_t frame = {0};
    screen_t layers[3] = {0};
    size_t layer_count = 0;
    if (active >= 0 && !g_finished && (!shutdown_is_requested() || active == UI_SCREEN_RENDER_PROGRESS)) {
      frame = g_screens[active];
      atomic_fetch_add(&frame.owned->references, 1);
      g_screens[active].dirty = false;
      g_rendering_screen = active;
      ++g_render_started;
    }
    mutex_unlock(&g_mutex);
    if (active < 0) {
      if (changed) {
        const char restore[] = "\033[0m\033[2J\033[H\033[?25h";
        ui_controller_write(atomic_load(&g_default_fd), restore, sizeof(restore) - 1);
      }
      const ui_session_view_t hidden = {.screen = -1};
      ui_session_render(g_session, &hidden, time_get_ns());
      atomic_store_bool(&g_blocked, false);
    }
    if (frame.render) {
      g_render_snapshot = frame.owned;
      screen_t *screen = &frame;
      terminal_resize_watcher_t watcher = {.size = previous_size};
      bool resized = false;
      // Observe physical changes once for all modes; consumers use the published dimensions.
      if (terminal_resize_poll(&watcher, screen->fd, &resized) != ASCIICHAT_OK) {
        // A minimized or detached terminal has no usable drawing area.
        atomic_store_bool(&g_blocked, true);
        const ui_session_view_t hidden = {.screen = -1};
        ui_session_render(g_session, &hidden, time_get_ns());
        goto frame_complete;
      }
      uint64_t geometry_done_ns = time_get_ns();
      terminal_size_t size = watcher.size;
      if (size.cols != previous_size.cols || size.rows != previous_size.rows) {
        mutex_lock(&g_mutex);
        atomic_store(&g_size, pack_size(size));
        publish_input_state_locked();
        mutex_unlock(&g_mutex);
      }
      g_render_size = size;
      if (active == UI_SCREEN_PROMPT)
        screen->minimum.rows = ui_prompt_required_rows(screen->snapshot, size.cols);
      title_update(active, screen->snapshot, ui_too_small(size, screen->minimum));
      uint64_t now = time_get_ns();
      uint64_t volume_until = atomic_load_u64(&g_volume_until);
      bool volume_visible =
          active == UI_SCREEN_MEDIA && !GET_OPTION(snapshot_mode) && !GET_OPTION(strip_ansi) && now < volume_until;
      bool volume_changed =
          volume_visible != volume_was_visible || (volume_visible && volume_until != last_volume_until);
      if (volume_was_visible && !volume_visible)
        redraw = true;
      volume_was_visible = volume_visible;
      last_volume_until = volume_until;
      bool show_fps = active == UI_SCREEN_MEDIA && GET_OPTION(fps_counter);
      redraw |= fps_counter_set_visible(g_fps, show_fps);
      bool was_covered = ui_session_is_covered(g_session);
      if (changed || was_covered)
        fps_counter_reset(g_fps);
      g_render_fd = screen->fd;
      frame_buffer_set_screen_output_fd(screen->fd);
      bool errors = ui_error_popup_visible();
      bool overlay = errors || active == UI_SCREEN_HELP || active == UI_SCREEN_UPDATE;
      bool sync_backdrop = false;
      mutex_lock(&g_mutex);
      if (active == UI_SCREEN_HELP || active == UI_SCREEN_UPDATE) {
        int parent = selected_screen_excluding_locked(active);
        // Help can cover the update dialog, which itself covers the retained TUI.
        if (parent == UI_SCREEN_UPDATE && active == UI_SCREEN_HELP) {
          int underlying = keyboard_help_previous_interactive();
          if (underlying >= 0 && underlying != UI_SCREEN_HELP && g_screens[underlying].render)
            layers[layer_count++] = g_screens[underlying];
          else {
            for (int i = UI_SCREEN_SPLASH; i >= UI_SCREEN_STATUS; --i)
              if (g_screens[i].render) {
                layers[layer_count++] = g_screens[i];
                break;
              }
          }
        }
        if (parent >= 0 && parent != active && g_screens[parent].render)
          layers[layer_count++] = g_screens[parent];
        sync_backdrop = keyboard_help_previous_interactive() == UI_SCREEN_SYNC && layer_count == 0;
      }
      // The active frame already has its own lifetime reference.
      for (size_t i = 0; i < layer_count; ++i) {
        atomic_fetch_add(&layers[i].owned->references, 1);
      }
      g_rendering_layers = 1u << active;
      for (int i = 0; i < UI_SCREEN_COUNT; ++i)
        for (size_t j = 0; j < layer_count; ++j)
          if (g_screens[i].owned == layers[j].owned)
            g_rendering_layers |= 1u << i;
      mutex_unlock(&g_mutex);
      layers[layer_count++] = frame;
      if ((resized || changed) && (active == UI_SCREEN_MEDIA || overlay)) {
        // Keep the last usable media dimensions while a small-terminal view is shown.
        if (GET_OPTION(auto_width) && size.cols >= UI_MEDIA_MIN_COLS && size.cols <= 1024)
          options_set_int("width", size.cols);
        if (GET_OPTION(auto_height) && size.rows >= UI_MEDIA_MIN_ROWS && size.rows <= 1024)
          options_set_int("height", size.rows);
      }
      ui_session_resize(g_session, size.cols, size.rows);
      if (redraw)
        ui_session_redraw(g_session);
      if (active == UI_SCREEN_MEDIA) {
        // Console geometry queries can block while capture continues. Take the
        // latest mailbox snapshot after those queries instead of drawing the
        // image retained before them.
        screen_snapshot_t *retired = NULL;
        mutex_lock(&g_mutex);
        if (g_screens[active].owned && g_screens[active].owned != frame.owned) {
          retired = frame.owned;
          frame = g_screens[active];
          atomic_fetch_add(&frame.owned->references, 1);
          g_screens[active].dirty = false;
          g_render_snapshot = frame.owned;
        }
        mutex_unlock(&g_mutex);
        release_snapshot(retired);
      }
      layers[layer_count - 1] = frame;
      native_view_t context = {.screen = screen,
                               .layers = layers,
                               .layer_count = layer_count,
                               .active = active,
                               .volume = volume_visible,
                               .errors = errors,
                               .sync_backdrop = sync_backdrop};
      bool help_changed = false;
      if (active == UI_SCREEN_HELP) {
        uint64_t revision = options_revision();
        help_changed = revision != help_revision;
        help_revision = revision;
      }
      ui_session_view_t view = {
          .screen = active,
          .minimum = errors && active != UI_SCREEN_HELP ? (terminal_size_t){1, 1} : screen->minimum,
          .dirty = screen->dirty || overlay || volume_changed || help_changed,
          .refresh_ns = active == UI_SCREEN_PROMPT   ? 50 * NS_PER_MS_INT
                        : active == UI_SCREEN_LOGS   ? 100 * NS_PER_MS_INT
                        : active == UI_SCREEN_UPDATE ? (update_banner_is_checking() ? 100 * NS_PER_MS_INT : 0)
                        : active == UI_SCREEN_RENDER_PROGRESS
                            ? 125 * NS_PER_MS_INT
                            : (active != UI_SCREEN_MEDIA && active != UI_SCREEN_STATS && active != UI_SCREEN_HELP
                                   ? 16 * NS_PER_MS_INT
                                   : 0),
          .draw = draw_native_view,
          .context = &context};
      uint64_t compose_started_ns = time_get_ns();
      const char *output = ui_session_render(g_session, &view, now);
      uint64_t animation_deadline = ui_session_next_frame_ns(g_session);
      if (active == UI_SCREEN_HELP && animation_deadline < refresh_deadline)
        refresh_deadline = animation_deadline;
      uint64_t composed_ns = time_get_ns();
      bool covered = ui_session_is_covered(g_session);
      atomic_store_bool(&g_blocked, covered);
      if (covered && !was_covered)
        fps_counter_reset(g_fps);
      if (output) {
        stats_media_begin(context.fresh && context.drew);
        g_probe_frame = context.fresh && context.drew && reply_support >= 0;
        asciichat_error_t written = ui_controller_write(screen->fd, output, ui_session_frame_length(g_session));
        uint64_t write_done_ns = time_get_ns();
        if (context.fresh && context.drew) {
          fps_counter_write_begin(g_fps);
          // A frontend reply drains the complete update. A local console query
          // adds an RPC without establishing any stronger completion boundary.
          asciichat_error_t drained = g_probe_requested_ns ? ASCIICHAT_OK : terminal_flush(screen->fd);
          fps_counter_write_end(g_fps, drained == ASCIICHAT_OK);
          if (written != ASCIICHAT_OK || drained != ASCIICHAT_OK)
            CLEAR_ERRNO();
          else {
            uint64_t completed_ns = time_get_ns();
            log_debug(
                "FRAME_PRESENT: id=%llu submit_to_stdout_ms=%.2f acquired_ns=%llu submitted_ns=%llu "
                "completed_ns=%llu capture_to_stdout_ms=%.2f mailbox_ms=%.2f geometry_ms=%.2f "
                "compose_ms=%.2f write_ms=%.2f drain_ms=%.2f max_chunk_ms=%.2f chunks=%llu bytes=%zu cols=%d rows=%d",
                (unsigned long long)screen->owned->frame_id,
                (double)(completed_ns - screen->owned->submitted_ns) / NS_PER_MS_INT,
                (unsigned long long)screen->owned->acquisition_started_ns,
                (unsigned long long)screen->owned->submitted_ns, (unsigned long long)completed_ns,
                screen->owned->acquisition_started_ns
                    ? (double)(completed_ns - screen->owned->acquisition_started_ns) / NS_PER_MS_INT
                    : 0,
                (double)(compose_started_ns - screen->owned->submitted_ns) / NS_PER_MS_INT,
                (double)(geometry_done_ns - iteration_started_ns) / NS_PER_MS_INT,
                (double)(composed_ns - compose_started_ns) / NS_PER_MS_INT,
                (double)(write_done_ns - composed_ns) / NS_PER_MS_INT,
                (double)(completed_ns - write_done_ns) / NS_PER_MS_INT, (double)g_stats_max_chunk_ns / NS_PER_MS_INT,
                (unsigned long long)g_stats_chunks, ui_session_frame_length(g_session), size.cols, size.rows);
          }
        }
        stats_media_end();
        if (g_probe_frame) {
          awaiting_reply = written == ASCIICHAT_OK && g_probe_requested_ns != 0;
          if (awaiting_reply) {
            reply_requested_ns = g_probe_requested_ns;
            reply_frame_ns = screen->owned->acquisition_started_ns;
            reply_frame_id = screen->owned->frame_id;
            reply_written_ns = time_get_ns();
          }
        }
        if (context.drew && !awaiting_reply)
          fps_counter_frame_end(g_fps, time_get_ns());
      }
      if (!covered && !context.drew)
        fps_counter_render(g_fps, screen->fd, size.cols, context.drew);
    }
  frame_complete:
    g_render_fd = -1;
    g_render_snapshot = NULL;
    for (size_t i = 0; i + 1 < layer_count; ++i)
      release_snapshot(layers[i].owned);
    if (frame.render) {
      release_snapshot(frame.owned);
      mutex_lock(&g_mutex);
      g_rendering_screen = -1;
      g_rendering_layers = 0;
      g_render_completed = g_render_started;
      bool live = false;
      for (int i = 0; i < UI_SCREEN_COUNT; ++i)
        live |= g_screens[i].render != NULL;
      atomic_store_bool(&g_live, live);
      if (!live)
        errno_notifications_enable(false);
      cond_broadcast(&g_render_done);
      mutex_unlock(&g_mutex);
    }
    // New snapshots wake presentation immediately. The deadline only refreshes
    // animations, terminal geometry and idle FPS counters; rendering time is
    // part of the interval rather than an additional delay after each frame.
    mutex_lock(&g_mutex);
    int pending_screen = selected_screen_locked();
    bool pending = g_redraw || pending_screen != g_active || (pending_screen >= 0 && g_screens[pending_screen].dirty) ||
                   (ui_error_popup_visible() && g_screens[UI_SCREEN_MEDIA].dirty);
    uint64_t now = time_get_ns();
    if (!pending && !atomic_load_bool(&g_presentation_stop_requested) && now < refresh_deadline)
      cond_timedwait(&g_render_requested, &g_mutex, refresh_deadline - now);
    mutex_unlock(&g_mutex);
  }
  usage_transition(NULL, &stats_page, &stats_since);
  if (covered_screen)
    ascii_otel_ui_usage(covered_screen, "size_warning_ended", time_get_ns() - covered_since);
  usage_transition(NULL, &usage_screen, &usage_since);
  if (g_title_saved) {
    asciichat_errno_scope_t scope = errno_scope_begin();
    (void)terminal_title_restore(g_title_fd);
    (void)platform_close(g_title_fd);
    g_title_fd = -1;
    g_title_saved = false;
    atomic_store_bool(&g_title_active, false);
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  }
  fps_counter_destroy(g_fps);
  g_fps = NULL;
  g_owner = false;
  return NULL;
}

static asciichat_error_t controller_start(void) {
  ui_output_set_writer(ui_controller_write);
  if (lifecycle_init_once(&g_lifecycle)) {
    atomic_store_bool(&g_presentation_stop_requested, false);
    atomic_store_bool(&g_blocked, false);
    g_active = -1;
    g_redraw = false;
    g_finished = false;
    g_rendering_screen = -1;
    g_render_started = g_render_completed = 0;
    if (mutex_init(&g_mutex, "ui_controller") != 0) {
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize UI mutex");
    }
    if (mutex_init(&g_title_packet_mutex, "terminal_title_packets") != 0) {
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize terminal title packet mutex");
    }
    if (cond_init(&g_render_done, "ui_render_done") != 0) {
      mutex_destroy(&g_title_packet_mutex);
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize UI render completion condition");
    }
    if (cond_init(&g_render_requested, "ui_render_requested") != 0) {
      cond_destroy(&g_render_done);
      mutex_destroy(&g_title_packet_mutex);
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize UI presentation condition");
    }
    g_session = ui_session_create();
    if (!g_session) {
      cond_destroy(&g_render_requested);
      cond_destroy(&g_render_done);
      mutex_destroy(&g_title_packet_mutex);
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_MEMORY, "Cannot create session UI runtime");
    }
    NAMED_REGISTER_ATOMIC(&g_title_active, "terminal_title_active", NULL);
    NAMED_REGISTER_ATOMIC(&g_title_render, "terminal_title_render", NULL);
    NAMED_REGISTER_ATOMIC(&g_title_ready, "terminal_title_ready", NULL);
    NAMED_REGISTER_ATOMIC(&g_title_activity, "terminal_title_activity", NULL);
    NAMED_REGISTER_ATOMIC(&g_live, "ui_controller_live", NULL);
    NAMED_REGISTER_ATOMIC(&g_presentation_stop_requested, "ui_presentation_stop_requested", NULL);
    NAMED_REGISTER_ATOMIC(&g_blocked, "ui_controller_blocked", NULL);
    NAMED_REGISTER_ATOMIC(&g_lifecycle.state, "ui_controller_lifecycle", NULL);
    if (acthread_create(&g_thread, "ui_controller", presentation_main, NULL) != ASCIICHAT_OK) {
      ui_session_destroy(g_session);
      g_session = NULL;
      NAMED_UNREGISTER(&g_title_active);
      NAMED_UNREGISTER(&g_title_render);
      NAMED_UNREGISTER(&g_title_ready);
      NAMED_UNREGISTER(&g_title_activity);
      NAMED_UNREGISTER(&g_live);
      NAMED_UNREGISTER(&g_presentation_stop_requested);
      NAMED_UNREGISTER(&g_blocked);
      NAMED_UNREGISTER(&g_lifecycle.state);
      cond_destroy(&g_render_requested);
      cond_destroy(&g_render_done);
      mutex_destroy(&g_title_packet_mutex);
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot start UI controller");
    }
    lifecycle_init_commit(&g_lifecycle);
  }
  if (!lifecycle_is_initialized(&g_lifecycle))
    return SET_ERRNO(ERROR_INVALID_STATE, "UI controller initialization is not complete");
  return ASCIICHAT_OK;
}

void ui_controller_packet_begin(void) {
  if (lifecycle_is_initialized(&g_lifecycle))
    mutex_lock(&g_title_packet_mutex);
}

void ui_controller_packet_end(void) {
  if (lifecycle_is_initialized(&g_lifecycle))
    mutex_unlock(&g_title_packet_mutex);
}

asciichat_error_t ui_controller_start_terminal(int fd) {
  if (fd < 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI terminal descriptor");
  atomic_store(&g_default_fd, fd);
  asciichat_error_t result = controller_start();
  return result == ASCIICHAT_OK ? ui_sync_start(fd) : result;
}

asciichat_error_t ui_controller_title_start(const char *mode, int fd) {
  if (!mode || !platform_isatty(fd) ||
      (GET_OPTION(snapshot_mode) && !GET_OPTION(render_file)[0] && !GET_OPTION(render_kitty)))
    return ASCIICHAT_OK;
  if (atomic_load_bool(&g_title_active))
    return SET_ERRNO(ERROR_INVALID_STATE, "Terminal title is already initialized");
  atomic_store(&g_default_fd, fd);
  asciichat_error_t result = controller_start();
  if (result != ASCIICHAT_OK)
    return result;
  g_title_fd = platform_dup(fd);
  if (g_title_fd < 0)
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Cannot retain title terminal");
  g_title_mode = mode;
  g_title_started = time_get_ns();
  g_last_title[0] = '\0';
  g_title_written = false;
  g_title_startup_finished = false;
  atomic_store_int(&g_title_render, UI_TITLE_IDLE);
  atomic_store_int(&g_title_activity, UI_TITLE_ACTIVITY_IDLE);
  atomic_store_bool(&g_title_ready, false);
  // Publish only after the retained descriptor and immutable configuration are ready.
  atomic_store_bool(&g_title_active, true);
  return ASCIICHAT_OK;
}

bool ui_controller_screen_live(ui_screen_t screen) {
  return screen >= 0 && screen < UI_SCREEN_COUNT && (atomic_load(&g_screen_mask) & (1u << screen));
}

static asciichat_error_t submit_screen(ui_screen_t screen, int fd, terminal_size_t minimum, ui_render_fn render,
                                       const void *snapshot, size_t bytes, screen_terminal_t *retained_terminal,
                                       bool fresh_frame) {
  uint64_t entered_ns = time_get_ns();
  if (screen < 0 || screen >= UI_SCREEN_COUNT || !render || !snapshot || !bytes || minimum.cols <= 0 ||
      minimum.rows <= 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI screen snapshot");
  if (interactive_screen(screen) && screen != UI_SCREEN_ERRNO && screen != UI_SCREEN_HELP &&
      !(screen == UI_SCREEN_HELP && keyboard_help_parent_screen() == UI_SCREEN_ERRNO) &&
      (screen != UI_SCREEN_STATS || ui_controller_interactive_screen() == UI_SCREEN_STATS))
    ui_errno_stacks_hide();
  // A live slot retains its terminal even while LOG_IO redirects process stdio.
  bool live = lifecycle_is_initialized(&g_lifecycle) && (atomic_load(&g_screen_mask) & (1u << screen));
  // New modal slots inherit the live terminal instead of process stdio, which
  // an audio/decoder worker can temporarily redirect through LOG_IO.
  if (!live && !retained_terminal && lifecycle_is_initialized(&g_lifecycle) &&
      (screen == UI_SCREEN_PROMPT || screen == UI_SCREEN_RENDER_PROGRESS || screen == UI_SCREEN_NOTICE)) {
    screen_terminal_t *inherited = NULL;
    mutex_lock(&g_mutex);
    int active = selected_screen_locked();
    if (active >= 0) {
      inherited = g_screens[active].owned->terminal;
      atomic_fetch_add(&inherited->references, 1);
    }
    mutex_unlock(&g_mutex);
    if (inherited) {
      asciichat_error_t result =
          submit_screen(screen, inherited->fd, minimum, render, snapshot, bytes, inherited, fresh_frame);
      release_terminal(inherited);
      return result;
    }
  }
  // Batch output and finite snapshots retain their synchronous output semantics.
  if ((!live && !retained_terminal && !platform_isatty(fd)) ||
      (GET_OPTION(snapshot_mode) && screen != UI_SCREEN_RENDER_PROGRESS)) {
    bool previous_owner = g_owner;
    int previous_fd = g_render_fd;
    terminal_size_t detected = {0};
    // Redirected output has no terminal geometry. Avoid raising a terminal
    // error for every frame when rendering to a file, pipe, or null device.
    if (!platform_isatty(fd) || terminal_get_size_fd(fd, &detected) != ASCIICHAT_OK || detected.cols <= 0 ||
        detected.rows <= 0)
      detected = (terminal_size_t){.cols = GET_OPTION(width), .rows = GET_OPTION(height)};
    g_render_size = detected;
    g_owner = true;
    g_render_fd = fd;
    frame_buffer_set_screen_output_fd(fd);
    stats_media_begin(screen == UI_SCREEN_MEDIA);
    render(g_render_size, snapshot);
    stats_media_end();
    g_owner = previous_owner;
    g_render_fd = previous_fd;
    return ASCIICHAT_OK;
  }
  asciichat_error_t err = controller_start();
  if (err != ASCIICHAT_OK)
    return err;
  size_t payload = render == render_text ? ((const text_snapshot_t *)snapshot)->len : 0;
  if (bytes > SIZE_MAX - sizeof(screen_snapshot_t) || payload > SIZE_MAX - sizeof(screen_snapshot_t) - bytes)
    return SET_ERRNO(ERROR_MEMORY, "UI snapshot is too large");
  screen_snapshot_t *copy = allocate_snapshot(screen, sizeof(*copy) + bytes + payload);
  if (!copy)
    return SET_ERRNO(ERROR_MEMORY, "Cannot copy UI snapshot");
  memcpy(copy->data, snapshot, bytes);
  if (payload) {
    text_snapshot_t *text = (text_snapshot_t *)copy->data;
    text->text = copy->data + bytes;
    memcpy((char *)text->text, ((const text_snapshot_t *)snapshot)->text, payload);
  }
  uint64_t prepared_ns = time_get_ns();
  mutex_lock(&g_mutex);
  uint64_t locked_ns = time_get_ns();
  if (g_finished) {
    mutex_unlock(&g_mutex);
    release_snapshot(copy);
    return ASCIICHAT_OK;
  }
  copy->frame_id = screen == UI_SCREEN_MEDIA && fresh_frame ? ++g_media_frame_id : 0;
  copy->submitted_ns = time_get_ns();
  copy->acquisition_started_ns = screen == UI_SCREEN_MEDIA ? g_acquisition_started_ns : 0;
  screen_t *slot = &g_screens[screen];
  bool queue_notice = screen == UI_SCREEN_NOTICE && slot->render;
  if (slot->render && !queue_notice) {
    // The terminal belongs to the live slot, not to each incoming image.
    // Duplicating/closing a Windows console handle per frame introduces console
    // RPCs on producers and can serialize them behind ongoing presentation.
    copy->terminal = slot->owned->terminal;
    atomic_fetch_add(&copy->terminal->references, 1);
  } else if (retained_terminal) {
    copy->terminal = retained_terminal;
    atomic_fetch_add(&copy->terminal->references, 1);
  } else {
    int retained_fd = platform_dup(fd);
    if (retained_fd < 0) {
      mutex_unlock(&g_mutex);
      release_snapshot(copy);
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot retain UI output descriptor");
    }
    copy->terminal = SAFE_MALLOC(sizeof(*copy->terminal), screen_terminal_t *);
    if (!copy->terminal) {
      platform_close(retained_fd);
      mutex_unlock(&g_mutex);
      release_snapshot(copy);
      return SET_ERRNO(ERROR_MEMORY, "Cannot retain UI terminal ownership");
    }
    atomic_init(&copy->terminal->references, 1);
    copy->terminal->fd = retained_fd;
  }
  int output_fd = copy->terminal->fd;
  if (queue_notice) {
    size_t pending = 1;
    screen_t *tail = slot;
    while (tail->next) {
      tail = tail->next;
      ++pending;
    }
    if (pending >= 64) {
      release_snapshot(copy);
      mutex_unlock(&g_mutex);
      return SET_ERRNO(ERROR_BUFFER_FULL, "UI notice queue is full");
    }
    screen_t *queued = SAFE_CALLOC(1, sizeof(*queued), screen_t *);
    if (!queued) {
      release_snapshot(copy);
      mutex_unlock(&g_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate queued UI notice");
    }
    *queued = (screen_t){
        .owned = copy, .snapshot = copy->data, .render = render, .minimum = minimum, .fd = output_fd, .dirty = true};
    tail->next = queued;
    mutex_unlock(&g_mutex);
    return ASCIICHAT_OK;
  }
  screen_snapshot_t *retired = slot->owned;
  atomic_store_bool(&g_live, true);
  errno_notifications_enable(true);
  *slot = (screen_t){
      .owned = copy, .snapshot = copy->data, .render = render, .minimum = minimum, .fd = output_fd, .dirty = true};
  if (interactive_screen(screen) && screen != UI_SCREEN_STATS && (screen != UI_SCREEN_MDNS || !live))
    ui_controller_select_interactive(screen);
  publish_screen_locked();
  mutex_unlock(&g_mutex);
  uint64_t published_ns = time_get_ns();
  release_snapshot(retired);
  uint64_t retired_ns = time_get_ns();
  asciichat_error_t sync_result = ui_sync_start(output_fd);
  uint64_t finished_ns = time_get_ns();
  if (screen == UI_SCREEN_MEDIA && finished_ns - entered_ns > 50 * NS_PER_MS_INT)
    log_debug_every(
        US_PER_SEC_INT, "UI_SUBMIT_STAGE: build_ms=%.2f lock_ms=%.2f publish_ms=%.2f retire_ms=%.2f sync_ms=%.2f",
        (double)(prepared_ns - entered_ns) / NS_PER_MS_INT, (double)(locked_ns - prepared_ns) / NS_PER_MS_INT,
        (double)(published_ns - locked_ns) / NS_PER_MS_INT, (double)(retired_ns - published_ns) / NS_PER_MS_INT,
        (double)(finished_ns - retired_ns) / NS_PER_MS_INT);
  return sync_result;
}

asciichat_error_t ui_controller_submit(ui_screen_t screen, int fd, terminal_size_t minimum, ui_render_fn render,
                                       const void *snapshot, size_t bytes) {
  return submit_screen(screen, fd, minimum, render, snapshot, bytes, NULL, true);
}

void ui_controller_remove(ui_screen_t screen) {
  if (screen < 0 || screen >= UI_SCREEN_COUNT || !lifecycle_is_initialized(&g_lifecycle))
    return;
  mutex_lock(&g_mutex);
  // An old callback must not remove a replacement submitted while it rendered.
  if (g_render_snapshot && g_rendering_screen == (int)screen && g_screens[screen].owned != g_render_snapshot) {
    mutex_unlock(&g_mutex);
    return;
  }
  screen_snapshot_t *retired = g_screens[screen].owned;
  int expected = screen;
  atomic_compare_exchange_strong(&g_interactive_screen, &expected, -1);
  uint64_t pending = (g_rendering_layers & (1u << screen)) ? g_render_started : 0;
  screen_t *next = g_screens[screen].next;
  if (next) {
    g_screens[screen] = *next;
    SAFE_FREE(next);
    g_redraw = true;
  } else {
    memset(&g_screens[screen], 0, sizeof(screen_t));
  }
  publish_screen_locked();
  bool live = g_rendering_screen >= 0;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    live |= g_screens[i].render != NULL;
  atomic_store_bool(&g_live, live);
  if (!live)
    errno_notifications_enable(false);
  // Borrowed pointers inside snapshots may be freed as soon as remove returns.
  // A callback removing itself retains its owned bytes until the frame ends.
  while (!g_render_snapshot && g_render_completed < pending)
    cond_wait(&g_render_done, &g_mutex);
  mutex_unlock(&g_mutex);
  release_snapshot(retired);
}

ui_presentation_state_t ui_controller_state(void) {
  ui_presentation_state_t state = {.screen = -1};
  if (!lifecycle_is_initialized(&g_lifecycle))
    return state;
  state.screen = ui_controller_current_screen();
  if (state.screen == UI_SCREEN_ERROR) {
    state.covered = ui_too_small(ui_controller_size(), ui_error_popup_minimum());
    return state;
  }
  if (state.screen >= 0 && state.screen < UI_SCREEN_COUNT) {
    uint64_t published = atomic_load(&g_input_state);
    // Sync has an independent mailbox instead of a controller slot. Treat an
    // unpublished slot as absent so the session can dispatch queued switches.
    if (!(published & (UINT64_C(1) << state.screen)))
      state.screen = -1;
    else
      state.covered = (published & (UINT64_C(1) << (state.screen + 32))) != 0;
  }
  return state;
}

// Bound REP expansion to one terminal row before allocating its expanded text.
static char *expand_media_row(const char *row, size_t length, int columns) {
  size_t repeats = 0;
  for (const char *p = row, *end = row + length; p < end;) {
    if (*p != '\033') {
      ++p;
      continue;
    }
    const char *next = ansi_skip_escape(p, end);
    if (next <= p)
      return NULL;
    if (next - p >= 3 && p[1] == '[' && next[-1] == 'b') {
      size_t count = 0;
      for (const char *digit = p + 2; digit < next - 1; ++digit) {
        if (*digit < '0' || *digit > '9' || count > (size_t)columns / 10)
          return NULL;
        count = count * 10 + (size_t)(*digit - '0');
        if (count > (size_t)columns)
          return NULL;
      }
      if (!count || count > (size_t)columns - repeats)
        return NULL;
      repeats += count;
    }
    p = next;
  }
  return ansi_expand_rle(row, length);
}

static void render_text(terminal_size_t size, const void *data) {
  const text_snapshot_t *text = data;
  if (text->media && !GET_OPTION(snapshot_mode) && GET_OPTION(fps_counter) && size.cols >= 7 &&
      text->dimensions.cols <= size.cols && text->dimensions.rows <= size.rows) {
    const char *newline = memchr(text->text, '\n', text->len);
    size_t first_bytes = newline ? (size_t)(newline - text->text) : text->len;
    // Expand REP before clipping, so a repeated glyph cannot cross the counter.
    char *first = expand_media_row(text->text, first_bytes, size.cols);
    if (first) {
      size_t prefix = utf8_prefix_bytes_for_width(first, strlen(first), size.cols - 7);
      ui_controller_write(text->fd, first, prefix);
      SAFE_FREE(first);
    } else {
      ui_controller_printf(text->fd, "\033[1;1H\033[%dX", size.cols - 7);
    }
    if (newline) {
      ui_controller_write(text->fd, "\033[0m\033[2;1H", 10);
      ui_controller_write(text->fd, newline + 1, text->len - first_bytes - 1);
    }
    if (g_render_volume)
      append_volume(g_capture, size);
    return;
  }
  if (!text->media || GET_OPTION(snapshot_mode) ||
      (text->dimensions.cols <= size.cols && text->dimensions.rows <= size.rows) ||
      !platform_isatty(g_render_fd >= 0 ? g_render_fd : text->fd)) {
    if (text->media && g_render_volume) {
      frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
      if (buffer) {
        frame_buffer_append(buffer, text->text, text->len);
        append_volume(buffer, size);
        ui_controller_write(text->fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
        frame_buffer_destroy(buffer);
      } else {
        ui_controller_write(text->fd, text->text, text->len);
      }
    } else {
      ui_controller_write(text->fd, text->text, text->len);
    }
    return;
  }
  // A paused frame can outlive its original dimensions. Clip its rows instead
  // of letting old-sized content wrap and scroll after a terminal resize.
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  char *line = SAFE_MALLOC(text->len + 1, char *);
  char *clipped = SAFE_MALLOC(text->len + 32, char *);
  if (buffer && line && clipped) {
    const char *cursor = text->text;
    const char *end = cursor + text->len;
    for (int row = 1; row <= size.rows && cursor < end; ++row) {
      const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
      size_t length = (size_t)((newline ? newline : end) - cursor);
      memcpy(line, cursor, length);
      line[length] = '\0';
      bool reserved_counter = row == 1 && GET_OPTION(fps_counter) && size.cols >= 7;
      char *expanded = expand_media_row(line, length, text->dimensions.cols);
      truncate_with_ellipsis(expanded ? expanded : "", clipped, text->len + 32, size.cols - (reserved_counter ? 7 : 1));
      SAFE_FREE(expanded);
      frame_buffer_printf(buffer, reserved_counter ? "\033[%d;1H" : "\033[%d;1H\033[2K", row);
      frame_buffer_append(buffer, clipped, strlen(clipped));
      cursor += length + (newline ? 1 : 0);
    }
    if (g_render_volume)
      append_volume(buffer, size);
    ui_controller_write(text->fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  }
  if (buffer)
    frame_buffer_destroy(buffer);
  SAFE_FREE(line);
  SAFE_FREE(clipped);
}

void ui_controller_redraw(void) {
  if (!lifecycle_is_initialized(&g_lifecycle))
    return;
  mutex_lock(&g_mutex);
  g_redraw = true;
  cond_signal(&g_render_requested);
  mutex_unlock(&g_mutex);
}

static asciichat_error_t present_text(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data, size_t len,
                                      bool fresh) {
  uint64_t entered_ns = time_get_ns();
  if (!data || !len || len > UI_CONTROLLER_MAX_FRAME_BYTES)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI frame");
  text_snapshot_t text = {.fd = fd,
                          .media = screen == UI_SCREEN_MEDIA,
                          .dimensions = {.cols = GET_OPTION(width), .rows = GET_OPTION(height)},
                          .len = len,
                          .text = data};
  uint64_t prepared_ns = time_get_ns();
  asciichat_error_t err = submit_screen(screen, fd, minimum, render_text, &text, sizeof(text), NULL, fresh);
  uint64_t submitted_ns = time_get_ns();
  uint64_t finished_ns = time_get_ns();
  if (screen == UI_SCREEN_MEDIA && finished_ns - entered_ns > 50 * NS_PER_MS_INT)
    log_debug_every(US_PER_SEC_INT, "UI_TEXT_STAGE: prepare_ms=%.2f submit_ms=%.2f free_ms=%.2f",
                    (double)(prepared_ns - entered_ns) / NS_PER_MS_INT,
                    (double)(submitted_ns - prepared_ns) / NS_PER_MS_INT,
                    (double)(finished_ns - submitted_ns) / NS_PER_MS_INT);
  return err;
}

void ui_controller_shutdown(void) {
  if (!lifecycle_destroy_once(&g_lifecycle))
    return;
  mutex_lock(&g_mutex);
  atomic_store_bool(&g_presentation_stop_requested, true);
  cond_signal(&g_render_requested);
  mutex_unlock(&g_mutex);
  thread_join(&g_thread, NULL);
  ui_session_destroy(g_session);
  g_session = NULL;
  errno_notifications_enable(false);
  ui_error_popup_reset();
  atomic_store_u64(&g_volume_until, 0);
  ui_sync_stop();
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    release_screen(&g_screens[i], i == UI_SCREEN_NOTICE);
  }
  destroy_media_snapshots();
  cond_destroy(&g_render_requested);
  cond_destroy(&g_render_done);
  mutex_destroy(&g_title_packet_mutex);
  mutex_destroy(&g_mutex);
  atomic_store_bool(&g_live, false);
  atomic_store(&g_background_screen, -1);
  atomic_store(&g_screen_mask, 0);
  ui_controller_select_interactive(-1);
  NAMED_UNREGISTER(&g_title_active);
  NAMED_UNREGISTER(&g_title_render);
  NAMED_UNREGISTER(&g_title_ready);
  NAMED_UNREGISTER(&g_title_activity);
  NAMED_UNREGISTER(&g_live);
  NAMED_UNREGISTER(&g_presentation_stop_requested);
  NAMED_UNREGISTER(&g_blocked);
  NAMED_UNREGISTER(&g_lifecycle.state);
  lifecycle_destroy_commit(&g_lifecycle);
}

asciichat_error_t ui_controller_printf(int fd, const char *format, ...) {
  char buffer[16384];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot format terminal output");
  size_t bytes = (size_t)len < sizeof(buffer) ? (size_t)len : sizeof(buffer) - 1;
  return ui_controller_write(fd, buffer, bytes);
}

void ui_controller_restore_terminal(void) {
  ui_controller_shutdown();
  if (platform_isatty(STDOUT_FILENO))
    ui_controller_write(STDOUT_FILENO, "\033[0m\033[?25h", 10);
}

static void render_logs(terminal_size_t size, const void *snapshot) {
  (void)snapshot;
  ui_session_logs_render(g_capture, size, g_session);
}

void ui_controller_hide_logs(void) {
  ui_controller_remove(UI_SCREEN_LOGS);
  terminal_screen_logs_reset();
}

void ui_controller_toggle_logs(void) {
  ui_errno_stacks_hide();
  if (ui_controller_current_screen() == UI_SCREEN_LOGS) {
    ui_controller_hide_logs();
    return;
  }
  if (!platform_isatty(STDOUT_FILENO) || GET_OPTION(snapshot_mode))
    return;
  const bool snapshot = true;
  asciichat_error_t result =
      ui_controller_submit(UI_SCREEN_LOGS, STDOUT_FILENO, (terminal_size_t){.cols = 40, .rows = 6}, render_logs,
                           &snapshot, sizeof(snapshot));
  if (result != ASCIICHAT_OK)
    log_warn("Cannot show application logs: %d", result);
}

asciichat_error_t ui_controller_present(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data,
                                        size_t len) {
  return present_text(screen, fd, minimum, data, len, true);
}
asciichat_error_t ui_controller_present_retained(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data,
                                                 size_t len) {
  return present_text(screen, fd, minimum, data, len, false);
}
