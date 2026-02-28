/**
 * @file platform/linux/terminal.c
 * @ingroup platform
 * @brief Pixel renderer for render-file using Ghostty's full terminal emulation
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pty.h>
#include <ghostty.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/debug/named.h>

struct terminal_renderer_s {
  ghostty_config_t config;
  ghostty_app_t app;
  ghostty_surface_t surface;
  int pty_master, pty_slave;
  uint8_t *framebuffer;
  int width_px, height_px, pitch;
  term_renderer_theme_t theme;
  uint8_t fg_r, fg_g, fg_b; // Theme-aware foreground color
  uint8_t bg_r, bg_g, bg_b; // Theme-aware background color
};

static void wakeup_cb(void *ud) {
  (void)ud;
}
static bool action_cb(ghostty_app_t a, ghostty_target_s t, ghostty_action_s ac) {
  (void)a;
  (void)t;
  (void)ac;
  return false;
}

asciichat_error_t term_renderer_create(const term_renderer_config_t *cfg, terminal_renderer_t **out) {
  // Initialize ghostty global state (thread-safe, one-time only)
  asciichat_error_t init_err = terminal_ghostty_init_once();
  if (init_err != ASCIICHAT_OK) {
    log_error("term_renderer_create: terminal_ghostty_init_once failed");
    return init_err;
  }

  terminal_renderer_t *r = SAFE_CALLOC(1, sizeof(*r), terminal_renderer_t *);
  r->theme = cfg->theme;

  // Initialize theme-aware default colors using platform abstraction
  terminal_get_default_foreground_color(cfg->theme, &r->fg_r, &r->fg_g, &r->fg_b);
  terminal_get_default_background_color(cfg->theme, &r->bg_r, &r->bg_g, &r->bg_b);

  log_info("term_renderer_create: Starting ghostty renderer setup");

  if (openpty(&r->pty_master, &r->pty_slave, NULL, NULL, NULL) != 0) {
    log_error("term_renderer_create: openpty failed");
    SAFE_FREE(r);
    return SET_ERRNO_SYS(ERROR_INIT, "openpty");
  }
  log_info("term_renderer_create: PTY created master=%d slave=%d", r->pty_master, r->pty_slave);

  log_info("term_renderer_create: About to call ghostty_config_new - r=%p", (void *)r);
  r->config = ghostty_config_new();
  log_info("term_renderer_create: ghostty_config_new returned r->config=%p", (void *)r->config);
  if (!r->config) {
    log_error("term_renderer_create: ghostty_config_new returned NULL");
    close(r->pty_master);
    close(r->pty_slave);
    SAFE_FREE(r);
    return SET_ERRNO(ERROR_INIT, "ghostty_config_new failed");
  }

  // Write a transient config snippet to set the font
  char tmp[64] = "/tmp/ascii-chat-render-XXXXXX.conf";
  int tfd = mkstemp(tmp);
  if (tfd >= 0) {
    const char *fname = cfg->font_spec[0] ? cfg->font_spec : "monospace";
    dprintf(tfd, "font-family = %s\nfont-size = %.4g\n", fname, cfg->font_size_pt);
    close(tfd);
    log_info("term_renderer_create: Loading config from %s", tmp);
    ghostty_config_load_file(r->config, tmp);
    unlink(tmp);
  }
  log_info("term_renderer_create: Finalizing ghostty config");
  ghostty_config_finalize(r->config);

  log_info("term_renderer_create: Creating ghostty app");
  ghostty_runtime_config_s rt = {
      .userdata = r,
      .supports_selection_clipboard = false,
      .wakeup_cb = wakeup_cb,
      .action_cb = action_cb,
  };
  r->app = ghostty_app_new(&rt, r->config);
  if (!r->app) {
    log_error("term_renderer_create: ghostty_app_new returned NULL");
    ghostty_config_free(r->config);
    close(r->pty_master);
    close(r->pty_slave);
    SAFE_FREE(r);
    return SET_ERRNO(ERROR_INIT, "ghostty_app_new failed");
  }
  log_info("term_renderer_create: Ghostty app created");

  // Estimate pixel dimensions; corrected below after surface is realized
  r->width_px = (int)(cfg->cols * cfg->font_size_pt);
  r->height_px = (int)(cfg->rows * cfg->font_size_pt * 2.0);
  r->pitch = r->width_px * 3;

  log_info("term_renderer_create: Creating ghostty surface config");
  ghostty_surface_config_s sc = ghostty_surface_config_new();
  sc.platform_tag = GHOSTTY_PLATFORM_INVALID; // Offscreen rendering
  sc.font_size = (float)cfg->font_size_pt;
  sc.userdata = r;
  sc.scale_factor = 1.0; // 1x scale for offscreen rendering

  log_info("term_renderer_create: Creating ghostty surface");
  r->surface = ghostty_surface_new(r->app, &sc);
  if (!r->surface) {
    log_error("term_renderer_create: ghostty_surface_new returned NULL");
    ghostty_app_free(r->app);
    ghostty_config_free(r->config);
    close(r->pty_master);
    close(r->pty_slave);
    SAFE_FREE(r);
    return SET_ERRNO(ERROR_INIT, "ghostty_surface_new failed");
  }
  log_info("term_renderer_create: Ghostty surface created, setting size");

  ghostty_surface_set_size(r->surface, (uint32_t)cfg->cols, (uint32_t)cfg->rows);
  ghostty_surface_set_focus(r->surface, true);

  // Correct pixel dimensions from ghostty's realized cell metrics
  ghostty_surface_size_s sz = ghostty_surface_size(r->surface);
  r->width_px = (int)sz.width_px;
  r->height_px = (int)sz.height_px;
  r->pitch = r->width_px * 3;

  log_info("term_renderer_create: Final dimensions: %dx%d pixels, pitch=%d", r->width_px, r->height_px, r->pitch);

  r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);
  log_info("term_renderer_create: Framebuffer allocated: %zu bytes", (size_t)r->pitch * r->height_px);

  *out = r;

  /* Register terminal renderer with named registry */
  NAMED_REGISTER(r, "renderer", "terminal", "0x%tx");

  return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r, const char *ansi_frame, size_t len) {
  log_info("term_renderer_feed: Writing %zu bytes to PTY", len);

  // Deliver ANSI bytes through the PTY master — ghostty reads from the slave
  const char *cur = ansi_frame;
  size_t rem = len;
  while (rem > 0) {
    ssize_t n = write(r->pty_master, cur, rem);
    if (n < 0)
      return SET_ERRNO_SYS(ERROR_GENERAL, "write pty_master");
    cur += n;
    rem -= (size_t)n;
  }
  log_info("term_renderer_feed: PTY write complete");

  // Allow ghostty's event loop to consume the PTY bytes and render
  log_info("term_renderer_feed: Ticking ghostty app");
  usleep(16000);
  ghostty_app_tick(r->app);
  log_info("term_renderer_feed: Drawing ghostty surface");
  ghostty_surface_draw(r->surface);

  // Extract rendered pixels from ghostty surface
  log_info("term_renderer_feed: Getting pixels from ghostty");
  ghostty_pixel_data_t pixels = ghostty_surface_get_pixels(r->surface);
  if (pixels.pixels && pixels.width > 0 && pixels.height > 0) {
    log_info("term_renderer_feed: Got pixels: %ux%u pitch=%u", pixels.width, pixels.height, pixels.pitch);
    // Convert ghostty's BGRA pixels to RGB for framebuffer
    for (uint32_t y = 0; y < pixels.height && y < (uint32_t)r->height_px; y++) {
      for (uint32_t x = 0; x < pixels.width && x < (uint32_t)r->width_px; x++) {
        // Source: BGRA from ghostty
        const uint8_t *src = (const uint8_t *)pixels.pixels + y * pixels.pitch + x * 4;
        // Destination: RGB in our framebuffer
        uint8_t *dst = r->framebuffer + y * r->pitch + x * 3;
        dst[0] = src[2]; // R ← src[2]
        dst[1] = src[1]; // G ← src[1]
        dst[2] = src[0]; // B ← src[0]
      }
    }
    ghostty_free_pixels(&pixels);
  } else {
    log_warn("term_renderer_feed: ghostty_surface_get_pixels returned no pixels, using fallback");
    // Fallback: fill with theme background color if ghostty pixel API not implemented
    for (int py = 0; py < r->height_px; py++) {
      for (int px = 0; px < r->width_px; px++) {
        uint8_t *dst = r->framebuffer + py * r->pitch + px * 3;
        dst[0] = r->bg_r;
        dst[1] = r->bg_g;
        dst[2] = r->bg_b;
      }
    }
  }

  return ASCIICHAT_OK;
}

