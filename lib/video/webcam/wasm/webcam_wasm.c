/**
 * @file webcam_wasm.c
 * @brief WebAssembly (browser) webcam capture backend
 *
 * This module provides webcam access in WebAssembly builds by bridging the browser's
 * getUserMedia API to the cross-platform webcam_context_t interface.
 *
 * The implementation:
 * - Uses EM_JS to call getUserMedia() in the browser
 * - Captures frames via canvas.drawImage() + getImageData()
 * - Converts RGBA to RGB24 in C
 * - Runs frame capture on the main thread (browser requirement)
 *
 * @ingroup webcam
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>

/* ============================================================================
 * Context Structure
 * ============================================================================
 */

/**
 * @struct webcam_context_t
 * @brief WASM webcam context - holds state for browser-based webcam capture
 */
struct webcam_context_t {
    int width;              ///< Requested frame width (usually 320, 640, or 1280)
    int height;             ///< Requested frame height (usually 240, 480, or 720)
    uint8_t *rgba_buf;      ///< RGBA staging buffer (width * height * 4 bytes)
    image_t *cached_frame;  ///< RGB24 output image (cached, reused across reads)
};

/* ============================================================================
 * JavaScript Bridges (EM_JS)
 * ============================================================================
 */

/**
 * Initialize browser webcam: ensure canvas is sized correctly
 * Assumes Module.webcamVideo and Module.webcamCanvas are already set up by JavaScript
 * (JavaScript must call getUserMedia and initialize these before WASM code starts)
 */
EM_JS(int, js_webcam_init, (int width, int height), {
    // Check if Module.webcamVideo exists (should be set up by JavaScript)
    if (!Module.webcamVideo) {
        console.error('[WASM Webcam] Module.webcamVideo not found - JavaScript must initialize it first');
        return -1;
    }

    // Ensure canvas exists and is properly sized
    if (!Module.webcamCanvas) {
        Module.webcamCanvas = document.createElement('canvas');
        Module.webcamCanvas.style.display = 'none';
        document.body.appendChild(Module.webcamCanvas);
    }

    Module.webcamCanvas.width = width;
    Module.webcamCanvas.height = height;

    console.log('[WASM Webcam] Initialized at ' + width + 'x' + height);
    return 1;
});

/**
 * Capture a single frame from the webcam via canvas.getImageData()
 * Copies RGBA data into the provided buffer
 * Returns 1 on success, 0 if stream not ready
 */
EM_JS(int, js_webcam_read_frame, (uint8_t *rgba_buf, int width, int height), {
    try {
        if (!Module.webcamVideo || !Module.webcamStream || Module.webcamVideo.readyState < 2) {
            console.warn('[WASM Webcam] Video not ready');
            return 0;
        }

        const canvas = Module.webcamCanvas;
        const ctx = canvas.getContext('2d');

        // Draw current video frame to canvas
        ctx.drawImage(Module.webcamVideo, 0, 0, width, height);

        // Get pixel data as RGBA
        const imageData = ctx.getImageData(0, 0, width, height);
        const data = imageData.data;

        // Copy into WASM heap (RGBA format, 4 bytes per pixel)
        const byteOffset = rgba_buf;
        for (let i = 0; i < data.length; i++) {
            HEAPU8[byteOffset + i] = data[i];
        }

        return 1;
    } catch (err) {
        console.error('[WASM Webcam] Frame read failed:', err);
        return 0;
    }
});

/**
 * Stop the webcam stream and clean up resources
 */
EM_JS(void, js_webcam_cleanup, (), {
    try {
        if (Module.webcamStream) {
            Module.webcamStream.getTracks().forEach(track => track.stop());
            Module.webcamStream = null;
        }
        if (Module.webcamVideo) {
            Module.webcamVideo.srcObject = null;
            if (Module.webcamVideo.parentNode) {
                Module.webcamVideo.parentNode.removeChild(Module.webcamVideo);
            }
            Module.webcamVideo = null;
        }
        if (Module.webcamCanvas) {
            if (Module.webcamCanvas.parentNode) {
                Module.webcamCanvas.parentNode.removeChild(Module.webcamCanvas);
            }
            Module.webcamCanvas = null;
        }
    } catch (err) {
        console.error('[WASM Webcam] Cleanup failed:', err);
    }
});

/**
 * Enumerate available media devices
 * Returns count of video input devices (0, 1, or more)
 */
EM_JS(int, js_webcam_enumerate_devices, (), {
    try {
        return navigator.mediaDevices.enumerateDevices().then(devices => {
            const videoDevices = devices.filter(d => d.kind === 'videoinput');
            return videoDevices.length;
        }).catch(err => {
            console.error('[WASM Webcam] Enumeration failed:', err);
            return 1; // Assume at least the default camera
        });
    } catch (err) {
        console.error('[WASM Webcam] Enumeration error:', err);
        return 1;
    }
});

/* ============================================================================
 * Helper: RGBA to RGB24 Conversion
 * ============================================================================
 */

/**
 * Convert RGBA pixels (4 bytes per pixel) to RGB24 (3 bytes per pixel)
 * Strips the alpha channel
 */
