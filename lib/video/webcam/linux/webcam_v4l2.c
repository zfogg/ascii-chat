/**
 * @file video/webcam/linux/webcam_v4l2.c
 * @ingroup webcam
 * @brief 📷 Linux V4L2 webcam capture with multi-format support
 *
 * Supports RGB24 (native), NV12, I420, MJPEG (60fps), YUYV, and UYVY formats.
 * Uses libswscale for efficient format conversion to RGB24.
 * MJPEG frames are decompressed using FFmpeg's JPEG codec before conversion.
 */

#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/debug/named.h>
#include <stdatomic.h>

#define WEBCAM_BUFFER_COUNT_DEFAULT 4
#define WEBCAM_BUFFER_COUNT_MAX 8
#define WEBCAM_DEVICE_INDEX_MAX 99
#define WEBCAM_READ_RETRY_COUNT 3

// Module-scope cached frame (freed in webcam_cleanup_context when webcam closes)
static image_t *v4l2_cached_frame = NULL;

typedef struct {
  void *start;
  size_t length;
} webcam_buffer_t;

struct webcam_context_t {
  int fd;
  int width;
  int height;
  uint32_t pixelformat; // Actual pixel format from driver (RGB24, YUYV, NV12, I420, MJPEG, UYVY)
  webcam_buffer_t *buffers;
  int buffer_count;
  image_t *cached_frame;              // Reusable frame buffer (allocated once, reused for each read)
  struct SwsContext *sws_ctx;         // libswscale context for format conversion (if needed)
  enum AVPixelFormat av_pixel_format; // FFmpeg pixel format for swscale
  AVCodecContext *mjpeg_codec_ctx;    // MJPEG JPEG decompression context (if using MJPEG format)
  AVFrame *mjpeg_decoded_frame;       // Decoded JPEG frame buffer

  // Async camera reading (non-blocking)
  lifecycle_t async_lifecycle;             // Lifecycle state machine for camera thread
  asciichat_thread_t camera_thread;        // Background thread for continuous frame capture
  _Atomic(image_t *) latest_frame;         // Latest frame from camera (atomic swap)
  image_t *async_cached_frame;             // Last frame returned to caller (returned when no new frame available)
};

/**
 * @brief Background thread function for continuous camera frame capture
 *
 * Continuously reads frames from the camera and swaps them into the latest_frame buffer.
 * Uses lifecycle state machine to coordinate startup/shutdown safely.
 */
static void *webcam_camera_thread_func(void *arg) {
  webcam_context_t *ctx = (webcam_context_t *)arg;

  log_debug("Camera background thread started");

  while (lifecycle_is_initialized(&ctx->async_lifecycle)) {
    // Read frame (blocking on camera)
    image_t *frame = webcam_read_context(ctx);

    if (frame) {
      // Copy the frame data (webcam_read_context returns ptr to module-level static buffer)
      image_t *frame_copy = image_new_copy(frame);
      if (frame_copy) {
        // Atomic swap: replace latest_frame with new frame
        image_t *old_frame = atomic_exchange(&ctx->latest_frame, frame_copy);
        if (old_frame) {
          image_destroy(old_frame);
        }
      }
    }
  }

  log_debug("Camera background thread stopped");
  return NULL;
}

/**
 * @brief Initialize swscale context for format conversion
 *
 * Creates a libswscale context to convert from the source format to RGB24.
 * Returns 0 on success, -1 on failure.
 */
static int webcam_v4l2_init_swscale(webcam_context_t *ctx, enum AVPixelFormat src_fmt) {
  if (src_fmt == AV_PIX_FMT_NONE) {
    log_error("Invalid pixel format for swscale");
    return -1;
  }

  // Free existing context if present
  if (ctx->sws_ctx) {
    sws_freeContext(ctx->sws_ctx);
    ctx->sws_ctx = NULL;
  }

  // Create new swscale context for conversion to RGB24
  ctx->sws_ctx = sws_getContext(ctx->width, ctx->height, src_fmt, ctx->width, ctx->height, AV_PIX_FMT_RGB24,
                                SWS_BILINEAR, NULL, NULL, NULL);

  if (!ctx->sws_ctx) {
    log_error("Failed to create swscale context for format conversion");
    return -1;
  }

  ctx->av_pixel_format = src_fmt;
  return 0;
}

