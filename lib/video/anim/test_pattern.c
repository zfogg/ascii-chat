#include <ascii-chat/video/anim/controller.h>
#include "backends.h"
#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/common.h>
#include <ascii-chat/font.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <string.h>

struct test_pattern {
  image_t *image;
  uint8_t *rgba;
  FT_Library library;
  FT_Face face;
  uint8_t *label[2];
};

static uint8_t channel(double value) {
  return (uint8_t)(fmax(0, fmin(255, value)) + 0.5);
}
static rgb_pixel_t hue(double degrees, double lightness) {
  double h = fmod(degrees, 360) / 60;
  double c = 1 - fabs(2 * lightness - 1), x = c * (1 - fabs(fmod(h, 2) - 1)), m = lightness - c / 2;
  double r = 0, g = 0, b = 0;
  if (h < 1) {
    r = c;
    g = x;
  } else if (h < 2) {
    r = x;
    g = c;
  } else if (h < 3) {
    g = c;
    b = x;
  } else if (h < 4) {
    g = x;
    b = c;
  } else if (h < 5) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  return (rgb_pixel_t){channel((r + m) * 255), channel((g + m) * 255), channel((b + m) * 255)};
}
static void blend(rgb_pixel_t *p, rgb_pixel_t color, double alpha) {
  if (alpha <= 0)
    return;
  if (alpha >= 1) {
    *p = color;
    return;
  }
  p->r = channel(p->r * (1 - alpha) + color.r * alpha);
  p->g = channel(p->g * (1 - alpha) + color.g * alpha);
  p->b = channel(p->b * (1 - alpha) + color.b * alpha);
}
static double overlap(double pixel, double start, double end) {
  return fmax(0, fmin(pixel + 1, end) - fmax(pixel, start));
}

asciichat_error_t test_pattern_resize(test_pattern_t *p, int width, int height) {
  if (!p || width < 1 || height < 1)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid test pattern dimensions");
  if (p->image && p->image->w == width && p->image->h == height)
    return ASCIICHAT_OK;
  image_t *next = image_new(width, height);
  if (!next)
    return SET_ERRNO(ERROR_MEMORY, "Test pattern image allocation failed");
  uint8_t *labels[2] = {NULL, NULL};
  // Use the bundled face on every platform, independent of installed system fonts.
  double size = fmax(18, fmin(width, height) / 10.0);
  size = fmin(size, fmin(width / 10.0, height / 2.0));
  if (FT_Set_Char_Size(p->face, 0, (FT_F26Dot6)lround(fmax(1, size) * 64), 72, 72))
    goto font_error;
  for (int index = 0; index < 2; index++) {
    labels[index] = SAFE_CALLOC((size_t)width * height, 1, uint8_t *);
    const char *text = index ? "ascii-chat test2" : "ascii-chat test";
    int pen = (width >= 240 ? 24 : width / 20) * 64;
    int baseline = (int)fmin(fmax(36, height / 8.0), height - 1);
    for (const char *ch = text; *ch; ch++) {
      FT_Vector delta = {pen % 64, 0};
      FT_Set_Transform(p->face, NULL, &delta);
      if (FT_Load_Char(p->face, (unsigned char)*ch, FT_LOAD_RENDER | FT_LOAD_NO_HINTING))
        goto font_error;
      FT_GlyphSlot glyph = p->face->glyph;
      for (unsigned int y = 0; y < glyph->bitmap.rows; y++) {
        int py = baseline - glyph->bitmap_top + (int)y;
        if (py < 0 || py >= height)
          continue;
        for (unsigned int x = 0; x < glyph->bitmap.width; x++) {
          int px = pen / 64 + glyph->bitmap_left + (int)x;
          if (px >= 0 && px < width)
            labels[index][(size_t)py * width + px] = glyph->bitmap.buffer[y * glyph->bitmap.pitch + x];
        }
      }
      pen += (int)glyph->advance.x;
    }
  }
  if (p->image)
    image_destroy(p->image);
  p->image = next;
  SAFE_FREE(p->rgba);
  for (int index = 0; index < 2; index++) {
    SAFE_FREE(p->label[index]);
    p->label[index] = labels[index];
  }
  return ASCIICHAT_OK;
font_error:
  image_destroy(next);
  SAFE_FREE(labels[0]);
  SAFE_FREE(labels[1]);
  return SET_ERRNO(ERROR_INVALID_STATE, "Could not rasterize test pattern label");
}
asciichat_error_t test_pattern_create(int width, int height, test_pattern_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing test pattern output");
  *out = NULL;
  test_pattern_t *p = SAFE_CALLOC(1, sizeof(*p), test_pattern_t *);
  if (FT_Init_FreeType(&p->library) ||
      FT_New_Memory_Face(p->library, g_font_default, (FT_Long)g_font_default_size, 0, &p->face)) {
    test_pattern_destroy(p);
    return SET_ERRNO(ERROR_INVALID_STATE, "Could not initialize test pattern font");
  }
  asciichat_error_t result = test_pattern_resize(p, width, height);
  if (result != ASCIICHAT_OK) {
    test_pattern_destroy(p);
    return result;
  }
  *out = p;
  return ASCIICHAT_OK;
}
void test_pattern_destroy(test_pattern_t *p) {
  if (!p)
    return;
  if (p->image)
    image_destroy(p->image);
  SAFE_FREE(p->rgba);
  SAFE_FREE(p->label[0]);
  SAFE_FREE(p->label[1]);
  if (p->face)
    FT_Done_Face(p->face);
  if (p->library)
    FT_Done_FreeType(p->library);
  SAFE_FREE(p);
}
image_t *test_pattern_image(test_pattern_t *p) {
  return p ? p->image : NULL;
}

