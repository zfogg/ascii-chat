#include <ascii-chat/ui/sync.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#ifndef NDEBUG

typedef struct {
  uintptr_t key, owner, waiter;
  uint64_t generation, value, locks, unlocks, changes, last_ns;
  double lock_rate, unlock_rate, change_rate;
  char name[256], type[24], file[160];
  int line;
  unsigned waiters;
  bool cycle;
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
static _Atomic int g_sync_page;
static _Atomic int g_sync_selected;
static _Atomic bool g_sync_visible;
static _Atomic bool g_sync_stop;
static atomic_flag g_sync_starting = ATOMIC_FLAG_INIT;
static bool g_sync_started;
static asciichat_thread_t g_sync_thread;
static int g_sync_fd = -1;

static void sync_copy_row(uintptr_t key, uint64_t generation, const char *name, const char *type, const char *file,
                          int line, void *data) {
  sync_sample_t *sample = data;
  if (!type || (strcmp(type, "mutex") && strcmp(type, "rwlock") && strcmp(type, "cond") && strcmp(type, "atomic") &&
                strcmp(type, "atomic_ptr")))
    return;
  sync_row_t *r = &sample->rows[sample->count++];
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
  } else if (!strcmp(type, "atomic")) {
    const atomic_t *m = (const atomic_t *)key;
    r->value = atomic_load(&m->impl);
    r->changes = m->change_count;
    r->last_ns = m->last_store_time_ns;
  } else {
    const atomic_ptr_t *m = (const atomic_ptr_t *)key;
    r->value = (uintptr_t)atomic_load(&m->impl);
    r->changes = m->change_count;
    r->last_ns = m->last_store_time_ns;
  }
}

static int sync_compare_rows(const void *a, const void *b) {
  const sync_row_t *x = a, *y = b;
  if (x->cycle != y->cycle)
    return x->cycle ? -1 : 1;
  int result = strcmp(x->type, y->type);
  return result ? result : strcmp(x->name, y->name);
}

static double sync_rate(uint64_t current, uint64_t previous, double seconds) {
  return current >= previous && seconds > 0 ? (double)(current - previous) / seconds : 0;
}

static void *sync_collect_main(void *unused) {
  (void)unused;
  uint64_t next_sample = 0;
  while (!atomic_load(&g_sync_stop)) {
    ui_input_poll_sync();
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
      result = asciichat_thread_create(&g_sync_thread, "sync_collector", sync_collect_main, NULL);
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
  asciichat_thread_join(&g_sync_thread, NULL);
  keyboard_destroy();
  platform_close(g_sync_fd);
  g_sync_fd = -1;
  for (int i = 0; i < 3; ++i) {
    UNTRACKED_FREE(g_sync_samples[i].rows);
    g_sync_samples[i].capacity = g_sync_samples[i].count = 0;
  }
  atomic_store(&g_sync_published, -1);
  atomic_store(&g_sync_visible, false);
  g_sync_started = false;
}

bool ui_sync_is_visible(void) {
  return atomic_load(&g_sync_visible);
}
int ui_sync_output_fd(void) {
  return g_sync_fd;
}

keyboard_key_t ui_sync_handle_key(keyboard_key_t key) {
  if (key == KEY_0) {
    atomic_store(&g_sync_visible, !atomic_load(&g_sync_visible));
    return KEY_NONE;
  }
  if (!ui_sync_is_visible())
    return key;
  if (key == KEY_ESCAPE || key == KEY_QUESTION) {
    atomic_store(&g_sync_visible, false);
    return key == KEY_QUESTION && ui_controller_current_screen() != UI_SCREEN_HELP ? key : KEY_NONE;
  }
  if (key == KEY_RIGHT)
    atomic_fetch_add(&g_sync_page, 1);
  if (key == KEY_LEFT && atomic_load(&g_sync_page) > 0)
    atomic_fetch_sub(&g_sync_page, 1);
  if (key == KEY_DOWN)
    atomic_fetch_add(&g_sync_selected, 1);
  if (key == KEY_UP && atomic_load(&g_sync_selected) > 0)
    atomic_fetch_sub(&g_sync_selected, 1);
  if (key == KEY_HOME)
    atomic_store(&g_sync_page, 0);
  if (key == KEY_END)
    atomic_store(&g_sync_page, INT_MAX);
  return key == 'q' || key == 3 ? key : KEY_NONE;
}

static const char *sync_rate_color(double rate) {
  return rate >= 60 ? "\033[31m" : rate >= 10 ? "\033[33m" : rate > 0 ? "\033[32m" : "\033[34m";
}

static void sync_write_line(const char *text, int columns, bool newline) {
  char output[2048];
  size_t used = 0;
  int visible = 0;
  const char *prefix = "\033[2K";
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
      if (visible >= columns - 1)
        break;
      output[used++] = text[i++];
      ++visible;
    }
  }
  const char *suffix = newline ? "\033[0m\r\n" : "\033[0m";
  memcpy(output + used, suffix, strlen(suffix));
  used += strlen(suffix);
  ui_controller_write(g_sync_fd, output, used);
}

