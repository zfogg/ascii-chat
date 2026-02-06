// Standalone minimal ASCII converter for Mirror Mode WASM
// No dependencies on full ascii-chat codebase
#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ASCII palette (from darkest to brightest)
static const char PALETTE[] = "   ...',;:clodxkO0KXNWM";
static const int PALETTE_LEN = 23;

// Convert RGB to luminance (ITU-R BT.601)
static inline uint8_t rgb_to_luminance(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)((0.299f * r) + (0.587f * g) + (0.114f * b));
}

// Map luminance (0-255) to ASCII character
static inline char luminance_to_ascii(uint8_t lum) {
  int index = (lum * (PALETTE_LEN - 1)) / 255;
  return PALETTE[index];
}

// Exported function: Convert RGBA frame to ASCII
// Takes RGBA data from canvas, outputs ASCII string
EMSCRIPTEN_KEEPALIVE
char *convert_frame_to_ascii(uint8_t *rgba_data, int src_width, int src_height, int dst_width, int dst_height) {
  // Allocate output buffer
  int output_size = dst_width * dst_height;
  char *output = (char *)malloc(output_size + 1);
  if (!output)
    return NULL;

  // Calculate sampling step
  float x_ratio = (float)src_width / (float)dst_width;
  float y_ratio = (float)src_height / (float)dst_height;

  // Convert each character position
  for (int y = 0; y < dst_height; y++) {
    for (int x = 0; x < dst_width; x++) {
      // Sample source pixel (nearest neighbor)
      int src_x = (int)(x * x_ratio);
      int src_y = (int)(y * y_ratio);
      int src_idx = (src_y * src_width + src_x) * 4;

      // Get RGB values (skip alpha)
      uint8_t r = rgba_data[src_idx + 0];
      uint8_t g = rgba_data[src_idx + 1];
      uint8_t b = rgba_data[src_idx + 2];

      // Convert to luminance and then to ASCII
      uint8_t lum = rgb_to_luminance(r, g, b);
      char ascii_char = luminance_to_ascii(lum);

      // Store in output buffer
      output[y * dst_width + x] = ascii_char;
    }
  }

  output[output_size] = '\0';
  return output;
}

// Free memory allocated by convert_frame_to_ascii
EMSCRIPTEN_KEEPALIVE
void free_ascii_buffer(char *buffer) {
  if (buffer) {
    free(buffer);
  }
}