const uint8_t *test_pattern_rgba(test_pattern_t *p) {
  if (!p || !p->image)
    return NULL;
  size_t count = (size_t)p->image->w * p->image->h;
  if (!p->rgba)
    p->rgba = SAFE_MALLOC(count * 4, uint8_t *);
  for (size_t i = 0; i < count; i++) {
    p->rgba[i * 4] = p->image->pixels[i].r;
    p->rgba[i * 4 + 1] = p->image->pixels[i].g;
    p->rgba[i * 4 + 2] = p->image->pixels[i].b;
    p->rgba[i * 4 + 3] = 255;
  }
  return p->rgba;
}

asciichat_error_t test_pattern_render(test_pattern_t *p, int index, double time_ms, bool cadence) {
  animation_sample_t sample;
  animation_config_t config = {.type = ANIMATION_TEST_PATTERN, .fps = 60, .speed = 1};
  asciichat_error_t err = animation_sample_at(config, time_ms / 1000.0, &sample);
  if (err != ASCIICHAT_OK)
    return err;
  animation_target_t target = {.type = ANIMATION_TARGET_TEST_PATTERN,
                               .test_pattern = {.context = p, .index = index, .cadence = cadence}};
  return animation_apply(&sample, &target);
}

asciichat_error_t test_pattern_render_at(test_pattern_t *p, int index, double time_ms, bool cadence) {
  if (!p || !p->image || index < 0 || index > 1 || !isfinite(time_ms) || time_ms < 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid test pattern frame parameters");
  int w = p->image->w, h = p->image->h;
  double phase = fmod(time_ms, index ? 4000 : 10000) / (index ? 4000 : 10000);
  rgb_pixel_t dark = {16, 24, 32}, white = {255, 255, 255};
  rgb_pixel_t first = hue(phase * 360, 0.5), last = hue(fmod(phase + 0.5, 1) * 360, 0.5);
  double box = fmin(fmax(32, fmin(w, h) / 4.0), fmin(w, h) / 2.0);
  double bx = fmod(time_ms / 11, w + box) - box, by = fmod(time_ms / 17, h + box) - box;
  double stripe = w / 12.0, offset = phase * w;
  int first_stripe = (int)ceil(-offset / stripe);
  double radius = fmin(fmax(12, fmin(w, h) / 8.0), fmin(w, h) / 4.0);
  double cx = w + radius - phase * (w + radius * 2), cy = h / 2.0 + sin(phase * 12.566370614359172) * h / 4.0;
  rgb_pixel_t colors[12];
  for (int i = 0; i < 12; i++)
    colors[i] = hue(i * 30, 0.55);
  uint32_t frame = (uint32_t)fmod(floor(time_ms / (1000.0 / 60)), 512);
  uint32_t code = frame ^ (frame >> 1);
  const int slots[8] = {6, 4, 2, 0, 1, 3, 5, 7};
  rgb_pixel_t *pixels = p->image->pixels;
  if (index == 1) {
    // The background is constant down each column. Rasterize one row, then copy.
    for (int x = 0; x < w; x++) {
      rgb_pixel_t pixel = dark;
      if (cadence) {
        int slot = (int)(x / fmax(2, w / 8.0));
        if (slot < 8)
          pixel = (code & (1u << slots[slot])) ? white : (rgb_pixel_t){0, 0, 0};
      } else {
        int k = (int)floor((x - offset) / stripe) + 1;
        for (int j = k; j <= k + 1; j++)
          if (j >= first_stripe && j <= first_stripe + 12)
            blend(&pixel, colors[(j % 12 + 12) % 12], overlap(x, j * stripe + offset - stripe, j * stripe + offset));
      }
      pixels[x] = pixel;
    }
    for (int y = 1; y < h; y++)
      memcpy(pixels + (size_t)y * w, pixels, (size_t)w * sizeof(*pixels));
    if (cadence)
      return ASCIICHAT_OK;
    int top = (int)fmax(0, floor(cy - radius - 1)), bottom = (int)fmin(h, ceil(cy + radius + 1));
    int left = (int)fmax(0, floor(cx - radius - 1)), right = (int)fmin(w, ceil(cx + radius + 1));
    for (int y = top; y < bottom; y++)
      for (int x = left; x < right; x++) {
        double dx0 = x + 0.5 - cx, dy0 = y + 0.5 - cy, d2 = dx0 * dx0 + dy0 * dy0;
        if (d2 > (radius + 0.75) * (radius + 0.75))
          continue;
        double coverage = 1;
        if (d2 > (radius - 0.75) * (radius - 0.75)) {
          int inside = 0;
          for (int sy = 0; sy < 4; sy++)
            for (int sx = 0; sx < 4; sx++) {
              double dx = x + (sx + 0.5) / 4 - cx, dy = y + (sy + 0.5) / 4 - cy;
              if (dx * dx + dy * dy < radius * radius)
                inside++;
            }
          coverage = inside / 16.0;
        }
        blend(&pixels[(size_t)y * w + x], white, coverage);
      }
  } else {
    // A fixed-point ramp avoids three floating-point conversions per source
    // pixel. 4096 intervals keep rounding within one RGB level of Canvas.
    rgb_pixel_t gradient[4097];
    for (int i = 0; i <= 4096; i++) {
      double t = i / 4096.0;
      gradient[i] = (rgb_pixel_t){(uint8_t)(first.r * (1 - t) + last.r * t + 0.5),
                                  (uint8_t)(first.g * (1 - t) + last.g * t + 0.5),
                                  (uint8_t)(first.b * (1 - t) + last.b * t + 0.5)};
    }
    double scale = 4096.0 * 65536 / ((double)w * w + (double)h * h);
    uint32_t step = (uint32_t)(w * scale + 0.5);
    for (int y = 0; y < h; y++) {
      uint32_t position = (uint32_t)((0.5 * w + (y + 0.5) * h) * scale + 0.5);
      for (int x = 0; x < w; x++, position += step)
        pixels[(size_t)y * w + x] = gradient[position >> 16];
    }
    int top = (int)fmax(0, floor(by)), bottom = (int)fmin(h, ceil(by + box));
    int left = (int)fmax(0, floor(bx)), right = (int)fmin(w, ceil(bx + box));
    for (int y = top; y < bottom; y++)
      for (int x = left; x < right; x++)
        blend(&pixels[(size_t)y * w + x], white, 0.8 * overlap(x, bx, bx + box) * overlap(y, by, by + box));
  }
  int label_bottom = (int)fmin(fmax(36, h / 8.0) + 2, h);
  for (int y = 0; y < label_bottom; y++)
    for (int x = 0; x < w; x++) {
      size_t pos = (size_t)y * w + x;
      if (p->label[index][pos])
        blend(&pixels[pos], dark, p->label[index][pos] / 255.0);
    }
  return ASCIICHAT_OK;
}
