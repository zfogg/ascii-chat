#ifndef _WIN32

#include "platform.h"
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>

// Thread implementation
int ascii_thread_create(asciithread_t *thread, void *(*func)(void *), void *arg) {
  return pthread_create(&thread->thread, NULL, func, arg);
}

int ascii_thread_join(asciithread_t *thread, void **retval) {
  return pthread_join(thread->thread, retval);
}

void ascii_thread_exit(void *retval) {
  pthread_exit(retval);
}

int ascii_thread_detach(asciithread_t *thread) {
  return pthread_detach(thread->thread);
}

thread_id_t ascii_thread_self(void) {
  thread_id_t id;
  id.thread = pthread_self();
  return id;
}

int ascii_thread_equal(thread_id_t t1, thread_id_t t2) {
  return pthread_equal(t1.thread, t2.thread);
}

uint64_t ascii_thread_current_id(void) {
  return (uint64_t)pthread_self();
}

// Mutex implementation
int mutex_init(mutex_t *mutex) {
  return pthread_mutex_init(&mutex->mutex, NULL);
}

int mutex_destroy(mutex_t *mutex) {
  return pthread_mutex_destroy(&mutex->mutex);
}

int mutex_lock(mutex_t *mutex) {
  return pthread_mutex_lock(&mutex->mutex);
}

int mutex_trylock(mutex_t *mutex) {
  return pthread_mutex_trylock(&mutex->mutex);
}

int mutex_unlock(mutex_t *mutex) {
  return pthread_mutex_unlock(&mutex->mutex);
}

// Read-write lock implementation
int rwlock_init(rwlock_t *lock) {
  return pthread_rwlock_init(&lock->lock, NULL);
}

int rwlock_destroy(rwlock_t *lock) {
  return pthread_rwlock_destroy(&lock->lock);
}

int rwlock_rdlock(rwlock_t *lock) {
  return pthread_rwlock_rdlock(&lock->lock);
}

int rwlock_wrlock(rwlock_t *lock) {
  return pthread_rwlock_wrlock(&lock->lock);
}

int rwlock_unlock(rwlock_t *lock) {
  return pthread_rwlock_unlock(&lock->lock);
}

int rwlock_rdunlock(rwlock_t *lock) {
  return pthread_rwlock_unlock(&lock->lock);
}

int rwlock_wrunlock(rwlock_t *lock) {
  return pthread_rwlock_unlock(&lock->lock);
}

// Condition variable implementation
int cond_init(cond_t *cond) {
  return pthread_cond_init(&cond->cond, NULL);
}

int cond_destroy(cond_t *cond) {
  return pthread_cond_destroy(&cond->cond);
}

int cond_wait(cond_t *cond, mutex_t *mutex) {
  return pthread_cond_wait(&cond->cond, &mutex->mutex);
}

int cond_timedwait(cond_t *cond, mutex_t *mutex, int timeout_ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += timeout_ms / 1000;
  ts.tv_nsec += (timeout_ms % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }
  return pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
}

int cond_signal(cond_t *cond) {
  return pthread_cond_signal(&cond->cond);
}

int cond_broadcast(cond_t *cond) {
  return pthread_cond_broadcast(&cond->cond);
}

// Terminal I/O implementation
int terminal_get_size(terminal_size_t *size) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    size->rows = ws.ws_row;
    size->cols = ws.ws_col;
    return 0;
  }
  return -1;
}

const char *get_tty_path(void) {
  return "/dev/tty";
}

// Environment variable handling
const char *get_username_env(void) {
  static char username[256];
  const char *user = getenv("USER");
  if (!user) {
    user = getenv("USERNAME");
  }
  if (user) {
    strncpy(username, user, sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';
    return username;
  }
  return "unknown";
}

// Platform initialization
int platform_init(void) {
  // POSIX platforms don't need special initialization
  return 0;
}

void platform_cleanup(void) {
  // POSIX platforms don't need special cleanup
}

// Static mutex functions are implemented as inline in platform_init.h

// Sleep functions
void platform_sleep_ms(unsigned int ms) {
  usleep(ms * 1000);
}

void platform_sleep_us(unsigned int us) {
  usleep(us);
}

// Process functions
int platform_get_pid(void) {
  return (int)getpid();
}

const char *platform_get_username(void) {
  return get_username_env();
}

// Signal handling
signal_handler_t platform_signal(int sig, signal_handler_t handler) {
  return signal(sig, handler);
}

// Terminal functions
int terminal_set_raw_mode(bool enable) {
  static struct termios orig_termios;
  static bool saved = false;

  if (enable) {
    if (!saved) {
      tcgetattr(STDIN_FILENO, &orig_termios);
      saved = true;
    }
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  } else if (saved) {
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  }
  return 0;
}

int terminal_set_echo(bool enable) {
  struct termios tty;
  if (tcgetattr(STDIN_FILENO, &tty) != 0)
    return -1;

  if (enable) {
    tty.c_lflag |= ECHO;
  } else {
    tty.c_lflag &= ~ECHO;
  }

  return tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

bool terminal_supports_color(void) {
  const char *term = getenv("TERM");
  if (!term)
    return false;

  // Check for common color-capable terminals
  return (strstr(term, "color") != NULL || strstr(term, "xterm") != NULL || strstr(term, "screen") != NULL ||
          strstr(term, "vt100") != NULL || strstr(term, "linux") != NULL);
}

bool terminal_supports_unicode(void) {
  const char *lang = getenv("LANG");
  const char *lc_all = getenv("LC_ALL");
  const char *lc_ctype = getenv("LC_CTYPE");

  const char *check = lc_all ? lc_all : (lc_ctype ? lc_ctype : lang);
  if (!check)
    return false;

  return (strstr(check, "UTF-8") != NULL || strstr(check, "utf8") != NULL);
}

int terminal_clear_screen(void) {
  return system("clear");
}

int terminal_move_cursor(int row, int col) {
  printf("\033[%d;%dH", row + 1, col + 1);
  fflush(stdout);
  return 0;
}

void terminal_enable_ansi(void) {
  // POSIX terminals typically support ANSI by default
  // No special enabling needed
}

// Environment functions
const char *platform_getenv(const char *name) {
  return getenv(name);
}

int platform_setenv(const char *name, const char *value) {
  return setenv(name, value, 1);
}

// TTY functions
int platform_isatty(int fd) {
  return isatty(fd);
}

const char *platform_get_tty_path(void) {
  return get_tty_path();
}

int platform_open_tty(const char *mode) {
  int flags = O_RDWR;
  if (strchr(mode, 'r') && !strchr(mode, 'w')) {
    flags = O_RDONLY;
  } else if (strchr(mode, 'w') && !strchr(mode, 'r')) {
    flags = O_WRONLY;
  }
  return open("/dev/tty", flags);
}

#endif // !_WIN32