/**
 * @brief Initialize MJPEG (JPEG) decompression context
 *
 * Sets up FFmpeg's JPEG codec decoder for decompressing MJPEG frames from the camera.
 *
 * @param ctx The webcam context
 * @return int 0 on success, -1 on failure
 */
static int webcam_v4l2_init_mjpeg_decoder(webcam_context_t *ctx) {
  // Find JPEG codec
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
  if (!codec) {
    log_error("MJPEG codec not found in FFmpeg");
    return -1;
  }

  // Allocate codec context
  ctx->mjpeg_codec_ctx = avcodec_alloc_context3(codec);
  if (!ctx->mjpeg_codec_ctx) {
    log_error("Failed to allocate MJPEG codec context");
    return -1;
  }

  // Set expected frame dimensions (V4L2 tells us these)
  ctx->mjpeg_codec_ctx->width = ctx->width;
  ctx->mjpeg_codec_ctx->height = ctx->height;

  // Open codec
  if (avcodec_open2(ctx->mjpeg_codec_ctx, codec, NULL) < 0) {
    log_error("Failed to open MJPEG codec");
    avcodec_free_context(&ctx->mjpeg_codec_ctx);
    return -1;
  }

  // Allocate frame for decoded output
  ctx->mjpeg_decoded_frame = av_frame_alloc();
  if (!ctx->mjpeg_decoded_frame) {
    log_error("Failed to allocate MJPEG decoded frame");
    avcodec_free_context(&ctx->mjpeg_codec_ctx);
    return -1;
  }

  // Initialize swscale for converting decoded MJPEG (typically YUV420P) to RGB24
  if (webcam_v4l2_init_swscale(ctx, AV_PIX_FMT_YUV420P) != 0) {
    log_error("Failed to initialize swscale for MJPEG decoded frames");
    av_frame_free(&ctx->mjpeg_decoded_frame);
    avcodec_free_context(&ctx->mjpeg_codec_ctx);
    return -1;
  }

  return 0;
}

