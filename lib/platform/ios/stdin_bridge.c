#include <ascii-chat/platform/ios/stdin_bridge.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/mutex.h>
#include <string.h>

#define IOS_STDIN_BRIDGE_CAPACITY 16384

static unsigned char g_stdin_buffer[IOS_STDIN_BRIDGE_CAPACITY];
static size_t g_stdin_read;
static size_t g_stdin_write;
static size_t g_stdin_size;
static mutex_t g_stdin_mutex;
static cond_t g_stdin_cond;
static bool g_stdin_initialized;

static void ios_stdin_bridge_init(void) {
  if (g_stdin_initialized)
    return;
  mutex_init(&g_stdin_mutex, "ios_stdin_bridge");
  cond_init(&g_stdin_cond, "ios_stdin_bridge");
  g_stdin_initialized = true;
}

ssize_t ios_stdin_bridge_push(const void *data, size_t count) {
  if (!data || count == 0)
    return 0;
  ios_stdin_bridge_init();
  mutex_lock(&g_stdin_mutex);
  size_t accepted = count < IOS_STDIN_BRIDGE_CAPACITY - g_stdin_size ? count : IOS_STDIN_BRIDGE_CAPACITY - g_stdin_size;
  size_t first =
      accepted < IOS_STDIN_BRIDGE_CAPACITY - g_stdin_write ? accepted : IOS_STDIN_BRIDGE_CAPACITY - g_stdin_write;
  memcpy(g_stdin_buffer + g_stdin_write, data, first);
  memcpy(g_stdin_buffer, (const unsigned char *)data + first, accepted - first);
  g_stdin_write = (g_stdin_write + accepted) % IOS_STDIN_BRIDGE_CAPACITY;
  g_stdin_size += accepted;
  cond_broadcast(&g_stdin_cond);
  mutex_unlock(&g_stdin_mutex);
  return (ssize_t)accepted;
}

ssize_t ios_stdin_bridge_read(void *buffer, size_t count) {
  if (!buffer || count == 0)
    return 0;
  ios_stdin_bridge_init();
  mutex_lock(&g_stdin_mutex);
  while (g_stdin_size == 0)
    cond_wait(&g_stdin_cond, &g_stdin_mutex);
  size_t copied = count < g_stdin_size ? count : g_stdin_size;
  size_t first = copied < IOS_STDIN_BRIDGE_CAPACITY - g_stdin_read ? copied : IOS_STDIN_BRIDGE_CAPACITY - g_stdin_read;
  memcpy(buffer, g_stdin_buffer + g_stdin_read, first);
  memcpy((unsigned char *)buffer + first, g_stdin_buffer, copied - first);
  g_stdin_read = (g_stdin_read + copied) % IOS_STDIN_BRIDGE_CAPACITY;
  g_stdin_size -= copied;
  mutex_unlock(&g_stdin_mutex);
  return (ssize_t)copied;
}

size_t ios_stdin_bridge_available(void) {
  if (!g_stdin_initialized)
    return 0;
  mutex_lock(&g_stdin_mutex);
  size_t available = g_stdin_size;
  mutex_unlock(&g_stdin_mutex);
  return available;
}

void ios_stdin_bridge_reset(void) {
  if (!g_stdin_initialized)
    return;
  mutex_lock(&g_stdin_mutex);
  g_stdin_read = g_stdin_write = g_stdin_size = 0;
  mutex_unlock(&g_stdin_mutex);
}
