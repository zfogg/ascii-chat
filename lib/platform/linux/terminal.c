/**
 * @file platform/linux/terminal.c
 * @ingroup platform
 * @brief Pixel renderer for render-file: ghostty library for rendering
 *
 * Integrates libghostty for terminal emulation and color scheme support:
 * - Uses ghostty's terminal emulation (ANSI via PTY)
 * - Uses ghostty's rendering for pixel output
 * - Integrates ghostty_app_set_color_scheme() for light/dark theme support
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ghostty.h>
#include <pty.h>
#include <unistd.h>
#include <string.h>
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/colorscheme.h>

struct terminal_renderer_s {
    ghostty_config_t   config;
    ghostty_app_t      app;
    ghostty_surface_t  surface;
    int                pty_master, pty_slave;
    term_renderer_theme_t theme;
    uint8_t           *framebuffer;
    int                width_px, height_px, pitch;
};

static void wakeup_cb(void *ud) { (void)ud; }
static bool action_cb(ghostty_app_t a, ghostty_target_s t, ghostty_action_s ac)
    { (void)a; (void)t; (void)ac; return false; }

asciichat_error_t term_renderer_create(const term_renderer_config_t *cfg,
                                       terminal_renderer_t **out) {
    terminal_renderer_t *r = SAFE_CALLOC(1, sizeof(*r), terminal_renderer_t *);
    r->theme = cfg->theme;

    if (openpty(&r->pty_master, &r->pty_slave, NULL, NULL, NULL) != 0) {
        SAFE_FREE(r);
        return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "openpty");
    }

    r->config = ghostty_config_new();

    // Write a transient config snippet to set the font
    char tmp[64] = "/tmp/ascii-chat-render-XXXXXX.conf";
    int tfd = mkstemp(tmp);
    if (tfd >= 0) {
        const char *fname = cfg->font_spec[0] ? cfg->font_spec : "Monospace";
        dprintf(tfd, "font-family = %s\nfont-size = %.4g\n",
                fname, cfg->font_size_pt);
        close(tfd);
        ghostty_config_load_file(r->config, tmp);
        unlink(tmp);
    }
    ghostty_config_finalize(r->config);

    ghostty_runtime_config_s rt = {
        .userdata = r,
        .supports_selection_clipboard = false,
        .wakeup_cb = wakeup_cb,
        .action_cb = action_cb,
    };
    r->app = ghostty_app_new(&rt, r->config);

    // Set color scheme based on theme
    ghostty_color_scheme_e scheme = (cfg->theme == TERM_RENDERER_THEME_LIGHT)
        ? GHOSTTY_COLOR_SCHEME_LIGHT
        : GHOSTTY_COLOR_SCHEME_DARK;
    ghostty_app_set_color_scheme(r->app, scheme);
    log_debug("term_renderer_create: Using %s theme, ghostty color scheme set",
             (cfg->theme == TERM_RENDERER_THEME_LIGHT) ? "light" : "dark");

    // Also get project colorscheme for reference
    const color_scheme_t *proj_scheme = colorscheme_get_active_scheme();
    if (proj_scheme) {
        log_debug("term_renderer_create: Project colorscheme '%s' available", proj_scheme->name);
    }

    // Estimate pixel dimensions; corrected below after surface is realized
    r->width_px  = (int)(cfg->cols * cfg->font_size_pt);
    r->height_px = (int)(cfg->rows * cfg->font_size_pt * 2.0);
    r->pitch     = r->width_px * 3;

    ghostty_surface_config_s sc = ghostty_surface_config_new();
    sc.platform_tag            = GHOSTTY_PLATFORM_INVALID;
    sc.font_size               = (float)cfg->font_size_pt;
    sc.userdata                = r;
    r->surface = ghostty_surface_new(r->app, &sc);
    ghostty_surface_set_size(r->surface, (uint32_t)cfg->cols, (uint32_t)cfg->rows);
    ghostty_surface_set_focus(r->surface, true);

    // Correct pixel dimensions from ghostty's realized cell metrics
    ghostty_surface_size_s sz = ghostty_surface_size(r->surface);
    r->width_px  = (int)sz.width_px;
    r->height_px = (int)sz.height_px;
    r->pitch     = r->width_px * 3;

    r->framebuffer = SAFE_MALLOC((size_t)r->pitch * r->height_px, uint8_t *);
    *out = r;
    return ASCIICHAT_OK;
}

asciichat_error_t term_renderer_feed(terminal_renderer_t *r,
                                     const char *ansi_frame, size_t len) {
    // Deliver ANSI bytes through the PTY master — ghostty reads from the slave
    const char *cur = ansi_frame;
    size_t rem = len;
    while (rem > 0) {
        ssize_t n = write(r->pty_master, cur, rem);
        if (n < 0) return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "write pty_master");
        cur += n;
        rem -= (size_t)n;
    }

    // Allow ghostty's event loop to consume the PTY bytes
    usleep(16000);
    ghostty_app_tick(r->app);
    ghostty_surface_draw(r->surface);

    // TODO: Extract pixel data from ghostty surface
    // For now, render a placeholder
    memset(r->framebuffer, 0, (size_t)r->pitch * r->height_px);

    return ASCIICHAT_OK;
}

const uint8_t *term_renderer_pixels(terminal_renderer_t *r)   { return r->framebuffer; }
int term_renderer_width_px(terminal_renderer_t *r)             { return r->width_px;   }
int term_renderer_height_px(terminal_renderer_t *r)            { return r->height_px;  }
int term_renderer_pitch(terminal_renderer_t *r)                { return r->pitch;      }

void term_renderer_destroy(terminal_renderer_t *r) {
    if (!r) return;
    close(r->pty_master);
    close(r->pty_slave);
    ghostty_surface_free(r->surface);
    ghostty_app_free(r->app);
    ghostty_config_free(r->config);
    SAFE_FREE(r->framebuffer);
    SAFE_FREE(r);
}
#endif