/**
 * @brief Set the format of the webcam
 *
 * Tries formats in this order:
 * 1. RGB24 (native, no conversion needed)
 * 2. NV12 (libswscale - Raspberry Pi, modern USB cameras)
 * 3. I420 (libswscale - planar YUV)
 * 4. MJPEG (FFmpeg JPEG decompression - supports 60fps on many cameras)
 * 5. YUYV (libswscale - YUV 4:2:2)
 * 6. UYVY (libswscale - YUV 4:2:2 variant)
 *
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
    ctx->sws_ctx = NULL;
    log_debug("V4L2 format set to RGB24 %dx%d", ctx->width, ctx->height);
    return 0;
  }

  // Try NV12 (Raspberry Pi cameras, modern USB cameras)
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
  if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_NV12) {
    ctx->pixelformat = V4L2_PIX_FMT_NV12;
    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    if (webcam_v4l2_init_swscale(ctx, AV_PIX_FMT_NV12) == 0) {
      log_debug("V4L2 format set to NV12 %dx%d (will convert to RGB with swscale)", ctx->width, ctx->height);
      return 0;
    }
  }

  // Try I420 (planar YUV)
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
  if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420) {
    ctx->pixelformat = V4L2_PIX_FMT_YUV420;
    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    if (webcam_v4l2_init_swscale(ctx, AV_PIX_FMT_YUV420P) == 0) {
      log_debug("V4L2 format set to I420 %dx%d (will convert to RGB with swscale)", ctx->width, ctx->height);
      return 0;
    }
  }

  // Try MJPEG (Motion-JPEG - compressed format supporting 60fps on many cameras)
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  int mjpeg_ret = ioctl(ctx->fd, VIDIOC_S_FMT, &fmt);
  if (mjpeg_ret == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
    ctx->pixelformat = V4L2_PIX_FMT_MJPEG;
    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    if (webcam_v4l2_init_mjpeg_decoder(ctx) == 0) {
      log_debug("V4L2 format set to MJPEG %dx%d (60fps compressed - will decompress JPEG with FFmpeg)", ctx->width, ctx->height);
      log_info("MJPEG format selected: pixelformat=0x%x", ctx->pixelformat);
      return 0;
    } else {
      log_warn("MJPEG decoder init failed, trying next format");
    }
  } else {
    log_debug("MJPEG format not supported (ioctl returned %d, got format 0x%x)", mjpeg_ret, fmt.fmt.pix.pixelformat);
  }

  // Try YUYV (YUV 4:2:2 - most webcams support this)
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
    ctx->pixelformat = V4L2_PIX_FMT_YUYV;
    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    if (webcam_v4l2_init_swscale(ctx, AV_PIX_FMT_YUYV422) == 0) {
      log_debug("V4L2 format set to YUYV %dx%d (will convert to RGB with swscale)", ctx->width, ctx->height);
      return 0;
    }
  }

  // Try UYVY (YUV 4:2:2 variant)
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
  if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_UYVY) {
    ctx->pixelformat = V4L2_PIX_FMT_UYVY;
    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;
    if (webcam_v4l2_init_swscale(ctx, AV_PIX_FMT_UYVY422) == 0) {
      log_debug("V4L2 format set to UYVY %dx%d (will convert to RGB with swscale)", ctx->width, ctx->height);
      return 0;
    }
  }

  // Save errno before log_error() clears it
  int saved_errno = errno;

  // Check if format setting failed because device is busy
  if (saved_errno == EBUSY) {
    log_error("Failed to set V4L2 format: device is busy (another application is using it)");
    errno = saved_errno;
    return -1;
  }

  log_error("Failed to set V4L2 format: device supports none of (RGB24, NV12, I420, YUYV, UYVY) (errno=%d: %s)",
            saved_errno, SAFE_STRERROR(saved_errno));
  errno = saved_errno;
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

  log_dev("V4L2 streaming started");
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
  context->fd = platform_open("webcam_device", device_path, PLATFORM_O_RDWR | O_NONBLOCK);
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

  // Set format (try 320x240 first for better performance, fallback to whatever the device supports)
  if (webcam_v4l2_set_format(context, 320, 240) != 0) {
    int saved_errno = errno; // Save errno before close() potentially changes it
    close(context->fd);
    SAFE_FREE(context);
    // If format setting failed because device is busy, return ERROR_WEBCAM_IN_USE
    if (saved_errno == EBUSY) {
      return SET_ERRNO(ERROR_WEBCAM_IN_USE, "V4L2 device %s is in use - cannot set format", device_path);
    }
    return SET_ERRNO(ERROR_WEBCAM, "Failed to set V4L2 format for device %s", device_path);
  }

  // Initialize buffers
  if (webcam_v4l2_init_buffers(context) != 0) {
    close(context->fd);
    SAFE_FREE(context);
    return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize V4L2 buffers for device %s", device_path);
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
    return SET_ERRNO(ERROR_WEBCAM, "Failed to start V4L2 streaming for device %s", device_path);
  }

  // Request target FPS via VIDIOC_S_PARM AFTER streaming starts
  // Some devices require streaming to be active before frame rate control works
  uint32_t target_fps = (uint32_t)GET_OPTION(fps);
  if (target_fps == 0) target_fps = 60;  // Default to 60 if not set

  struct v4l2_streamparm parm = {0};
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = target_fps;

  if (ioctl(context->fd, VIDIOC_S_PARM, &parm) == 0) {
    // Get back what the driver actually set
    if (ioctl(context->fd, VIDIOC_G_PARM, &parm) == 0) {
      int actual_fps = parm.parm.capture.timeperframe.denominator /
                      (parm.parm.capture.timeperframe.numerator ?
                       parm.parm.capture.timeperframe.numerator : 1);
      log_info("V4L2 frame rate set: requested %u FPS, got %d FPS", target_fps, actual_fps);
    }
  } else {
    log_debug("V4L2 device does not support VIDIOC_S_PARM (frame rate control)");
  }

  // Initialize async lifecycle for camera thread
  if (!lifecycle_init(&context->async_lifecycle, "webcam_camera")) {
    // Cleanup on lifecycle init failure
    for (int i = 0; i < context->buffer_count; i++) {
      if (context->buffers[i].start != MAP_FAILED) {
        munmap(context->buffers[i].start, context->buffers[i].length);
      }
    }
    SAFE_FREE(context->buffers);
    close(context->fd);
    SAFE_FREE(context);
    return SET_ERRNO(ERROR_INIT, "Failed to initialize camera thread lifecycle");
  }

  // Start background camera thread
  asciichat_error_t thread_err = asciichat_thread_create(&context->camera_thread, "webcam_camera",
                                                          webcam_camera_thread_func, context);
  if (thread_err != ASCIICHAT_OK) {
    lifecycle_shutdown(&context->async_lifecycle);
    for (int i = 0; i < context->buffer_count; i++) {
      if (context->buffers[i].start != MAP_FAILED) {
        munmap(context->buffers[i].start, context->buffers[i].length);
      }
    }
    SAFE_FREE(context->buffers);
    close(context->fd);
    SAFE_FREE(context);
    return thread_err;
  }

  *ctx = context;
  log_dev("V4L2 webcam initialized successfully on %s with async camera thread", device_path);

  /* Register webcam context with named registry */
  NAMED_REGISTER(context, device_path, "webcam", "0x%tx");

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

  NAMED_UNREGISTER(ctx);

  // Stop camera thread by shutting down lifecycle
  if (lifecycle_shutdown(&ctx->async_lifecycle)) {
    // This caller won the shutdown race, join the thread
    asciichat_thread_join(&ctx->camera_thread, NULL);
    log_debug("Camera background thread joined");
  }

  // Clean up any remaining frames in the atomic and cached buffers
  image_t *leftover_frame = atomic_exchange(&ctx->latest_frame, NULL);
  if (leftover_frame) {
    image_destroy(leftover_frame);
  }

  if (ctx->async_cached_frame) {
    image_destroy(ctx->async_cached_frame);
    ctx->async_cached_frame = NULL;
  }

  // Free swscale context if it was allocated
  if (ctx->sws_ctx) {
    sws_freeContext(ctx->sws_ctx);
    ctx->sws_ctx = NULL;
  }

  // Free MJPEG codec context if it was allocated
  if (ctx->mjpeg_codec_ctx) {
    avcodec_free_context(&ctx->mjpeg_codec_ctx);
    ctx->mjpeg_codec_ctx = NULL;
  }

  // Free MJPEG decoded frame if it was allocated
  if (ctx->mjpeg_decoded_frame) {
    av_frame_free(&ctx->mjpeg_decoded_frame);
    ctx->mjpeg_decoded_frame = NULL;
  }

  // Free cached frame if it was allocated
  if (v4l2_cached_frame) {
    image_destroy(v4l2_cached_frame);
    v4l2_cached_frame = NULL;
  }

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
  log_debug("V4L2 webcam cleaned up");
}

