// Mock webcam implementation for testing
// Provides video file playback or test pattern generation

#include "common.h"
#include "logging.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#define usleep(x) Sleep((x) / 1000)
#else
#include <unistd.h>
#endif

#ifdef HAVE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#endif

// Include mock header first to get overrides
#include "tests/mocks/webcam_mock.h"

// Now include the actual types
typedef struct webcam_context_t webcam_context_t;
#include "image2ascii/image.h"

// Mock context structure
typedef struct mock_webcam_context {
  int width;
  int height;
  int frame_count;
  uint8_t *frame_buffer;
  time_t start_time;

#ifdef HAVE_FFMPEG
  // FFmpeg video playback
  AVFormatContext *format_ctx;
  AVCodecContext *codec_ctx;
  AVFrame *frame;
  AVFrame *rgb_frame;
  AVPacket *packet;
  struct SwsContext *sws_ctx;
  int video_stream_index;
#endif

  const char *video_path;
  bool use_test_pattern;
} mock_webcam_context_t;

// Global mock configuration
static struct {
  const char *video_file;
  bool use_test_pattern;
  int width;
  int height;
  bool initialized;
} g_mock_config = {.video_file = NULL, .use_test_pattern = true, .width = 640, .height = 480, .initialized = false};

// Global context for simple API
static mock_webcam_context_t *g_global_ctx = NULL;

// Configuration functions
void mock_webcam_set_video_file(const char *path) {
  g_mock_config.video_file = path;
  g_mock_config.use_test_pattern = false;
}

void mock_webcam_set_test_pattern(bool enable) {
  g_mock_config.use_test_pattern = enable;
}

void mock_webcam_set_dimensions(int width, int height) {
  g_mock_config.width = width;
  g_mock_config.height = height;
}

void mock_webcam_reset(void) {
  g_mock_config.video_file = NULL;
  g_mock_config.use_test_pattern = true;
  g_mock_config.width = 640;
  g_mock_config.height = 480;
}

// Generate test pattern for rgb_t pixel array
static void generate_test_pattern_rgb(rgb_t *pixels, int width, int height, int frame_num) {
  // Create moving patterns that are good for ASCII conversion
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = y * width + x;

      // Moving sine wave pattern
      float wave = sin((x + frame_num * 5) * 0.05) * 127 + 128;

      // Checkerboard
      bool checker = ((x / 40) + (y / 40)) % 2;

      // Circular gradient from center
      int cx = width / 2;
      int cy = height / 2;
      float dist = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      float max_dist = sqrt(cx * cx + cy * cy);
      float gradient = (1.0 - dist / max_dist) * 255;

      // Moving circle
      int circle_x = cx + (int)(100 * cos(frame_num * 0.1));
      int circle_y = cy + (int)(100 * sin(frame_num * 0.1));
      float circle_dist = sqrt((x - circle_x) * (x - circle_x) + (y - circle_y) * (y - circle_y));
      bool in_circle = circle_dist < 50;

      if (in_circle) {
        // Bright moving circle
        pixels[idx].r = 255;
        pixels[idx].g = 200;
        pixels[idx].b = 100;
      } else if (checker) {
        // Checkerboard with wave
        pixels[idx].r = (uint8_t)wave;
        pixels[idx].g = (uint8_t)wave;
        pixels[idx].b = (uint8_t)wave;
      } else {
        // Gradient background
        pixels[idx].r = (uint8_t)(gradient * 0.3);
        pixels[idx].g = (uint8_t)(gradient * 0.5);
        pixels[idx].b = (uint8_t)(gradient * 0.7);
      }
    }
  }

  // Add frame counter overlay (simulated as a bright box)
  int box_y = height - 60;
  for (int y = box_y; y < box_y + 40 && y < height; y++) {
    for (int x = 10; x < 200 && x < width; x++) {
      int idx = y * width + x;
      if (y == box_y || y == box_y + 39 || x == 10 || x == 199) {
        // White border
        pixels[idx].r = 255;
        pixels[idx].g = 255;
        pixels[idx].b = 255;
      } else {
        // Black background for text
        pixels[idx].r = 0;
        pixels[idx].g = 0;
        pixels[idx].b = 0;
      }
    }
  }
}

