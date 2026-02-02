/**
 * @file platform/linux/keepawake.c
 * @brief Linux system sleep prevention using systemd-inhibit
 * @ingroup platform
 */

#include <ascii-chat/platform/system.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <unistd.h>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-bus.h>
static int g_inhibit_fd = -1;

// Weak symbols for runtime systemd detection
extern int sd_bus_default_system(sd_bus **ret) __attribute__((weak));
#endif

asciichat_error_t platform_enable_keepawake(void) {
#ifdef HAVE_LIBSYSTEMD
  // Linux: Use systemd-inhibit if available
  if (g_inhibit_fd >= 0) {
    log_debug("Keepawake already enabled");
    return ASCIICHAT_OK;
  }

  // Check if systemd is available at runtime (weak symbol)
  if (sd_bus_default_system == NULL) {
    log_debug("systemd not available, keepawake not supported");
    return ASCIICHAT_OK; // Not an error, just unsupported
  }

  sd_bus *bus = NULL;
  sd_bus_message *reply = NULL;
  sd_bus_error error = SD_BUS_ERROR_NULL;

  if (sd_bus_default_system(&bus) < 0) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to connect to system bus");
  }

  if (!bus) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "System bus is NULL");
  }

  int r = sd_bus_call_method(bus, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                             "Inhibit", &error, &reply, "ssss",
                             "sleep:idle",            // What to inhibit
                             "ascii-chat",            // Who
                             "Video/audio streaming", // Why
                             "block"                  // Mode
  );

  if (r < 0) {
    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to inhibit sleep via systemd");
  }

  if (!reply) {
    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "systemd inhibit reply is NULL");
  }

  if (sd_bus_message_read(reply, "h", &g_inhibit_fd) < 0) {
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to read inhibit fd from systemd reply");
  }

  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  sd_bus_unref(bus);

  log_debug("Keepawake enabled via systemd-inhibit (fd: %d)", g_inhibit_fd);
  return ASCIICHAT_OK;

#else
  // Other POSIX systems: not implemented
  log_debug("Keepawake not implemented on this platform");
  return ASCIICHAT_OK;
#endif
}

void platform_disable_keepawake(void) {
#ifdef HAVE_LIBSYSTEMD
  if (g_inhibit_fd >= 0) {
    close(g_inhibit_fd);
    log_debug("Keepawake disabled (closed inhibit fd: %d)", g_inhibit_fd);
    g_inhibit_fd = -1;
  }
#endif
}
