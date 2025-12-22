/**
 * @file os/linux/webcam_v4l2.c
 * @ingroup webcam
 * @brief 📷 Linux V4L2 webcam capture implementation with MJPEG/YUY2 format support
 */

#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "os/webcam.h"
#include "common.h"
#include "platform/file.h"
#include "platform/internal.h"

#define WEBCAM_BUFFER_COUNT_DEFAULT 4
#define WEBCAM_BUFFER_COUNT_MAX 8
#define WEBCAM_DEVICE_INDEX_MAX 99
#define WEBCAM_READ_RETRY_COUNT 3

typedef struct {
  void *start;
  size_t length;
} webcam_buffer_t;

struct webcam_context_t {
  int fd;
  int width;
  int height;
  uint32_t pixelformat; // Actual pixel format from driver (RGB24 or YUYV)
  webcam_buffer_t *buffers;
  int buffer_count;
};

/**
 * @brief Convert YUYV (YUV 4:2:2) to RGB24
 *
 * YUYV packs 2 pixels into 4 bytes: Y0 U Y1 V
 * Each Y gets its own pixel, U and V are shared between adjacent pixels.
 *
 * @param yuyv Source YUYV buffer
 * @param rgb Destination RGB24 buffer
 * @param width Frame width
 * @param height Frame height
 */
static void yuyv_to_rgb24(const uint8_t *yuyv, uint8_t *rgb, int width, int height) {
  const int num_pixels = width * height;
  for (int i = 0; i < num_pixels; i += 2) {
    // Each 4 bytes of YUYV contains 2 pixels
    const int yuyv_idx = i * 2;
    const int y0 = yuyv[yuyv_idx + 0];
    const int u = yuyv[yuyv_idx + 1];
    const int y1 = yuyv[yuyv_idx + 2];
    const int v = yuyv[yuyv_idx + 3];

    // Convert YUV to RGB using standard formula
    // R = Y + 1.402 * (V - 128)
    // G = Y - 0.344 * (U - 128) - 0.714 * (V - 128)
    // B = Y + 1.772 * (U - 128)
    const int c0 = y0 - 16;
    const int c1 = y1 - 16;
    const int d = u - 128;
    const int e = v - 128;

    // First pixel
    int r = (298 * c0 + 409 * e + 128) >> 8;
    int g = (298 * c0 - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c0 + 516 * d + 128) >> 8;

    const int rgb_idx0 = i * 3;
    rgb[rgb_idx0 + 0] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
    rgb[rgb_idx0 + 1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
    rgb[rgb_idx0 + 2] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));

    // Second pixel
    r = (298 * c1 + 409 * e + 128) >> 8;
    g = (298 * c1 - 100 * d - 208 * e + 128) >> 8;
    b = (298 * c1 + 516 * d + 128) >> 8;

    const int rgb_idx1 = (i + 1) * 3;
    rgb[rgb_idx1 + 0] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
    rgb[rgb_idx1 + 1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
    rgb[rgb_idx1 + 2] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
  }
}

/**
 * @brief Set the format of the webcam
 *
 * Tries RGB24 first (native), then falls back to YUYV (most common).
 * V4L2 drivers may change the requested format, so we check what was actually set.
 *
 * @param ctx The webcam context
 * @param width The width of the frame
 * @param height The height of the frame
 * @return int 0 on success, -1 on failure
 */
static int webcam_v4l2_set_format(webcam_context_t *ctx, int width, int height) {
  struct v4l2_format fmt = {0};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;

  // Try RGB24 first (ideal - no conversion needed)
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
  if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24) {
    ctx->pixelformat = V4L2_PIX_FMT_RGB24;
    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    log_info("V4L2 format set to RGB24 %dx%d", ctx->width, ctx->height);
    return 0;
  }

  // Fall back to YUYV (most webcams support this natively)
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
    ctx->pixelformat = V4L2_PIX_FMT_YUYV;
    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    log_info("V4L2 format set to YUYV %dx%d (will convert to RGB)", ctx->width, ctx->height);
    return 0;
  }

  log_error("Failed to set V4L2 format: device supports neither RGB24 nor YUYV");
  return -1;
}