// Generate test pattern (legacy function for raw buffer)
static void generate_test_pattern(uint8_t *buffer, int width, int height, int frame_num) {
  // Create moving patterns that are good for ASCII conversion
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = y * width + x;

      // Moving sine wave pattern
      float wave = sin((x + frame_num * 5) * 0.05) * 127 + 128;

      // Checkerboard
      bool checker = ((x / 40) + (y / 40)) % 2;

      // Circular gradient from center
      int cx = width / 2;
      int cy = height / 2;
      float dist = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      float max_dist = sqrt(cx * cx + cy * cy);
      float gradient = (1.0 - dist / max_dist) * 255;

      // Moving circle
      int circle_x = cx + (int)(100 * cos(frame_num * 0.1));
      int circle_y = cy + (int)(100 * sin(frame_num * 0.1));
      float circle_dist = sqrt((x - circle_x) * (x - circle_x) + (y - circle_y) * (y - circle_y));
      bool in_circle = circle_dist < 50;

      if (in_circle) {
        // Bright moving circle
        buffer[idx * 3] = 255;
        buffer[idx * 3 + 1] = 200;
        buffer[idx * 3 + 2] = 100;
      } else if (checker) {
        // Checkerboard with wave
        buffer[idx * 3] = (uint8_t)wave;
        buffer[idx * 3 + 1] = (uint8_t)wave;
        buffer[idx * 3 + 2] = (uint8_t)wave;
      } else {
        // Gradient background
        buffer[idx * 3] = (uint8_t)(gradient * 0.3);
        buffer[idx * 3 + 1] = (uint8_t)(gradient * 0.5);
        buffer[idx * 3 + 2] = (uint8_t)(gradient * 0.7);
      }
    }
  }

  // Add frame counter overlay (simulated as a bright box)
  int box_y = height - 60;
  for (int y = box_y; y < box_y + 40 && y < height; y++) {
    for (int x = 10; x < 200 && x < width; x++) {
      int idx = y * width + x;
      if (y == box_y || y == box_y + 39 || x == 10 || x == 199) {
        // White border
        buffer[idx * 3] = 255;
        buffer[idx * 3 + 1] = 255;
        buffer[idx * 3 + 2] = 255;
      } else {
        // Black background for text
        buffer[idx * 3] = 0;
        buffer[idx * 3 + 1] = 0;
        buffer[idx * 3 + 2] = 0;
      }
    }
  }
}

#ifdef HAVE_FFMPEG
// Initialize FFmpeg video playback
static bool init_video_playback(mock_webcam_context_t *ctx, const char *video_path) {
  av_register_all();

  if (avformat_open_input(&ctx->format_ctx, video_path, NULL, NULL) < 0) {
    log_error("Mock: Failed to open video file: %s", video_path);
    return false;
  }

  if (avformat_find_stream_info(ctx->format_ctx, NULL) < 0) {
    log_error("Mock: Failed to find stream info");
    avformat_close_input(&ctx->format_ctx);
    return false;
  }

  // Find video stream
  ctx->video_stream_index = -1;
  for (unsigned int i = 0; i < ctx->format_ctx->nb_streams; i++) {
    if (ctx->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      ctx->video_stream_index = i;
      break;
    }
  }

  if (ctx->video_stream_index == -1) {
    log_error("Mock: No video stream found");
    avformat_close_input(&ctx->format_ctx);
    return false;
  }

  // Setup decoder
  AVCodecParameters *codecpar = ctx->format_ctx->streams[ctx->video_stream_index]->codecpar;
  AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
  if (!codec) {
    log_error("Mock: Codec not found");
    avformat_close_input(&ctx->format_ctx);
    return false;
  }

  ctx->codec_ctx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(ctx->codec_ctx, codecpar);

  if (avcodec_open2(ctx->codec_ctx, codec, NULL) < 0) {
    log_error("Mock: Failed to open codec");
    avcodec_free_context(&ctx->codec_ctx);
    avformat_close_input(&ctx->format_ctx);
    return false;
  }

  // Allocate frames and scaling context
  ctx->frame = av_frame_alloc();
  ctx->rgb_frame = av_frame_alloc();
  ctx->packet = av_packet_alloc();

  ctx->width = ctx->codec_ctx->width;
  ctx->height = ctx->codec_ctx->height;

  int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, ctx->width, ctx->height, 1);
  ctx->frame_buffer = SAFE_MALLOC(num_bytes);

  av_image_fill_arrays(ctx->rgb_frame->data, ctx->rgb_frame->linesize, ctx->frame_buffer, AV_PIX_FMT_RGB24, ctx->width,
                       ctx->height, 1);

  ctx->sws_ctx = sws_getContext(ctx->width, ctx->height, ctx->codec_ctx->pix_fmt, ctx->width, ctx->height,
                                AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

  log_info("Mock: Video playback initialized: %s (%dx%d)", video_path, ctx->width, ctx->height);
  return true;
}

// Read frame from video file
static bool read_video_frame(mock_webcam_context_t *ctx, uint8_t *output) {
  while (av_read_frame(ctx->format_ctx, ctx->packet) >= 0) {
    if (ctx->packet->stream_index == ctx->video_stream_index) {
      if (avcodec_send_packet(ctx->codec_ctx, ctx->packet) == 0) {
        if (avcodec_receive_frame(ctx->codec_ctx, ctx->frame) == 0) {
          // Convert to RGB
          sws_scale(ctx->sws_ctx, (const uint8_t *const *)ctx->frame->data, ctx->frame->linesize, 0, ctx->height,
                    ctx->rgb_frame->data, ctx->rgb_frame->linesize);

          memcpy(output, ctx->frame_buffer, ctx->width * ctx->height * 3);
          av_packet_unref(ctx->packet);
          return true;
        }
      }
    }
    av_packet_unref(ctx->packet);
  }

  // Loop video
  av_seek_frame(ctx->format_ctx, ctx->video_stream_index, 0, AVSEEK_FLAG_BACKWARD);
  return false;
}
#endif

