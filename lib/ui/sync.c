#include <ascii-chat/ui/sync.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/too_small.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/options/options.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#ifndef NDEBUG

typedef struct {
  uintptr_t key, owner, waiter;
  uint64_t generation, value, locks, unlocks, changes, actions, last_ns, wait_ns;
  double lock_rate, unlock_rate, change_rate;
  char name[256], type[24], file[160];
  int line;
  unsigned waiters;
  bool cycle, wait_known;
} sync_row_t;

typedef struct {
  atomic_flag busy;
  sync_row_t *rows;
  size_t count, capacity;
  uint64_t sampled_ns;
  bool waits_complete, limited;
} sync_sample_t;

static sync_sample_t g_sync_samples[3] = {
    {.busy = ATOMIC_FLAG_INIT}, {.busy = ATOMIC_FLAG_INIT}, {.busy = ATOMIC_FLAG_INIT}};
static _Atomic int g_sync_published = -1;
static _Atomic int g_sync_selected;
static _Atomic int g_sync_page_size = 1;
static _Atomic int g_sync_total;
static _Atomic bool g_sync_stop;
static atomic_flag g_sync_starting = ATOMIC_FLAG_INIT;
static bool g_sync_started;
static asciichat_thread_t g_sync_thread;
static int g_sync_fd = -1;
static _Thread_local bool g_sync_use_color, g_sync_use_utf8;

static void sync_copy_row(uintptr_t key, uint64_t generation, const char *name, const char *type, const char *file,
                          int line, void *data) {
  sync_sample_t *sample = data;
  if (!type || (strcmp(type, "mutex") && strcmp(type, "rwlock") && strcmp(type, "cond") && strcmp(type, "atomic") &&
                strcmp(type, "atomic_ptr")))
    return;
  // Initializers and their owning resources can both register the same
  // primitive. Show its latest name once, rather than counting aliases.
  sync_row_t *r = NULL;
  for (size_t i = 0; i < sample->count; ++i) {
    if (sample->rows[i].key != key)
      continue;
    if (sample->rows[i].generation >= generation)
      return;
    r = &sample->rows[i];
    break;
  }
  if (!r)
    r = &sample->rows[sample->count++];
  memset(r, 0, sizeof(*r));
  r->key = key;
  r->generation = generation;
  r->line = line;
  snprintf(r->name, sizeof(r->name), "%s", name ? name : "?");
  snprintf(r->type, sizeof(r->type), "%s", type);
  const char *source = file ? file : "?";
  for (const char *p = source; *p; ++p) {
    if (p != source && p[-1] != '/' && p[-1] != '\\')
      continue;
    if (((!strncmp(p, "lib", 3) || !strncmp(p, "src", 3)) && (p[3] == '/' || p[3] == '\\')) ||
        (!strncmp(p, "include", 7) && (p[7] == '/' || p[7] == '\\'))) {
      source = p;
      break;
    }
  }
  snprintf(r->file, sizeof(r->file), "%s", source);
  for (char *p = r->file; *p; ++p)
    if (*p == '\\')
      *p = '/';
  if (!strcmp(type, "mutex")) {
    const mutex_t *m = (const mutex_t *)key;
    r->owner = m->currently_held_by_key;
    r->locks = m->lock_count + m->trylock_success_count;
    r->unlocks = m->unlock_count;
    r->last_ns = m->last_lock_time_ns;
  } else if (!strcmp(type, "rwlock")) {
    const rwlock_t *m = (const rwlock_t *)key;
    r->owner = m->write_held_by_key;
    r->value = atomic_load(&m->read_lock_count.impl);
    r->locks = m->rdlock_count + m->wrlock_count;
    r->unlocks = m->unlock_count;
    r->last_ns = m->last_wrlock_time_ns;
  } else if (!strcmp(type, "cond")) {
    const cond_t *m = (const cond_t *)key;
    r->value = atomic_load(&m->waiting_count.impl);
    r->locks = m->wait_count;
    r->unlocks = m->signal_count + m->broadcast_count;
    r->last_ns = m->last_wait_time_ns;
    r->owner = m->last_waiting_key;
    r->wait_known = cond_total_wait_ns(m, time_get_ns(), &r->wait_ns);
  } else if (!strcmp(type, "atomic")) {
    const atomic_t *m = (const atomic_t *)key;
    r->value = atomic_load(&m->impl);
    r->changes = m->change_count;
    r->actions = atomic_load(&m->load_count) + atomic_load(&m->store_count) +
                 atomic_load(&m->cas_count) + atomic_load(&m->fetch_count);
    r->last_ns = m->last_store_time_ns;
  } else {
    const atomic_ptr_t *m = (const atomic_ptr_t *)key;
    r->value = (uintptr_t)atomic_load(&m->impl);
    r->changes = m->change_count;
    r->actions = atomic_load(&m->load_count) + atomic_load(&m->store_count) +
                 atomic_load(&m->cas_count) + atomic_load(&m->exchange_count);
    r->last_ns = m->last_store_time_ns;
  }
  if (strncmp(type, "atomic", 6))
    r->actions = r->locks + r->unlocks;
}