static int webcam_v4l2_init_buffers(webcam_context_t *ctx) {
  struct v4l2_requestbuffers req = {0};
  req.count = WEBCAM_BUFFER_COUNT_DEFAULT;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) == -1) {
    log_error("Failed to request V4L2 buffers: %s", SAFE_STRERROR(errno));
    return -1;
  }

  if (req.count < 2) {
    log_error("Insufficient buffer memory");
    return -1;
  }

  // Ensure we don't exceed our maximum buffer count
  if (req.count > WEBCAM_BUFFER_COUNT_MAX) {
    log_warn("Driver requested %d buffers, limiting to %d", req.count, WEBCAM_BUFFER_COUNT_MAX);
    req.count = WEBCAM_BUFFER_COUNT_MAX;
  }

  ctx->buffer_count = req.count;

  // Allocate buffer array
  ctx->buffers = SAFE_MALLOC(sizeof(webcam_buffer_t) * ctx->buffer_count, webcam_buffer_t *);
  if (!ctx->buffers) {
    log_error("Failed to allocate buffer array");
    return -1;
  }

  // Initialize buffer array
  memset(ctx->buffers, 0, sizeof(webcam_buffer_t) * ctx->buffer_count);

  for (int i = 0; i < ctx->buffer_count; i++) {
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) == -1) {
      log_error("Failed to query buffer %d: %s", i, SAFE_STRERROR(errno));
      return -1;
    }

    ctx->buffers[i].length = buf.length;
    ctx->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buf.m.offset);

    if (ctx->buffers[i].start == MAP_FAILED) {
      log_error("Failed to mmap buffer %d: %s", i, SAFE_STRERROR(errno));
      return -1;
    }
  }

  return 0;
}

static int webcam_v4l2_start_streaming(webcam_context_t *ctx) {
  // Queue all buffers
  for (int i = 0; i < ctx->buffer_count; i++) {
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
      log_error("Failed to queue buffer %d: %s", i, SAFE_STRERROR(errno));
      return -1;
    }
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) == -1) {
    log_error("Failed to start V4L2 streaming: %s", SAFE_STRERROR(errno));
    return -1;
  }

  log_info("V4L2 streaming started");
  return 0;
}

asciichat_error_t webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
  webcam_context_t *context;
  context = SAFE_MALLOC(sizeof(webcam_context_t), webcam_context_t *);
  if (!context) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate webcam context");
  }

  memset(context, 0, sizeof(webcam_context_t));

  // Validate device index
  if (device_index > WEBCAM_DEVICE_INDEX_MAX) {
    SAFE_FREE(context);
    return SET_ERRNO(ERROR_WEBCAM, "Invalid device index: %d (max: %d)", device_index, WEBCAM_DEVICE_INDEX_MAX);
  }

  // Open V4L2 device
  char device_path[32];
  SAFE_SNPRINTF(device_path, sizeof(device_path), "/dev/video%d", device_index);

  // Check if device file exists before trying to open it
  context->fd = platform_open(device_path, PLATFORM_O_RDWR | O_NONBLOCK);
  if (context->fd == -1) {
    // Provide more helpful error messages based on errno
    if (errno == ENOENT) {
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM,
                       "V4L2 device %s does not exist.\n"
                       "No webcam found. Try:\n"
                       "  1. Check if camera is connected: ls /dev/video*\n"
                       "  2. Use test pattern instead: --test-pattern",
                       device_path);
    } else if (errno == EACCES) {
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM_PERMISSION,
                       "Permission denied accessing %s.\n"
                       "Try: sudo usermod -a -G video $USER\n"
                       "Then log out and log back in.",
                       device_path);
    } else if (errno == EBUSY) {
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM_IN_USE, "V4L2 device %s is already in use by another application.", device_path);
    } else {
      SAFE_FREE(context);
      return SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to open V4L2 device %s", device_path);
    }
  }

  // Check if it's a video capture device
  struct v4l2_capability cap;
  if (ioctl(context->fd, VIDIOC_QUERYCAP, &cap) == -1) {
    close(context->fd);
    SAFE_FREE(context);
    return SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to query V4L2 capabilities");
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    close(context->fd);
    SAFE_FREE(context);
    return SET_ERRNO(ERROR_WEBCAM, "Device is not a video capture device");
  }

  // Set format (try 640x480 first, fallback to whatever the device supports)
  if (webcam_v4l2_set_format(context, 640, 480) != 0) {
    close(context->fd);
    SAFE_FREE(context);
    return ERROR_WEBCAM;
  }

  // Initialize buffers
  if (webcam_v4l2_init_buffers(context) != 0) {
    close(context->fd);
    SAFE_FREE(context);
    return ERROR_WEBCAM;
  }

  // Start streaming
  if (webcam_v4l2_start_streaming(context) != 0) {
    // Cleanup buffers
    for (int i = 0; i < context->buffer_count; i++) {
      if (context->buffers[i].start != MAP_FAILED) {
        munmap(context->buffers[i].start, context->buffers[i].length);
      }
    }
    SAFE_FREE(context->buffers);
    close(context->fd);
    SAFE_FREE(context);
    return -1;
  }

  *ctx = context;
  log_info("V4L2 webcam initialized successfully on %s", device_path);
  return 0;
}

void webcam_flush_context(webcam_context_t *ctx) {
  if (!ctx)
    return;

  // Stop streaming to interrupt any blocking reads
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) == 0) {
    log_debug("V4L2 streaming stopped for flush");
    // Restart streaming so reads can continue if needed
    ioctl(ctx->fd, VIDIOC_STREAMON, &type);
  }
}