static void rgba_to_rgb24(const uint8_t *rgba_buf, int width, int height, rgb_pixel_t *rgb_pixels) {
    int pixel_count = width * height;
    for (int i = 0; i < pixel_count; i++) {
        // RGBA input: 4 bytes per pixel
        // RGB output: 3 bytes per pixel (packed)
        rgb_pixels[i].r = rgba_buf[i * 4 + 0];
        rgb_pixels[i].g = rgba_buf[i * 4 + 1];
        rgb_pixels[i].b = rgba_buf[i * 4 + 2];
        // alpha (rgba_buf[i * 4 + 3]) ignored
    }
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================
 */

asciichat_error_t webcam_list_devices(webcam_device_info_t **devices, unsigned int *count) {
    if (!devices || !count) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "devices and count must not be NULL");
    }

    // In browser, we report a single "Browser Webcam" device
    // (actual enumeration would require async Promise handling)
    *devices = SAFE_MALLOC(sizeof(webcam_device_info_t), webcam_device_info_t *);
    if (!*devices) {
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate device list");
    }

    (*devices)[0].index = 0;
    snprintf((*devices)[0].name, WEBCAM_DEVICE_NAME_MAX, "Browser Webcam");
    *count = 1;

    return ASCIICHAT_OK;
}

void webcam_free_device_list(webcam_device_info_t *devices) {
    SAFE_FREE(devices);
}

asciichat_error_t webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
    if (!ctx) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "ctx must not be NULL");
    }

    // Only support device 0 in browser
    if (device_index != 0) {
        return SET_ERRNO(ERROR_WEBCAM, "Browser webcam only supports device index 0");
    }

    // Allocate context
    *ctx = SAFE_MALLOC(sizeof(struct webcam_context_t), struct webcam_context_t *);
    if (!*ctx) {
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate webcam context");
    }

    // Default resolution
    int width = 640;
    int height = 480;

    // Try to get preferred resolution from options
    const int opt_width = GET_OPTION(width);
    const int opt_height = GET_OPTION(height);
    if (opt_width > 0 && opt_height > 0) {
        width = opt_width;
        height = opt_height;
    }

    (*ctx)->width = width;
    (*ctx)->height = height;

    // Allocate RGBA staging buffer (4 bytes per pixel)
    size_t rgba_size = (size_t)width * height * 4;
    (*ctx)->rgba_buf = SAFE_MALLOC(rgba_size, uint8_t *);
    if (!(*ctx)->rgba_buf) {
        SAFE_FREE(*ctx);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate RGBA buffer");
    }

    // Allocate RGB24 output image
    (*ctx)->cached_frame = image_new(width, height);
    if (!(*ctx)->cached_frame) {
        SAFE_FREE((*ctx)->rgba_buf);
        SAFE_FREE(*ctx);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate output image");
    }

    // Initialize browser webcam (synchronous call wrapping async promise)
    int init_result = js_webcam_init(width, height);
    if (init_result < 0) {
        image_destroy((*ctx)->cached_frame);
        SAFE_FREE((*ctx)->rgba_buf);
        SAFE_FREE(*ctx);
        return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize browser webcam");
    }

    log_debug("WASM webcam initialized: %dx%d", width, height);
    return ASCIICHAT_OK;
}

void webcam_cleanup_context(webcam_context_t *ctx) {
    if (!ctx) {
        return;
    }

    js_webcam_cleanup();

    if (ctx->cached_frame) {
        image_destroy(ctx->cached_frame);
    }

    SAFE_FREE(ctx->rgba_buf);
    SAFE_FREE(ctx);

    log_debug("WASM webcam cleaned up");
}

void webcam_flush_context(webcam_context_t *ctx) {
    // No-op for WASM - no blocking operations to interrupt
    (void)ctx;
}

image_t *webcam_read_context(webcam_context_t *ctx) {
    if (!ctx || !ctx->cached_frame) {
        return NULL;
    }

    // Capture frame from canvas into RGBA buffer
    int read_result = js_webcam_read_frame(ctx->rgba_buf, ctx->width, ctx->height);
    if (read_result <= 0) {
        log_warn_every(100, "Failed to read webcam frame");
        return NULL;
    }

    // Convert RGBA to RGB24 in-place into the cached image
    rgba_to_rgb24(ctx->rgba_buf, ctx->width, ctx->height, ctx->cached_frame->pixels);

    return ctx->cached_frame;
}

image_t *webcam_read_async(webcam_context_t *ctx) {
    // WASM doesn't have a background thread model like other platforms
    // Fall back to synchronous read
    return webcam_read_context(ctx);
}

asciichat_error_t webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
    if (!ctx || !width || !height) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "ctx, width, and height must not be NULL");
    }

    *width = ctx->width;
    *height = ctx->height;

    return ASCIICHAT_OK;
}

// Global webcam context for the simplified interface
static webcam_context_t *g_webcam_ctx = NULL;

asciichat_error_t webcam_init(unsigned short int webcam_index) {
    if (g_webcam_ctx) {
        return ASCIICHAT_OK; // Already initialized
    }
    return webcam_init_context(&g_webcam_ctx, webcam_index);
}

image_t *webcam_read(void) {
    if (!g_webcam_ctx) {
        return NULL;
    }
    return webcam_read_context(g_webcam_ctx);
}

void webcam_destroy(void) {
    if (g_webcam_ctx) {
        webcam_cleanup_context(g_webcam_ctx);
        g_webcam_ctx = NULL;
    }
}

void webcam_flush(void) {
    if (g_webcam_ctx) {
        webcam_flush_context(g_webcam_ctx);
    }
}

void webcam_print_init_error_help(asciichat_error_t error_code) {
    (void)error_code;
    log_error("Webcam initialization failed. Check that:");
    log_error("1. HTTPS is enabled (or using localhost)");
    log_error("2. Browser has camera permissions granted");
    log_error("3. Camera device is not in use by another application");
}

#endif // __EMSCRIPTEN__