static const char *sync_short_name(const sync_row_t *row) {
  const char *name = strrchr(row->name, '#');
  return name ? name + 1 : row->name;
}

static void sync_row_label(const sync_row_t *row, char *label, size_t capacity) {
  const char *name = sync_short_name(row);
  if (name != row->name)
    snprintf(label, capacity, "%s.%" PRIu64, name, row->generation);
  else
    snprintf(label, capacity, "%s", name);
}

static int sync_compare_rows(const void *a, const void *b) {
  const sync_row_t *x = a, *y = b;
  if (x->cycle != y->cycle)
    return x->cycle ? -1 : 1;
  double x_activity = x->lock_rate + x->unlock_rate;
  double y_activity = y->lock_rate + y->unlock_rate;
  if (x_activity != y_activity)
    return x_activity > y_activity ? -1 : 1;
  int result = strcmp(x->type, y->type);
  if (!result)
    result = strcmp(sync_short_name(x), sync_short_name(y));
  return result ? result : x->generation < y->generation ? -1 : x->generation > y->generation;
}

static double sync_rate(uint64_t current, uint64_t previous, double seconds) {
  return current >= previous && seconds > 0 ? (double)(current - previous) / seconds : 0;
}

static void *sync_collect_main(void *unused) {
  (void)unused;
  uint64_t next_sample = 0;
  while (!atomic_load(&g_sync_stop)) {
    ui_input_poll_sync();
    // Poll the debug shortcut while hidden, but collect registry and waiter
    // snapshots only for the visible overlay. The pairwise rate comparisons
    // otherwise compete with live capture and input throughout the session.
    if (!ui_sync_is_visible()) {
      next_sample = 0;
      platform_sleep_ns(10 * NS_PER_MS_INT);
      continue;
    }
    uint64_t now = time_get_ns();
    if (now >= next_sample) {
      next_sample = now + 250 * NS_PER_MS_INT;
      int previous = atomic_load(&g_sync_published);
      for (int slot = 0; slot < 3; ++slot) {
        sync_sample_t *sample = &g_sync_samples[slot];
        if (slot == previous || atomic_flag_test_and_set(&sample->busy))
          continue;
        size_t required = 0;
        bool complete = false;
        sample->count = 0;
        asciichat_error_t result =
            named_registry_try_snapshot(sample->capacity, &required, &complete, sync_copy_row, sample);
        if (result == ASCIICHAT_OK && !complete && required > sample->capacity &&
            required <= SIZE_MAX / sizeof(sync_row_t)) {
          // Diagnostic storage must not enter memory tracking or the registry.
          sync_row_t *rows = UNTRACKED_MALLOC(required * sizeof(*rows), sync_row_t *);
          if (rows) {
            UNTRACKED_FREE(sample->rows);
            sample->rows = rows;
            sample->capacity = required;
          }
        }
        if (complete) {
          sample->sampled_ns = now;
          mutex_wait_snapshot_t waits[256];
          size_t wait_count = 0;
          sample->limited = false;
          sample->waits_complete = mutex_stack_try_snapshot(waits, 256, &wait_count, &sample->limited);
          for (size_t i = 0; i < sample->count; ++i) {
            sync_row_t *r = &sample->rows[i];
            for (size_t j = 0; j < wait_count; ++j) {
              if (waits[j].mutex_key == r->key) {
                ++r->waiters;
                r->waiter = waits[j].thread_key;
                r->cycle |= waits[j].cycle;
              }
            }
            if (previous >= 0) {
              const sync_sample_t *old = &g_sync_samples[previous];
              double seconds = (double)(now - old->sampled_ns) / NS_PER_SEC_INT;
              for (size_t j = 0; j < old->count; ++j) {
                const sync_row_t *p = &old->rows[j];
                if (p->generation == r->generation) {
                  r->lock_rate = sync_rate(r->locks, p->locks, seconds);
                  r->unlock_rate = sync_rate(r->unlocks, p->unlocks, seconds);
                  r->change_rate = sync_rate(r->changes, p->changes, seconds);
                  break;
                }
              }
            }
          }
          qsort(sample->rows, sample->count, sizeof(*sample->rows), sync_compare_rows);
          atomic_store(&g_sync_published, slot);
        }
        atomic_flag_clear(&sample->busy);
        break;
      }
    }
    platform_sleep_ns(10 * NS_PER_MS_INT);
  }
  return NULL;
}