const uint8_t *term_renderer_pixels(terminal_renderer_t *r) {
  return r->framebuffer;
}
int term_renderer_width_px(terminal_renderer_t *r) {
  return r->width_px;
}
int term_renderer_height_px(terminal_renderer_t *r) {
  return r->height_px;
}
int term_renderer_pitch(terminal_renderer_t *r) {
  return r->pitch;
}

void term_renderer_destroy(terminal_renderer_t *r) {
  if (!r)
    return;
  NAMED_UNREGISTER(r);
  close(r->pty_master);
  close(r->pty_slave);
  ghostty_surface_free(r->surface);
  ghostty_app_free(r->app);
  ghostty_config_free(r->config);
  SAFE_FREE(r->framebuffer);
  SAFE_FREE(r);
}

/* ============================================================================
 * Ghostty Initialization (Offscreen Rendering)
 * ============================================================================ */

#include <stdatomic.h>

static lifecycle_t g_ghostty_lc = LIFECYCLE_INIT;

asciichat_error_t terminal_ghostty_init_once(void) {
  if (!lifecycle_init_once(&g_ghostty_lc)) {
    // Already initialized or in progress, wait for completion
    return ASCIICHAT_OK;
  }

  // We won the init race, do the work
  int argc = 1;
  char *argv[] = {"ascii-chat", NULL};

  int ret = ghostty_init((uintptr_t)argc, (char **)argv);
  if (ret != 0) {
    lifecycle_init_abort(&g_ghostty_lc);
    log_error("terminal_ghostty_init_once: ghostty_init failed with code %d", ret);
    return SET_ERRNO(ERROR_INIT, "ghostty_init failed with code %d", ret);
  }

  log_info("terminal_ghostty_init_once: ghostty global state initialized successfully");
  lifecycle_init_commit(&g_ghostty_lc);
  return ASCIICHAT_OK;
}
#endif
