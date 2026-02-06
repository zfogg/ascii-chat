// lib/video/ascii_wasm.c
// WASM-specific ASCII rendering wrapper for browser
#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Include the actual headers
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/palette.h>

// External function from ascii.c
extern char *ascii_convert(image_t *original, const ssize_t width, const ssize_t height, const bool color,
                           const bool _aspect_ratio, const bool stretch, const char *palette_chars,
                           const char luminance_palette[256]);

// External function from palette.c
extern int build_client_luminance_palette(const char *palette_chars, size_t palette_len, char luminance_mapping[256]);

// Default palette (from palette.h)
static const char *WASM_PALETTE = "   ...',;:clodxkO0KXNWM";
static const size_t WASM_PALETTE_LEN = 23;

// Global luminance palette (initialized once)
static char g_luminance_palette[256];
static bool g_palette_initialized = false;

// Initialize palette on first use
static void ensure_palette_initialized(void) {
  if (!g_palette_initialized) {
    build_client_luminance_palette(WASM_PALETTE, WASM_PALETTE_LEN, g_luminance_palette);
    g_palette_initialized = true;
  }
}

// Exported function: Convert RGBA frame to ASCII
// Takes RGBA data (as from Canvas getImageData), converts to RGB, renders to ASCII
EMSCRIPTEN_KEEPALIVE
char *convert_frame_to_ascii(uint8_t *rgba_data, // RGBA format from canvas (4 bytes per pixel)
                             int width, int height, int ascii_width, int ascii_height) {
  ensure_palette_initialized();

  // Create image_t structure
  image_t image;
  image.w = width;
  image.h = height;

  // Allocate RGB pixel array (3 bytes per pixel)
  image.pixels = (rgb_pixel_t *)malloc(width * height * sizeof(rgb_pixel_t));
  if (!image.pixels) {
    return NULL;
  }

  // Convert RGBA to RGB (skip alpha channel)
  for (int i = 0; i < width * height; i++) {
    image.pixels[i].r = rgba_data[i * 4 + 0];
    image.pixels[i].g = rgba_data[i * 4 + 1];
    image.pixels[i].b = rgba_data[i * 4 + 2];
    // Skip alpha channel (rgba_data[i * 4 + 3])
  }

  // Call ascii_convert
  // Parameters: image, target_width, target_height, color, aspect_ratio, stretch, palette, luminance
  char *result = ascii_convert(&image, ascii_width, ascii_height,
                               false, // color: false for monochrome (simpler for MVP)
                               true,  // aspect_ratio: true to maintain aspect
                               false, // stretch: false to preserve aspect ratio
                               WASM_PALETTE, g_luminance_palette);

  // Free the temporary RGB buffer
  free(image.pixels);

  return result;
}

// Free memory allocated by convert_frame_to_ascii
EMSCRIPTEN_KEEPALIVE
void free_ascii_buffer(char *buffer) {
  if (buffer) {
    free(buffer);
  }
}