asciichat_error_t ui_sync_start(int fd) {
  if (atomic_flag_test_and_set(&g_sync_starting))
    return ASCIICHAT_OK;
  asciichat_error_t result = ASCIICHAT_OK;
  if (!g_sync_started && platform_isatty(fd) && platform_isatty(STDIN_FILENO)) {
    g_sync_fd = platform_dup(fd);
    if (g_sync_fd < 0) {
      result = SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot retain sync display terminal");
    } else if ((result = keyboard_init()) != ASCIICHAT_OK) {
      platform_close(g_sync_fd);
      g_sync_fd = -1;
    } else {
      atomic_store(&g_sync_stop, false);
      result = acthread_create(&g_sync_thread, "sync_collector", sync_collect_main, NULL);
      if (result == ASCIICHAT_OK) {
        g_sync_started = true;
      } else {
        keyboard_destroy();
        platform_close(g_sync_fd);
        g_sync_fd = -1;
        result = SET_ERRNO(ERROR_THREAD, "Cannot start sync collector");
      }
    }
  }
  atomic_flag_clear(&g_sync_starting);
  return result;
}

void ui_sync_stop(void) {
  if (!g_sync_started)
    return;
  atomic_store(&g_sync_stop, true);
  thread_join(&g_sync_thread, NULL);
  keyboard_destroy();
  platform_close(g_sync_fd);
  g_sync_fd = -1;
  for (int i = 0; i < 3; ++i) {
    UNTRACKED_FREE(g_sync_samples[i].rows);
    g_sync_samples[i].capacity = g_sync_samples[i].count = 0;
  }
  atomic_store(&g_sync_published, -1);
  if (ui_sync_is_visible())
    ui_controller_select_interactive(-1);
  g_sync_started = false;
}

bool ui_sync_is_visible(void) {
  return ui_controller_interactive_screen() == UI_SCREEN_SYNC;
}
int ui_sync_output_fd(void) {
  return g_sync_fd;
}

keyboard_key_t ui_sync_handle_key(keyboard_key_t key) {
  if (key == KEY_0) {
    ui_controller_select_interactive(ui_sync_is_visible() ? -1 : UI_SCREEN_SYNC);
    return KEY_NONE;
  }
  if (!ui_sync_is_visible())
    return key;
  // Leave sync selected until the replacement has a complete snapshot.
  if (key == KEY_QUESTION || key == ']' || key == 'o' || key == 'O' || key == 15)
    return key;
  if (key == KEY_ESCAPE) {
    ui_controller_select_interactive(-1);
    return KEY_NONE;
  }
  int selected = atomic_load(&g_sync_selected);
  int last = atomic_load(&g_sync_total) - 1;
  int step = atomic_load(&g_sync_page_size);
  if (key == KEY_RIGHT && last >= 0 && selected / step < last / step)
    selected += step;
  if (key == KEY_LEFT && selected >= step)
    selected -= step;
  if (key == KEY_DOWN)
    ++selected;
  if (key == KEY_UP)
    --selected;
  if (key == KEY_HOME)
    selected = 0;
  if (key == KEY_END)
    selected = last;
  atomic_store(&g_sync_selected, selected < 0 ? 0 : selected > last && last >= 0 ? last : selected);
  return key == 'q' || key == 3 ? key : KEY_NONE;
}