void webcam_cleanup_context(webcam_context_t *ctx) {
  if (!ctx)
    return;

  // Stop streaming
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);

  // Unmap buffers
  if (ctx->buffers) {
    for (int i = 0; i < ctx->buffer_count; i++) {
      if (ctx->buffers[i].start != MAP_FAILED) {
        munmap(ctx->buffers[i].start, ctx->buffers[i].length);
      }
    }
    SAFE_FREE(ctx->buffers);
  }

  close(ctx->fd);
  SAFE_FREE(ctx);
  log_info("V4L2 webcam cleaned up");
}

image_t *webcam_read_context(webcam_context_t *ctx) {
  if (!ctx)
    return NULL;

  struct v4l2_buffer buf = {0};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  // Dequeue a buffer with retry logic for transient errors
  int retry_count = 0;
  while (retry_count < WEBCAM_READ_RETRY_COUNT) {
    if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) == 0) {
      break; // Success
    }

    if (errno == EAGAIN) {
      return NULL; // No frame available - this is normal
    }

    retry_count++;
    if (retry_count >= WEBCAM_READ_RETRY_COUNT) {
      log_error("Failed to dequeue V4L2 buffer after %d retries: %s", retry_count, SAFE_STRERROR(errno));
      return NULL;
    }

    usleep(1000); // 1ms
  }

  // Validate buffer index to prevent crashes
  if (buf.index >= (unsigned int)ctx->buffer_count) {
    log_error("V4L2 returned invalid buffer index %u (max: %d)", buf.index, ctx->buffer_count - 1);
    return NULL;
  }

  // Validate buffer pointer
  if (!ctx->buffers || !ctx->buffers[buf.index].start) {
    log_error("V4L2 buffer %u not initialized (start=%p, buffers=%p)", buf.index,
              ctx->buffers ? ctx->buffers[buf.index].start : NULL, ctx->buffers);
    return NULL;
  }

  // Create image_t structure
  image_t *img = image_new(ctx->width, ctx->height);
  if (!img) {
    log_error("Failed to allocate image buffer");
    // Re-queue the buffer - use safe error handling
    if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
      log_error("Failed to re-queue buffer after image allocation failure: %s", SAFE_STRERROR(errno));
    }
    return NULL;
  }

  // Copy and convert frame data based on pixel format
  if (ctx->pixelformat == V4L2_PIX_FMT_YUYV) {
    // Convert YUYV to RGB24
    yuyv_to_rgb24(ctx->buffers[buf.index].start, (uint8_t *)img->pixels, ctx->width, ctx->height);
  } else {
    // RGB24 - direct copy
    const size_t frame_size = (size_t)ctx->width * ctx->height * 3;
    memcpy(img->pixels, ctx->buffers[buf.index].start, frame_size);
  }

  // Re-queue the buffer for future use
  if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
    log_error("Failed to re-queue V4L2 buffer %u: %s (fd=%d, type=%d, memory=%d)", buf.index, SAFE_STRERROR(errno),
              ctx->fd, buf.type, buf.memory);
    // Still return the image - the frame data was already copied
  }

  return img;
}

asciichat_error_t webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  if (!ctx || !width || !height)
    return ERROR_INVALID_PARAM;

  *width = ctx->width;
  *height = ctx->height;
  return ASCIICHAT_OK;
}

asciichat_error_t webcam_list_devices(webcam_device_info_t **out_devices, unsigned int *out_count) {
  if (!out_devices || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "webcam_list_devices: invalid parameters");
  }

  *out_devices = NULL;
  *out_count = 0;

  // First pass: count valid video devices
  unsigned int device_count = 0;
  for (int i = 0; i <= WEBCAM_DEVICE_INDEX_MAX; i++) {
    char device_path[32];
    snprintf(device_path, sizeof(device_path), "/dev/video%d", i);

    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
      continue;
    }

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 && (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
      device_count++;
    }
    close(fd);
  }

  if (device_count == 0) {
    // No devices found - not an error
    return ASCIICHAT_OK;
  }

  // Allocate device array
  webcam_device_info_t *devices = SAFE_CALLOC(device_count, sizeof(webcam_device_info_t), webcam_device_info_t *);
  if (!devices) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate device info array");
  }

  // Second pass: populate device info
  unsigned int idx = 0;
  for (int i = 0; i <= WEBCAM_DEVICE_INDEX_MAX && idx < device_count; i++) {
    char device_path[32];
    snprintf(device_path, sizeof(device_path), "/dev/video%d", i);

    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
      continue;
    }

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 && (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
      devices[idx].index = (unsigned int)i;
      SAFE_STRNCPY(devices[idx].name, (const char *)cap.card, WEBCAM_DEVICE_NAME_MAX);
      idx++;
    }
    close(fd);
  }

  *out_devices = devices;
  *out_count = idx;

  return ASCIICHAT_OK;
}

void webcam_free_device_list(webcam_device_info_t *devices) {
  SAFE_FREE(devices);
}

#endif // __linux__