void ui_sync_render(terminal_size_t size) {
  if (size.rows < 7 || size.cols < 30) {
    ui_controller_write(g_sync_fd, "\033[H\033[J", 6);
    sync_write_line("Sync: resize (0 closes)", size.cols, false);
    return;
  }
  // Copy only the visible page before terminal writes; the collector never
  // waits for output, and the renderer never waits for collection.
  sync_row_t rows[128];
  size_t count = 0, total = 0, cycles = 0;
  uint64_t sampled = 0;
  bool waits_complete = false, limited = false;
  int per_page = size.rows > 6 ? size.rows - 6 : 1;
  if (per_page > 128)
    per_page = 128;
  int page = atomic_load(&g_sync_page), pages = 1;
  int slot = atomic_load(&g_sync_published);
  if (slot >= 0 && !atomic_flag_test_and_set(&g_sync_samples[slot].busy)) {
    sync_sample_t *s = &g_sync_samples[slot];
    total = s->count;
    pages = total ? (int)((total + per_page - 1) / per_page) : 1;
    if (page >= pages)
      page = pages - 1;
    atomic_store(&g_sync_page, page);
    size_t first = (size_t)page * per_page;
    count = total > first ? total - first : 0;
    if (count > (size_t)per_page)
      count = per_page;
    if (count)
      memcpy(rows, s->rows + first, count * sizeof(*rows));
    for (size_t i = 0; i < total; ++i)
      cycles += s->rows[i].cycle;
    sampled = s->sampled_ns;
    waits_complete = s->waits_complete;
    limited = s->limited;
    atomic_flag_clear(&s->busy);
  }
  char line[1024];
  ui_controller_write(g_sync_fd, "\033[H", 3);
  uint64_t now = time_get_ns();
  snprintf(line, sizeof(line), "Sync primitives | %zu objects | page %d/%d | sample age %.1fs", total, page + 1, pages,
           sampled ? (double)(now - sampled) / NS_PER_SEC_INT : 0.0);
  sync_write_line(line, size.cols, true);
  snprintf(line, sizeof(line), "%s%s | %zu mutexes in wait cycles%s", cycles ? "\033[31m" : "",
           !sampled                         ? "Waiting for registry"
           : now - sampled > NS_PER_SEC_INT ? "STALE: collection unavailable"
                                            : "Live",
           cycles,
           !waits_complete ? " | wait tracker busy"
           : limited       ? " | wait tracker capacity reached"
                           : "");
  sync_write_line(line, size.cols, true);
  const char *heading = " Type     Name                 State/value          L/s    U/s Change/s";
  sync_write_line(heading, size.cols, true);
  int selected = atomic_load(&g_sync_selected);
  if ((size_t)selected >= count)
    selected = count ? (int)count - 1 : 0;
  atomic_store(&g_sync_selected, selected);
  for (size_t i = 0; i < count; ++i) {
    sync_row_t *r = &rows[i];
    // Parent names may be media paths; keep the primitive visible in the table.
    // The detail row identifies the source location and address.
    const char *name = strrchr(r->name, '#');
    name = name ? name + 1 : r->name;
    char state[128];
    if (!strcmp(r->type, "mutex"))
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
    snprintf(line, sizeof(line), "%c%-8.8s %-20.20s %-18.18s %s%6.1f %s%6.1f %s%7.1f\033[0m%s",
             (int)i == selected ? '>' : ' ', r->type, name, state, sync_rate_color(r->lock_rate), r->lock_rate,
             sync_rate_color(r->unlock_rate), r->unlock_rate, sync_rate_color(r->change_rate), r->change_rate,
             r->cycle ? " DEADLOCK" : "");
    sync_write_line(line, size.cols, true);
  }
  snprintf(line, sizeof(line), "\033[J\033[%d;1H", size.rows > 2 ? size.rows - 2 : 1);
  ui_controller_write(g_sync_fd, line, strlen(line));
  if (count) {
    sync_row_t *r = &rows[selected];
    snprintf(line, sizeof(line), "%s:%d @0x%" PRIxPTR, r->file, r->line, r->key);
    sync_write_line(line, size.cols, true);
    snprintf(line, sizeof(line), "owner=0x%" PRIxPTR " waiter=0x%" PRIxPTR " | value=%" PRIu64
                                " | last operation %.2fs ago%s", r->owner, r->waiter, r->value,
             r->last_ns ? (double)(now - r->last_ns) / NS_PER_SEC_INT : 0.0, r->cycle ? " DEADLOCK" : "");
    sync_write_line(line, size.cols, true);
  }
  snprintf(line, sizeof(line), "\033[%d;1H", size.rows);
  ui_controller_write(g_sync_fd, line, strlen(line));
  sync_write_line("0/Esc close | ? help | Left/Right page | Up/Down detail | Home/End", size.cols, false);
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