static const char *sync_color(log_color_t color) {
  return g_sync_use_color ? log_level_color(color) : "";
}

static const char *sync_rate_color(double rate) {
  return sync_color(rate >= 60   ? LOG_COLOR_ERROR
                    : rate >= 10 ? LOG_COLOR_WARN
                    : rate > 0   ? LOG_COLOR_INFO
                                 : LOG_COLOR_DEBUG);
}

// Only the presentation worker writes this storage. Diagnostic rendering must
// remain usable even when the allocator or another subsystem mutex is stuck.
static char g_sync_output[64 * 1024];
static size_t g_sync_output_used;

static void sync_flush(void) {
  if (g_sync_output_used) {
    ui_controller_write(g_sync_fd, g_sync_output, g_sync_output_used);
    g_sync_output_used = 0;
  }
}

static void sync_write(const char *text, size_t length) {
  if (length > sizeof(g_sync_output) - g_sync_output_used)
    sync_flush();
  if (length > sizeof(g_sync_output)) {
    ui_controller_write(g_sync_fd, text, length);
    return;
  }
  memcpy(g_sync_output + g_sync_output_used, text, length);
  g_sync_output_used += length;
}

static void sync_write_line(const char *text, int columns, bool newline, int *measured_width) {
  char output[2048];
  size_t used = 0;
  int visible = 0;
  const char *prefix = "\033[0m";
  memcpy(output, prefix, strlen(prefix));
  used = strlen(prefix);
  for (size_t i = 0; text[i] && used + 16 < sizeof(output);) {
    if (text[i] == '\033' && text[i + 1] == '[') {
      output[used++] = text[i++];
      output[used++] = text[i++];
      while (text[i] && !(text[i] >= '@' && text[i] <= '~'))
        output[used++] = text[i++];
      if (text[i])
        output[used++] = text[i++];
    } else {
      if (!measured_width && visible >= columns - 1)
        break;
      int bytes = utf8_next_char_bytes(text + i, strlen(text + i));
      if (bytes <= 0)
        break;
      int width = utf8_display_width_n(text + i, bytes);
      if (!measured_width && visible + width > columns - 1)
        break;
      memcpy(output + used, text + i, bytes);
      used += bytes;
      i += bytes;
      visible += width;
    }
  }
  if (measured_width) {
    if (visible > *measured_width)
      *measured_width = visible;
    return;
  }
  const char *suffix = newline ? "\033[0m\033[K\r\n" : "\033[0m\033[K";
  memcpy(output + used, suffix, strlen(suffix));
  used += strlen(suffix);
  sync_write(output, used);
}

// Header/detail numbers share the log palette, including hexadecimal addresses.
static void sync_color_numbers(const char *text, char *output, size_t capacity) {
  size_t used = 0;
  for (const char *p = text; *p && used + 128 < capacity;) {
    if (p[0] == '\033' && p[1] == '[') {
      output[used++] = *p++;
      output[used++] = *p++;
      while (*p && !(*p >= '@' && *p <= '~'))
        output[used++] = *p++;
      if (*p)
        output[used++] = *p++;
    } else if (*p >= '0' && *p <= '9') {
      char *end;
      double value;
      if (p[0] == '0' && p[1] == 'x')
        value = (double)strtoull(p, &end, 16);
      else
        value = strtod(p, &end);
      used += snprintf(output + used, capacity - used, "%s%.*s%s",
                       sync_color(value == 0 ? LOG_COLOR_ERROR : LOG_COLOR_FATAL), (int)(end - p), p,
                       sync_color(LOG_COLOR_RESET));
      p = end;
    } else {
      output[used++] = *p++;
    }
  }
  output[used] = '\0';
}

static void sync_write_numbers(const char *text, int columns, int *measure) {
  char output[2048];
  sync_color_numbers(text, output, sizeof(output));
  sync_write_line(output, columns, true, measure);
}

static void sync_separator(int columns, int *measure) {
  if (measure)
    return;
  char line[2048];
  size_t used = snprintf(line, sizeof(line), "%s", sync_color(LOG_COLOR_DEBUG));
  const char *cell = g_sync_use_utf8 ? "─" : "-";
  for (int i = 0; i < columns - 1 && used + 8 < sizeof(line); ++i) {
    memcpy(line + used, cell, strlen(cell));
    used += strlen(cell);
  }
  line[used] = '\0';
  sync_write_line(line, columns, true, NULL);
}

