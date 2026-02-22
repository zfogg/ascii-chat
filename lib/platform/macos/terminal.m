/**
 * @file platform/macos/terminal.m
 * @ingroup platform
 * @brief Pixel renderer for render-file: ghostty (libghostty) + Metal offscreen
 */
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#include <pty.h>
#include <ghostty.h>
#include <ascii-chat/video/renderer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/log/logging.h>
#include <unistd.h>

struct terminal_renderer_s {
    ghostty_config_t   config;
    ghostty_app_t      app;
    ghostty_surface_t  surface;
    int                pty_master, pty_slave;
    id<MTLDevice>      metal_device;
    id<MTLTexture>     render_target;
    NSView            *offscreen_view;
    uint8_t           *framebuffer;
    int                width_px, height_px, pitch;
    term_renderer_theme_t theme;  // Terminal theme for color selection
    uint8_t fg_r, fg_g, fg_b;     // Theme-aware foreground color
    uint8_t bg_r, bg_g, bg_b;     // Theme-aware background color
};

static void wakeup_cb(void *ud) { (void)ud; }
static bool action_cb(ghostty_app_t a, ghostty_target_s t, ghostty_action_s ac)
    { (void)a; (void)t; (void)ac; return false; }

asciichat_error_t term_renderer_create(const term_renderer_config_t *cfg,
                                       terminal_renderer_t **out) {
    terminal_renderer_t *r = SAFE_CALLOC(1, sizeof(*r), terminal_renderer_t *);
    r->theme = cfg->theme;

    // Initialize theme-aware default colors using platform abstraction
    terminal_get_default_foreground_color(cfg->theme, &r->fg_r, &r->fg_g, &r->fg_b);
    terminal_get_default_background_color(cfg->theme, &r->bg_r, &r->bg_g, &r->bg_b);

    if (openpty(&r->pty_master, &r->pty_slave, NULL, NULL, NULL) != 0) {
        SAFE_FREE(r);
        return SET_ERRNO_SYS(ERROR_SYSTEM, "openpty");
    }

    r->config = ghostty_config_new();

    // Write a transient config snippet to set the font.
    // ghostty resolves the family name via CoreText internally.
    char tmp[64] = "/tmp/ascii-chat-render-XXXXXX.conf";
    int tfd = mkstemp(tmp);
    if (tfd >= 0) {
        const char *fname = cfg->font_spec[0] ? cfg->font_spec : "SF Mono";
        // ghostty config accepts fractional font sizes (e.g. "font-size = 10.5")
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

    r->metal_device = MTLCreateSystemDefaultDevice();

    // Estimate pixel dimensions; corrected below after surface is realized.
    r->width_px  = (int)(cfg->cols * cfg->font_size_pt);
    r->height_px = (int)(cfg->rows * cfg->font_size_pt * 2.0);
    r->pitch     = r->width_px * 3;

    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:(NSUInteger)r->width_px
                                    height:(NSUInteger)r->height_px
                                 mipmapped:NO];
    td.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeManaged;
    r->render_target = [r->metal_device newTextureWithDescriptor:td];

    NSRect frame = NSMakeRect(0, 0, r->width_px, r->height_px);
    r->offscreen_view = [[NSView alloc] initWithFrame:frame];
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device     = r->metal_device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.drawableSize = CGSizeMake(r->width_px, r->height_px);
    r->offscreen_view.layer = layer;

    ghostty_surface_config_s sc = ghostty_surface_config_new();
    sc.platform_tag            = GHOSTTY_PLATFORM_MACOS;
    sc.platform.macos.nsview   = (__bridge void *)r->offscreen_view;
    sc.font_size               = (float)cfg->font_size_pt;  // ghostty takes float
    sc.userdata                = r;
    r->surface = ghostty_surface_new(r->app, &sc);
    ghostty_surface_set_size(r->surface, (uint32_t)r->width_px, (uint32_t)r->height_px);
    ghostty_surface_set_focus(r->surface, true);

    // Correct pixel dimensions from ghostty's realized cell metrics.
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
    // Deliver ANSI bytes through the PTY master — ghostty reads from the slave.
    const char *cur = ansi_frame;
    size_t rem = len;
    while (rem > 0) {
        ssize_t n = write(r->pty_master, cur, rem);
        if (n < 0) return SET_ERRNO_SYS(ERROR_SYSTEM, "write pty_master");
        cur += n;
        rem -= (size_t)n;
    }

    // Allow ghostty's event loop to consume the PTY bytes (~one frame period).
    usleep(16000);
    ghostty_app_tick(r->app);
    ghostty_surface_draw(r->surface);

    // Synchronize the managed MTLTexture to CPU-accessible memory.
    id<MTLCommandQueue>      cq  = [r->metal_device newCommandQueue];
    id<MTLCommandBuffer>     cb  = [cq commandBuffer];
    id<MTLBlitCommandEncoder> bc = [cb blitCommandEncoder];
    [bc synchronizeTexture:r->render_target slice:0 level:0];
    [bc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    // BGRA → RGB conversion.
    size_t bgra_pitch = (size_t)r->width_px * 4;
    uint8_t *bgra = SAFE_MALLOC(bgra_pitch * r->height_px, uint8_t *);
    [r->render_target getBytes:bgra
                   bytesPerRow:bgra_pitch
                    fromRegion:MTLRegionMake2D(0,0,r->width_px,r->height_px)
                   mipmapLevel:0];
    for (int y = 0; y < r->height_px; y++) {
        for (int x = 0; x < r->width_px; x++) {
            uint8_t *s = bgra + y * bgra_pitch + x * 4;
            uint8_t *d = r->framebuffer + y * r->pitch + x * 3;
            d[0]=s[2]; d[1]=s[1]; d[2]=s[0];  // BGRA → RGB
        }
    }
    SAFE_FREE(bgra);
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
#endif // __APPLE__