image_t *webcam_read_context(webcam_context_t *ctx) {
  if (!ctx)
    return NULL;

  struct v4l2_buffer buf = {0};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  // Use poll() to efficiently wait for frame available (500ms timeout)
  // At 30fps, frames arrive every ~33ms; at 60fps every ~16ms
  struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN};

  int poll_ret = poll(&pfd, 1, 500);  // 500ms timeout for slower cameras

  if (poll_ret < 0) {
    log_error("poll() failed on V4L2 device: %s", SAFE_STRERROR(errno));
    return NULL;
  }

  if (poll_ret == 0) {
    // Timeout - no frame available within 100ms
    return NULL;
  }

  // poll() returned > 0: data is available
  if (!(pfd.revents & POLLIN)) {
    // Some other event occurred (error/disconnect)
    log_error("V4L2 device error: poll revents=0x%x", pfd.revents);
    return NULL;
  }

  // Dequeue the buffer (should succeed now)
  if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) != 0) {
    log_error("Failed to dequeue V4L2 buffer after poll(): %s", SAFE_STRERROR(errno));
    return NULL;
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

  // Allocate or reallocate cached frame if needed
  if (!v4l2_cached_frame || v4l2_cached_frame->w != ctx->width || v4l2_cached_frame->h != ctx->height) {
    if (v4l2_cached_frame) {
      image_destroy(v4l2_cached_frame);
    }
    v4l2_cached_frame = image_new(ctx->width, ctx->height);
    if (!v4l2_cached_frame) {
      log_error("Failed to allocate image buffer");
      // Re-queue the buffer - use safe error handling
      if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        log_error("Failed to re-queue buffer after image allocation failure: %s", SAFE_STRERROR(errno));
      }
      return NULL;
    }
  }

  image_t *img = v4l2_cached_frame;

  // Handle MJPEG decompression if using MJPEG format
  if (ctx->pixelformat == V4L2_PIX_FMT_MJPEG) {
    if (!ctx->mjpeg_codec_ctx) {
      log_error("MJPEG codec context not initialized");
      if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        log_error("Failed to re-queue buffer after MJPEG codec error: %s", SAFE_STRERROR(errno));
      }
      return NULL;
    }


    // Create packet from MJPEG frame data
    AVPacket pkt = {0};
    av_new_packet(&pkt, buf.bytesused);
    memcpy(pkt.data, ctx->buffers[buf.index].start, buf.bytesused);

    // Send packet to decoder
    if (avcodec_send_packet(ctx->mjpeg_codec_ctx, &pkt) < 0) {
      log_warn_every(1000000000LL, "Failed to send MJPEG packet (size=%u) to decoder", buf.bytesused);
      av_packet_unref(&pkt);
      if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        log_error("Failed to re-queue buffer after MJPEG decode error: %s", SAFE_STRERROR(errno));
      }
      return NULL;
    }

    // Receive decoded frame (JPEG decompression happens here)
    if (avcodec_receive_frame(ctx->mjpeg_codec_ctx, ctx->mjpeg_decoded_frame) < 0) {
      log_warn_every(1000000000LL, "Failed to decode MJPEG frame from %u bytes", buf.bytesused);
      av_packet_unref(&pkt);
      if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        log_error("Failed to re-queue buffer after MJPEG receive error: %s", SAFE_STRERROR(errno));
      }
      return NULL;
    }

    av_packet_unref(&pkt);

    // Convert decoded frame to RGB24 using swscale
    if (!ctx->sws_ctx) {
      log_error("Swscale context not initialized for MJPEG decoded frame");
      if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        log_error("Failed to re-queue buffer after swscale init error: %s", SAFE_STRERROR(errno));
      }
      return NULL;
    }

    uint8_t *dst_data[1] = {(uint8_t *)img->pixels};
    int dst_linesize[1] = {ctx->width * 3};

    sws_scale(ctx->sws_ctx, (const uint8_t * const *)ctx->mjpeg_decoded_frame->data,
              ctx->mjpeg_decoded_frame->linesize, 0, ctx->height, dst_data, dst_linesize);

    // Re-queue the buffer for future use
    if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
      log_error("Failed to re-queue V4L2 buffer %u after MJPEG decode: %s", buf.index, SAFE_STRERROR(errno));
    }

    return img;
  }

  // Copy and convert frame data based on pixel format
  if (ctx->sws_ctx) {
    // Use libswscale for format conversion (NV12, I420, YUYV, UYVY)
    const uint8_t *src_data[1] = {(const uint8_t *)ctx->buffers[buf.index].start};
    int src_linesize[1] = {0};

    // Calculate source linesize based on format
    switch (ctx->pixelformat) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_YUV420:
      // For planar formats, linesize equals width
      src_linesize[0] = ctx->width;
      break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
      // For packed YUV 4:2:2, each pixel is 2 bytes
      src_linesize[0] = ctx->width * 2;
      break;
    default:
      src_linesize[0] = ctx->width;
    }

    uint8_t *dst_data[1] = {(uint8_t *)img->pixels};
    int dst_linesize[1] = {ctx->width * 3};

    sws_scale(ctx->sws_ctx, src_data, src_linesize, 0, ctx->height, dst_data, dst_linesize);
  } else {
    // RGB24 - direct copy with overflow checking
    size_t frame_size;
    if (image_calc_rgb_size((size_t)ctx->width, (size_t)ctx->height, &frame_size) != ASCIICHAT_OK) {
      log_error("Failed to calculate frame size: width=%d, height=%d (would overflow)", ctx->width, ctx->height);
      image_destroy(img);
      return NULL;
    }
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

/**
 * @brief Non-blocking async read of latest cached frame
 *
 * Returns the most recent frame from the background camera thread without blocking.
 * Always returns a cached frame except on startup before any frames have been captured.
 * Never returns NULL after the first frame is captured.
 *
 * The returned frame should NOT be freed by the caller (it's internal to the context).
 */
image_t *webcam_read_async(webcam_context_t *ctx) {
  if (!ctx || !lifecycle_is_initialized(&ctx->async_lifecycle)) {
    return NULL;
  }

  // Check if there's a new frame from the camera thread
  image_t *new_frame = atomic_exchange(&ctx->latest_frame, NULL);

  if (new_frame) {
    // New frame available - update cache and return it
    // (The background thread allocated this frame, we take ownership)
    if (ctx->async_cached_frame) {
      image_destroy(ctx->async_cached_frame);
    }
    ctx->async_cached_frame = new_frame;
    return new_frame;
  }

  // No new frame - return the last cached frame (smooth playback without gaps)
  return ctx->async_cached_frame;
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
    safe_snprintf(device_path, sizeof(device_path), "/dev/video%d", i);

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
    safe_snprintf(device_path, sizeof(device_path), "/dev/video%d", i);

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