void ui_sync_render(terminal_size_t size) {
  terminal_capabilities_t caps = detect_terminal_capabilities();
  terminal_color_mode_t mode = GET_OPTION(color_mode);
  g_sync_use_color = mode != TERM_COLOR_NONE && GET_OPTION(color) != COLOR_SETTING_FALSE && !GET_OPTION(strip_ansi) &&
                     (mode != TERM_COLOR_AUTO || caps.color_level != TERM_COLOR_NONE);
  utf8_setting_t utf8 = GET_OPTION(force_utf8);
  g_sync_use_utf8 = utf8 == UTF8_SETTING_TRUE || (utf8 == UTF8_SETTING_AUTO && caps.utf8_support);
  // Copy only the visible page before terminal writes; the collector never
  // waits for output, and the renderer never waits for collection.
  sync_row_t rows[128];
  size_t count = 0, total = 0, cycles = 0;
  size_t mutexes = 0, conditions = 0, rwlocks = 0, atomics = 0;
  uint64_t sampled = 0;
  bool waits_complete = false, limited = false;
  enum { header_rows = 4, detail_rows = 3, footer_rows = 1 };
  const int fixed_rows = header_rows + detail_rows + footer_rows;
  int name_width = (int)strlen("Name");
  int per_page = size.rows > fixed_rows ? size.rows - fixed_rows : 1;
  if (per_page > 128)
    per_page = 128;
  atomic_store(&g_sync_page_size, per_page);
  int selected = atomic_load(&g_sync_selected);
  int page = 0, pages = 1;
  int slot = atomic_load(&g_sync_published);
  if (slot >= 0) {
    // Keep the last complete screen if collection is reusing this mailbox.
    if (atomic_flag_test_and_set(&g_sync_samples[slot].busy))
      return;
    sync_sample_t *s = &g_sync_samples[slot];
    total = s->count;
    atomic_store(&g_sync_total, total > INT_MAX ? INT_MAX : (int)total);
    if ((size_t)selected >= total) {
      int previous = selected;
      selected = total ? (int)total - 1 : 0;
      // Do not overwrite a key received while the renderer copies a sample.
      atomic_compare_exchange_strong(&g_sync_selected, &previous, selected);
    }
    pages = total ? (int)((total + per_page - 1) / per_page) : 1;
    page = selected / per_page;
    size_t first = (size_t)page * per_page;
    count = total > first ? total - first : 0;
    if (count > (size_t)per_page)
      count = per_page;
    if (count)
      memcpy(rows, s->rows + first, count * sizeof(*rows));
    for (size_t i = 0; i < total; ++i) {
      cycles += s->rows[i].cycle;
      const char *type = s->rows[i].type;
      mutexes += !strcmp(type, "mutex");
      conditions += !strcmp(type, "cond");
      rwlocks += !strcmp(type, "rwlock");
      atomics += !strncmp(type, "atomic", 6);
      char label[288];
      sync_row_label(&s->rows[i], label, sizeof(label));
      int width = (int)strlen(label);
      if (width > name_width)
        name_width = width;
    }
    sampled = s->sampled_ns;
    waits_complete = s->waits_complete;
    limited = s->limited;
    atomic_flag_clear(&s->busy);
  }
  if (name_width > 30)
    name_width = 30;
  uint64_t now = time_get_ns();
  selected %= per_page;
  if ((size_t)selected >= count)
    selected = count ? (int)count - 1 : 0;
  int measured_width = 0;
  for (int pass = 0; pass < 2; ++pass) {
    int *measure = pass == 0 ? &measured_width : NULL;
    char line[1024];
    if (!measure)
      sync_write("\033[?2026h\033[H", 11);
    snprintf(line, sizeof(line), "Sync primitives | %zu objects | page %d/%d | item %zu/%zu | age %.1fs", total,
             page + 1, pages, count ? (size_t)page * per_page + selected + 1 : 0, total,
             sampled ? (double)(now - sampled) / NS_PER_SEC_INT : 0.0);
    sync_write_numbers(line, size.cols, measure);
    snprintf(line, sizeof(line), "%s%s | M=%zu RW=%zu C=%zu A=%zu | %zu mutexes in wait cycles%s",
             cycles ? sync_color(LOG_COLOR_ERROR) : "",
             !sampled                         ? "Waiting for registry"
             : now - sampled > NS_PER_SEC_INT ? "STALE: collection unavailable"
                                              : "Live registered",
             mutexes, rwlocks, conditions, atomics, cycles,
             !waits_complete ? " | wait tracker busy"
             : limited       ? " | wait tracker capacity reached"
                             : "");
    sync_write_numbers(line, size.cols, measure);
    snprintf(line, sizeof(line), "%s %-8s %-*s %-12s  %13s %6s %6s %7s %12s%s", sync_color(LOG_COLOR_GREY), "Type",
             name_width, "Name", "State/value",
             "Total Actions", "L/s", "U/s", "Change/s", "Total Wait", sync_color(LOG_COLOR_RESET));
    sync_write_line(line, size.cols, true, measure);
    sync_separator(size.cols, measure);
    for (size_t i = 0; i < count; ++i) {
      sync_row_t *r = &rows[i];
      // Parent names may be media paths; keep the primitive visible in the table.
      // The detail row identifies the source location and address.
      char name[288];
      sync_row_label(r, name, sizeof(name));
      if (strlen(name) > (size_t)name_width) {
        const char *suffix = strrchr(name, '.');
        size_t suffix_bytes = suffix && strspn(suffix + 1, "0123456789") == strlen(suffix + 1)
                                  ? strlen(suffix) : 0;
        if (suffix_bytes >= (size_t)name_width - 1)
          suffix_bytes = 0;
        size_t used = 0;
        int columns = 0;
        while (name[used]) {
          int bytes = utf8_next_char_bytes(name + used, strlen(name + used));
          if (bytes <= 0)
            break;
          int width = utf8_display_width_n(name + used, bytes);
          if (columns + width > name_width - 1 - (int)suffix_bytes)
            break;
          used += bytes;
          columns += width;
        }
        name[used++] = '~';
        if (suffix_bytes) {
          memmove(name + used, suffix, suffix_bytes);
          used += suffix_bytes;
        }
        name[used] = '\0';
      }
      char state[128];
      if (r->cycle)
        snprintf(state, sizeof(state), "DEADLOCK");
      else if (!strcmp(r->type, "mutex"))
        snprintf(state, sizeof(state), "%s t=%" PRIxPTR " w=%u",
                 r->cycle   ? "CYCLE"
                 : r->owner ? "HELD"
                            : "FREE",
                 r->owner, r->waiters);
      else if (!strcmp(r->type, "rwlock"))
        snprintf(state, sizeof(state), "W=%" PRIxPTR " R=%" PRIu64, r->owner, r->value);
      else if (!strcmp(r->type, "cond"))
        snprintf(state, sizeof(state), "waiting=%" PRIu64, r->value);
      else
        snprintf(state, sizeof(state), "0x%" PRIx64 " (%" PRIu64 ")", r->value, r->value);
      // Prefer the complete hex value over a clipped redundant decimal copy.
      if (!strncmp(r->type, "atomic", 6) && strlen(state) > 12)
        snprintf(state, sizeof(state), "0x%" PRIx64, r->value);
      char actions[32];
      format_number_compact(r->actions, actions, sizeof(actions));
      char changes[64];
      if (!strncmp(r->type, "atomic", 6))
        snprintf(changes, sizeof(changes), "%s%7.1f", sync_rate_color(r->change_rate), r->change_rate);
      else
        snprintf(changes, sizeof(changes), "%s%7s", sync_color(LOG_COLOR_RESET), "-");
      char wait[32] = "-";
      if (!strcmp(r->type, "cond")) {
        if (r->wait_known)
          time_pretty(r->wait_ns, -1, wait, sizeof(wait));
        else
          snprintf(wait, sizeof(wait), "busy");
      }
      snprintf(line, sizeof(line), "%c%-8.8s %-*s %-12.12s  %s%13s %s%6.1f %s%6.1f %s\033[0m %s%12s\033[0m",
               (int)i == selected ? '>' : ' ', r->type, name_width, name, state,
               sync_color(r->actions ? LOG_COLOR_FATAL : LOG_COLOR_ERROR), actions, sync_rate_color(r->lock_rate),
               r->lock_rate, sync_rate_color(r->unlock_rate), r->unlock_rate, changes,
               sync_color(r->wait_ns ? LOG_COLOR_FATAL : LOG_COLOR_RESET), wait);
      sync_write_line(line, size.cols, true, measure);
    }
    if (!measure) {
      // Clear only unused table rows, leaving the live footer in place.
      for (int row = header_rows + (int)count + 1; row < size.rows - 3; ++row) {
        snprintf(line, sizeof(line), "\033[%d;1H\033[K", row);
        sync_write(line, strlen(line));
      }
      snprintf(line, sizeof(line), "\033[%d;1H", size.rows > 3 ? size.rows - 3 : 1);
      sync_write(line, strlen(line));
    }
    sync_separator(size.cols, measure);
    if (count) {
      sync_row_t *r = &rows[selected];
      snprintf(line, sizeof(line), "%d @0x%" PRIxPTR, r->line, r->key);
      char numbers[512];
      sync_color_numbers(line, numbers, sizeof(numbers));
      char full_name[288], colored_name[640];
      sync_row_label(r, full_name, sizeof(full_name));
      sync_color_numbers(full_name, colored_name, sizeof(colored_name));
      snprintf(line, sizeof(line), "%s%s%s:%s | %s", sync_color(LOG_COLOR_DEV), r->file, sync_color(LOG_COLOR_RESET),
               numbers, colored_name);
      sync_write_line(line, size.cols, true, measure);
      snprintf(line, sizeof(line),
               "owner=0x%" PRIxPTR " waiter=0x%" PRIxPTR " | value=%" PRIu64 " | last operation %.2fs ago%s", r->owner,
               r->waiter, r->value, r->last_ns ? (double)(now - r->last_ns) / NS_PER_SEC_INT : 0.0,
               r->cycle ? " DEADLOCK" : "");
      sync_write_numbers(line, size.cols, measure);
    } else if (!measure) {
      snprintf(line, sizeof(line), "\033[%d;1H\033[K\033[%d;1H\033[K", size.rows - 2, size.rows - 1);
      sync_write(line, strlen(line));
    }
    snprintf(line, sizeof(line), "\033[%d;1H", size.rows);
    if (!measure)
      sync_write(line, strlen(line));
    snprintf(line, sizeof(line),
             "%s0%s/%sEsc%s close | %s?%s help | %sLeft%s/%sRight%s page | %sUp%s/%sDown%s select | "
             "%sHome%s/%sEnd%s first/last",
             sync_color(LOG_COLOR_FATAL), sync_color(LOG_COLOR_RESET), sync_color(LOG_COLOR_WARN),
             sync_color(LOG_COLOR_RESET), sync_color(LOG_COLOR_WARN), sync_color(LOG_COLOR_RESET),
             sync_color(LOG_COLOR_WARN), sync_color(LOG_COLOR_RESET), sync_color(LOG_COLOR_WARN),
             sync_color(LOG_COLOR_RESET), sync_color(LOG_COLOR_WARN), sync_color(LOG_COLOR_RESET),
             sync_color(LOG_COLOR_WARN), sync_color(LOG_COLOR_RESET), sync_color(LOG_COLOR_WARN),
             sync_color(LOG_COLOR_RESET), sync_color(LOG_COLOR_WARN), sync_color(LOG_COLOR_RESET));
    sync_write_line(line, size.cols, false, measure);
    if (measure) {
      terminal_size_t minimum = {.cols = measured_width + 1, .rows = fixed_rows + 1};
      if (ui_too_small(size, minimum)) {
        char output[512];
        size_t length = ui_too_small_format(output, sizeof(output), size, minimum);
        ui_controller_write(g_sync_fd, output, length);
        return;
      }
    }
  }
  sync_write("\033[?2026l", 8);
  sync_flush();
}

#else
asciichat_error_t ui_sync_start(int fd) {
  (void)fd;
  return ASCIICHAT_OK;
}
void ui_sync_stop(void) {}
bool ui_sync_is_visible(void) {
  return false;
}
keyboard_key_t ui_sync_handle_key(keyboard_key_t key) {
  return key;
}
void ui_sync_render(terminal_size_t size) {
  (void)size;
}
int ui_sync_output_fd(void) {
  return -1;
}
#endif