// Context-based API implementation
int mock_webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
  (void)device_index; // Unused in mock

  log_info("Mock: Initializing webcam context");

  mock_webcam_context_t *mock = SAFE_CALLOC(1, sizeof(mock_webcam_context_t));
  if (!mock) {
    return -1;
  }

  mock->video_path = g_mock_config.video_file;
  mock->use_test_pattern = g_mock_config.use_test_pattern;
  mock->width = g_mock_config.width;
  mock->height = g_mock_config.height;
  mock->frame_count = 0;
  mock->start_time = time(NULL);

#ifdef HAVE_FFMPEG
  // Try to use video file if specified
  if (mock->video_path && !mock->use_test_pattern) {
    if (!init_video_playback(mock, mock->video_path)) {
      log_warn("Mock: Failed to init video, falling back to test pattern");
      mock->use_test_pattern = true;
    }
  }
#else
  if (mock->video_path) {
    log_warn("Mock: FFmpeg not available, using test pattern instead of video");
  }
  mock->use_test_pattern = true;
#endif

  if (mock->use_test_pattern) {
    mock->frame_buffer = SAFE_MALLOC(mock->width * mock->height * 3);
    log_info("Mock: Using test pattern (%dx%d)", mock->width, mock->height);
  }

  *ctx = (webcam_context_t *)mock;
  return 0;
}

void mock_webcam_cleanup_context(webcam_context_t *ctx) {
  mock_webcam_context_t *mock = (mock_webcam_context_t *)ctx;
  if (!mock)
    return;

  log_info("Mock: Cleaning up webcam (generated %d frames)", mock->frame_count);

#ifdef HAVE_FFMPEG
  if (mock->format_ctx) {
    av_frame_free(&mock->frame);
    av_frame_free(&mock->rgb_frame);
    av_packet_free(&mock->packet);
    sws_freeContext(mock->sws_ctx);
    avcodec_free_context(&mock->codec_ctx);
    avformat_close_input(&mock->format_ctx);
  }
#endif

  SAFE_FREE(mock->frame_buffer);
  SAFE_FREE(mock);
}

image_t *mock_webcam_read_context(webcam_context_t *ctx) {
  mock_webcam_context_t *mock = (mock_webcam_context_t *)ctx;
  if (!mock)
    return NULL;

  image_t *img = SAFE_MALLOC(sizeof(image_t));
  if (!img)
    return NULL;

  img->w = mock->width;
  img->h = mock->height;
  img->pixels = SAFE_MALLOC(mock->width * mock->height * sizeof(rgb_t));

  if (!img->pixels) {
    SAFE_FREE(img);
    return NULL;
  }

#ifdef HAVE_FFMPEG
  if (mock->format_ctx && !mock->use_test_pattern) {
    // Create a temporary buffer for FFmpeg data and convert to rgb_t
    uint8_t *temp_buffer = SAFE_MALLOC(mock->width * mock->height * 3);
    if (read_video_frame(mock, temp_buffer)) {
      // Convert from raw RGB buffer to rgb_t array
      for (int i = 0; i < mock->width * mock->height; i++) {
        img->pixels[i].r = temp_buffer[i * 3];
        img->pixels[i].g = temp_buffer[i * 3 + 1];
        img->pixels[i].b = temp_buffer[i * 3 + 2];
      }
    } else {
      // If video read fails, fall back to test pattern
      generate_test_pattern_rgb(img->pixels, mock->width, mock->height, mock->frame_count);
    }
    SAFE_FREE(temp_buffer);
  } else
#endif
  {
    generate_test_pattern_rgb(img->pixels, mock->width, mock->height, mock->frame_count);
  }

  mock->frame_count++;

  // Simulate 30 FPS
  usleep(33333);

  return img;
}

int mock_webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  mock_webcam_context_t *mock = (mock_webcam_context_t *)ctx;
  if (!mock)
    return -1;

  *width = mock->width;
  *height = mock->height;
  return 0;
}

// Simple API implementation (uses global context)
int mock_webcam_init(unsigned short int webcam_index) {
  if (g_global_ctx) {
    log_warn("Mock: Global webcam already initialized");
    return -1;
  }

  return mock_webcam_init_context((webcam_context_t **)&g_global_ctx, webcam_index);
}

image_t *mock_webcam_read(void) {
  if (!g_global_ctx) {
    log_error("Mock: Webcam not initialized");
    return NULL;
  }

  return mock_webcam_read_context((webcam_context_t *)g_global_ctx);
}

void mock_webcam_cleanup(void) {
  if (g_global_ctx) {
    mock_webcam_cleanup_context((webcam_context_t *)g_global_ctx);
    g_global_ctx = NULL;
  }
}