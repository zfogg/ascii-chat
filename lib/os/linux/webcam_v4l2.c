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
  webcam_buffer_t *buffers;
  int buffer_count;
};

static int webcam_v4l2_set_format(webcam_context_t *ctx, int width, int height) {
  struct v4l2_format fmt = {0};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
  fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

  if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == -1) {
    log_error("Failed to set V4L2 format: %s", strerror(errno));
    return -1;
  }

  ctx->width = fmt.fmt.pix.width;
  ctx->height = fmt.fmt.pix.height;

  log_info("V4L2 format set to %dx%d", ctx->width, ctx->height);
  return 0;
}

static int webcam_v4l2_init_buffers(webcam_context_t *ctx) {
  struct v4l2_requestbuffers req = {0};
  req.count = WEBCAM_BUFFER_COUNT_DEFAULT;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) == -1) {
    log_error("Failed to request V4L2 buffers: %s", strerror(errno));
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
      log_error("Failed to query buffer %d: %s", i, strerror(errno));
      return -1;
    }

    ctx->buffers[i].length = buf.length;
    ctx->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buf.m.offset);

    if (ctx->buffers[i].start == MAP_FAILED) {
      log_error("Failed to mmap buffer %d: %s", i, strerror(errno));
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
      log_error("Failed to queue buffer %d: %s", i, strerror(errno));
      return -1;
    }
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) == -1) {
    log_error("Failed to start V4L2 streaming: %s", strerror(errno));
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
      return SET_ERRNO(ERROR_WEBCAM_IN_USE,
                      "V4L2 device %s is already in use by another application.",
                      device_path);
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
      log_error("Failed to dequeue V4L2 buffer after %d retries: %s", retry_count, strerror(errno));
      return NULL;
    }

    usleep(1000); // 1ms
  }

  // Create image_t structure
  image_t *img = image_new(ctx->width, ctx->height);
  if (!img) {
    log_error("Failed to allocate image buffer");
    // Re-queue the buffer
    ioctl(ctx->fd, VIDIOC_QBUF, &buf);
    return NULL;
  }

  // Copy frame data (V4L2 RGB24 format matches our rgb_t structure)
  const size_t frame_size = (size_t)ctx->width * ctx->height * 3;
  memcpy(img->pixels, ctx->buffers[buf.index].start, frame_size);

  // Re-queue the buffer
  if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
    log_error("Failed to re-queue V4L2 buffer: %s", strerror(errno));
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

#endif // __linux__
