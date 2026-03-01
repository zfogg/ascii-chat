/**
 * @file video/webcam/macos/webcam_avfoundation.m
 * @ingroup webcam
 * @brief 📷 macOS AVFoundation webcam capture implementation with hardware acceleration
 */

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <dispatch/dispatch.h>

#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/image.h>

// AVFoundation timeout configuration
#define AVFOUNDATION_FRAME_TIMEOUT_NS 500000000         // 500ms timeout (adjustable for slow cameras)
#define AVFOUNDATION_INIT_TIMEOUT_NS (3 * NSEC_PER_SEC) // 3 second timeout for initialization

@interface WebcamCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) dispatch_semaphore_t frameSemaphore;
@property(nonatomic) CVPixelBufferRef currentFrame;
@property(nonatomic) int frameWidth;
@property(nonatomic) int frameHeight;
@property(nonatomic) BOOL isActive;
@property(nonatomic) BOOL hasNewFrame;
@property(nonatomic, strong) NSLock *frameLock;
@end

@implementation WebcamCaptureDelegate

- (instancetype)init {
  self = [super init];
  if (self) {
    _frameSemaphore = dispatch_semaphore_create(0);
    _currentFrame = NULL;
    _frameWidth = 0;
    _frameHeight = 0;
    _isActive = YES;
    _hasNewFrame = NO;
    _frameLock = [[NSLock alloc] init];
  }
  return self;
}

- (void)dealloc {
  [_frameLock lock];
  if (_currentFrame) {
    CVPixelBufferRelease(_currentFrame);
    _currentFrame = NULL;
  }
  [_frameLock unlock];
  [_frameLock release];
  // Release the dispatch semaphore (required under MRC for dispatch objects)
  if (_frameSemaphore) {
    dispatch_release(_frameSemaphore);
    _frameSemaphore = nil;
  }
  [super dealloc];
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
  // Check if delegate is still active to prevent callbacks during cleanup
  if (!self.isActive) {
    return;
  }

  CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pixelBuffer)
    return;

  [self.frameLock lock];

  // Replace old frame with new one
  CVPixelBufferRef oldFrame = self.currentFrame;
  self.currentFrame = CVPixelBufferRetain(pixelBuffer);

  if (oldFrame) {
    CVPixelBufferRelease(oldFrame);
  }

  // Update dimensions
  self.frameWidth = (int)CVPixelBufferGetWidth(pixelBuffer);
  self.frameHeight = (int)CVPixelBufferGetHeight(pixelBuffer);

  // Always signal semaphore when we get a new frame
  self.hasNewFrame = YES;

  [self.frameLock unlock];

  dispatch_semaphore_signal(self.frameSemaphore);
}

- (void)deactivate {
  self.isActive = NO;
}

@end

struct webcam_context_t {
  AVCaptureSession *session;
  AVCaptureDeviceInput *input;
  AVCaptureVideoDataOutput *output;
  WebcamCaptureDelegate *delegate;
  dispatch_queue_t queue;
  int width;
  int height;
  image_t *cached_frame;      // Reusable frame buffer (allocated once, reused for each read)
  OSType actual_pixel_format; // Track actual format from camera
};

// Helper function to get supported device types without deprecated ones
static NSArray *getSupportedDeviceTypes(void) {
  NSMutableArray *deviceTypes = [NSMutableArray array];

  // Always add built-in camera
  [deviceTypes addObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];

  // Add continuity camera support for macOS 13.0+
  if (@available(macOS 13.0, *)) {
    [deviceTypes addObject:AVCaptureDeviceTypeContinuityCamera];
  }

  // Add external camera support for macOS 14.0+
  if (@available(macOS 14.0, *)) {
    [deviceTypes addObject:AVCaptureDeviceTypeExternal];
  }

  return [[deviceTypes copy] autorelease];
}

asciichat_error_t webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
  webcam_context_t *context;
  context = SAFE_MALLOC(sizeof(webcam_context_t), webcam_context_t *);
  if (!context) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate webcam context");
  }

  memset(context, 0, sizeof(webcam_context_t));

  @autoreleasepool {
    // Create capture session
    context->session = [[AVCaptureSession alloc] init];
    if (!context->session) {
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to create AVCaptureSession");
    }

    // Set session preset for quality
    [context->session setSessionPreset:AVCaptureSessionPreset640x480];

    // Find camera device using newer discovery session API with proper device types
    AVCaptureDevice *device = nil;

    NSArray *deviceTypes = getSupportedDeviceTypes();
    AVCaptureDeviceDiscoverySession *discoverySession =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionUnspecified];

    NSArray *devices = discoverySession.devices;

    for (AVCaptureDevice *device in devices) {
      NSUInteger idx = [devices indexOfObject:device];
      log_debug("Found device %lu: %s (type: %s)", (unsigned long)idx, [device.localizedName UTF8String],
                [device.deviceType UTF8String]);
    }

    if (device_index < [devices count]) {
      device = [devices objectAtIndex:device_index];
    } else if ([devices count] > 0) {
      device = [devices objectAtIndex:0]; // Use first available camera
    }

    // Fallback to default device if discovery session fails
    if (!device) {
      device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    }

    if (!device) {
      log_error("No camera device found at index %d", device_index);
      [context->session release];
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam");
    }

    // Create device input
    NSError *error = nil;
    context->input = [[AVCaptureDeviceInput alloc] initWithDevice:device error:&error];
    if (!context->input || error) {
      log_error("Failed to create device input: %s", [[error localizedDescription] UTF8String]);
      [context->session release];
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam");
    }

    // Add input to session
    if ([context->session canAddInput:context->input]) {
      [context->session addInput:context->input];
    } else {
      log_error("Cannot add input to capture session");
      [context->input release];
      [context->session release];
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam");
    }

    // Create video output
    context->output = [[AVCaptureVideoDataOutput alloc] init];
    if (!context->output) {
      log_error("Failed to create video output");
      [context->input release];
      [context->session release];
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam");
    }

    // Configure video output for RGB format with fallback support
    // Try RGB24 first, then fallback to other common formats (UYVY, YUV420, etc)
    NSArray *preferredFormats = @[
      @(kCVPixelFormatType_24RGB),            // Preferred: 24-bit RGB
      @(kCVPixelFormatType_422YpCbCr8),       // UYVY - very common
      @(kCVPixelFormatType_420YpCbCr8Planar), // I420/YUV420 (3-plane)
      @(kCVPixelFormatType_32BGRA),           // 32-bit BGRA fallback
    ];

    NSDictionary *videoSettings = @{(NSString *)kCVPixelBufferPixelFormatTypeKey : preferredFormats};
    [context->output setVideoSettings:videoSettings];
    log_debug("Configured AVFoundation video output with format priorities: RGB24, UYVY, I420, BGRA");

    // Create delegate and queue
    context->delegate = [[WebcamCaptureDelegate alloc] init];
    context->queue = dispatch_queue_create("webcam_capture_queue", DISPATCH_QUEUE_SERIAL);

    [context->output setSampleBufferDelegate:context->delegate queue:context->queue];

    // Add output to session
    if ([context->session canAddOutput:context->output]) {
      [context->session addOutput:context->output];
    } else {
      log_error("Cannot add output to capture session");
      [context->delegate release];
      [context->output release];
      [context->input release];
      [context->session release];
      dispatch_release(context->queue);
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam");
    }

    // Start the session
    log_debug("Starting AVFoundation capture session...");
    [context->session startRunning];

    // Check if session is actually running
    if (![context->session isRunning]) {
      log_error("Failed to start capture session - session not running");
      [context->delegate release];
      [context->output release];
      [context->input release];
      [context->session release];
      dispatch_release(context->queue);
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam");
    }
    log_debug("Capture session started successfully");

    // Wait for first frame to get dimensions
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, AVFOUNDATION_INIT_TIMEOUT_NS);
    if (dispatch_semaphore_wait(context->delegate.frameSemaphore, timeout) == 0) {
      context->width = context->delegate.frameWidth;
      context->height = context->delegate.frameHeight;
      log_debug("AVFoundation webcam initialized: %dx%d, delegate isActive=%d", context->width, context->height,
                context->delegate.isActive);
    } else {
      log_error("Timeout waiting for first frame");
      [context->session stopRunning];
      [context->delegate release];
      [context->output release];
      [context->input release];
      [context->session release];
      dispatch_release(context->queue);
      SAFE_FREE(context);
      return SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam");
    }
  }

  *ctx = context;
  return ASCIICHAT_OK;
}

/**
 * @brief Convert pixel buffer data to RGB24 format
 *
 * Handles different CVPixelBuffer formats and converts to RGB24.
 * For RGB formats, does direct copy. For YUV formats, performs conversion.
 *
 * @return true if conversion succeeded, false otherwise
 */
static BOOL convert_pixel_buffer_to_rgb24(CVPixelBufferRef pixelBuffer, rgb_pixel_t *dest_pixels, int width,
                                          int height) {
  OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);

  if (CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
    log_error("Failed to lock pixel buffer for format 0x%x", pixelFormat);
    return FALSE;
  }

  uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(pixelBuffer);
  size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);

  BOOL success = FALSE;

  switch (pixelFormat) {
  case kCVPixelFormatType_24RGB:
    // Direct copy - already RGB24
    for (int y = 0; y < height; y++) {
      memcpy(&dest_pixels[y * width], &baseAddress[y * bytesPerRow], width * 3);
    }
    success = TRUE;
    break;

  case kCVPixelFormatType_32BGRA: {
    // Convert BGRA to RGB (skip alpha channel)
    for (int y = 0; y < height; y++) {
      uint8_t *src = &baseAddress[y * bytesPerRow];
      rgb_pixel_t *dst = &dest_pixels[y * width];
      for (int x = 0; x < width; x++) {
        dst[x].b = src[x * 4 + 0]; // B
        dst[x].g = src[x * 4 + 1]; // G
        dst[x].r = src[x * 4 + 2]; // R
        // Skip alpha at src[x*4 + 3]
      }
    }
    success = TRUE;
    break;
  }

  case kCVPixelFormatType_422YpCbCr8: {
    // UYVY format: U Y V Y (packed)
    for (int y = 0; y < height; y++) {
      uint8_t *src = &baseAddress[y * bytesPerRow];
      rgb_pixel_t *dst = &dest_pixels[y * width];

      for (int x = 0; x < width; x += 2) {
        int u = src[x * 2 + 0] - 128;
        int y0 = src[x * 2 + 1] - 16;
        int v = src[x * 2 + 2] - 128;
        int y1 = src[x * 2 + 3] - 16;

        // Convert YUV to RGB
        int r0 = (298 * y0 + 409 * v + 128) >> 8;
        int g0 = (298 * y0 - 100 * u - 208 * v + 128) >> 8;
        int b0 = (298 * y0 + 516 * u + 128) >> 8;

        dst[x].r = (uint8_t)(r0 < 0 ? 0 : (r0 > 255 ? 255 : r0));
        dst[x].g = (uint8_t)(g0 < 0 ? 0 : (g0 > 255 ? 255 : g0));
        dst[x].b = (uint8_t)(b0 < 0 ? 0 : (b0 > 255 ? 255 : b0));

        int r1 = (298 * y1 + 409 * v + 128) >> 8;
        int g1 = (298 * y1 - 100 * u - 208 * v + 128) >> 8;
        int b1 = (298 * y1 + 516 * u + 128) >> 8;

        dst[x + 1].r = (uint8_t)(r1 < 0 ? 0 : (r1 > 255 ? 255 : r1));
        dst[x + 1].g = (uint8_t)(g1 < 0 ? 0 : (g1 > 255 ? 255 : g1));
        dst[x + 1].b = (uint8_t)(b1 < 0 ? 0 : (b1 > 255 ? 255 : b1));
      }
    }
    success = TRUE;
    break;
  }

  case kCVPixelFormatType_420YpCbCr8Planar: {
    // I420 planar format
    size_t plane_width = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
    size_t plane_height = CVPixelBufferGetHeightOfPlane(pixelBuffer, 0);

    uint8_t *y_data = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
    uint8_t *u_data = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
    uint8_t *v_data = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 2);

    size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
    size_t u_stride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
    size_t v_stride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 2);

    for (int y = 0; y < (int)plane_height; y++) {
      rgb_pixel_t *dst = &dest_pixels[y * width];

      for (int x = 0; x < (int)plane_width; x++) {
        int y_val = y_data[y * y_stride + x] - 16;
        int u_val = u_data[(y / 2) * u_stride + (x / 2)] - 128;
        int v_val = v_data[(y / 2) * v_stride + (x / 2)] - 128;

        int r = (298 * y_val + 409 * v_val + 128) >> 8;
        int g = (298 * y_val - 100 * u_val - 208 * v_val + 128) >> 8;
        int b = (298 * y_val + 516 * u_val + 128) >> 8;

        dst[x].r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
        dst[x].g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
        dst[x].b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
      }
    }
    success = TRUE;
    break;
  }

  default:
    log_error("Unsupported pixel format from camera: 0x%x", pixelFormat);
    break;
  }

  CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

  return success;
}

void webcam_flush_context(webcam_context_t *ctx) {
  (void)ctx;
  // No-op on macOS - AVFoundation handles frame buffering internally
}

void webcam_cleanup_context(webcam_context_t *ctx) {
  if (!ctx)
    return;

  @autoreleasepool {
    // First, deactivate delegate to reject new callbacks
    if (ctx->delegate) {
      [ctx->delegate deactivate];
    }

    // Remove delegate to prevent any further callbacks
    if (ctx->output) {
      [ctx->output setSampleBufferDelegate:nil queue:nil];
    }

    // Now stop the session
    if (ctx->session && [ctx->session isRunning]) {
      [ctx->session stopRunning];

      // Wait longer for session to fully stop
      usleep(200000); // 200ms - give more time for cleanup
    }

    // Clean up session and associated objects
    if (ctx->session) {
      if (ctx->input && [ctx->session.inputs containsObject:ctx->input]) {
        [ctx->session removeInput:ctx->input];
      }
      if (ctx->output && [ctx->session.outputs containsObject:ctx->output]) {
        [ctx->session removeOutput:ctx->output];
      }
      [ctx->session release];
      ctx->session = nil;
    }

    if (ctx->input) {
      [ctx->input release];
      ctx->input = nil;
    }

    if (ctx->output) {
      [ctx->output release];
      ctx->output = nil;
    }

    if (ctx->delegate) {
      [ctx->delegate release];
      ctx->delegate = nil;
    }

    if (ctx->queue) {
      dispatch_release(ctx->queue);
      ctx->queue = nil;
    }
  }

  // Free cached frame if it was allocated
  if (ctx->cached_frame) {
    image_destroy(ctx->cached_frame);
    ctx->cached_frame = NULL;
  }

  SAFE_FREE(ctx);
  // log_info("AVFoundation webcam cleaned up");
}

image_t *webcam_read_context(webcam_context_t *ctx) {
  if (!ctx || !ctx->delegate || !ctx->delegate.isActive)
    return NULL;

  @autoreleasepool {
    // Wait for a frame (configurable timeout for different camera speeds)
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, AVFOUNDATION_FRAME_TIMEOUT_NS);
    if (dispatch_semaphore_wait(ctx->delegate.frameSemaphore, timeout) != 0) {
      log_error("No webcam frame available within timeout: %0.2f seconds",
                AVFOUNDATION_FRAME_TIMEOUT_NS / NSEC_PER_SEC);
      return NULL; // No frame available within timeout
    }

    // Reset the new frame flag so we can get the next semaphore signal
    ctx->delegate.hasNewFrame = NO;

    // Lock to safely access the current frame
    [ctx->delegate.frameLock lock];

    CVPixelBufferRef pixelBuffer = ctx->delegate.currentFrame;
    if (!pixelBuffer) {
      [ctx->delegate.frameLock unlock];
      return NULL;
    }

    // Retain the buffer while we're using it
    CVPixelBufferRetain(pixelBuffer);

    [ctx->delegate.frameLock unlock];

    // Lock the pixel buffer
    if (CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
      log_error("Failed to lock pixel buffer");
      CVPixelBufferRelease(pixelBuffer);
      return NULL;
    }

    // Get buffer info
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(pixelBuffer);

    if (!baseAddress) {
      CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
      CVPixelBufferRelease(pixelBuffer); // Release our retained reference
      log_error("Failed to get pixel buffer base address");
      return NULL;
    }

    // Allocate or reuse cached frame (allocated once, reused for each call)
    // This matches the documented API contract: "Subsequent calls reuse the same buffer"
    if (!ctx->cached_frame) {
      ctx->cached_frame = image_new((int)width, (int)height);
      if (!ctx->cached_frame) {
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pixelBuffer); // Release our retained reference
        log_error("Failed to allocate image buffer");
        return NULL;
      }
    }

    image_t *img = ctx->cached_frame;

    // Get actual pixel format from camera
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    if (ctx->actual_pixel_format != pixelFormat) {
      ctx->actual_pixel_format = pixelFormat;
      const char *format_name = "Unknown";
      switch (pixelFormat) {
      case kCVPixelFormatType_24RGB:
        format_name = "RGB24";
        break;
      case kCVPixelFormatType_32BGRA:
        format_name = "BGRA32";
        break;
      case kCVPixelFormatType_422YpCbCr8:
        format_name = "UYVY";
        break;
      case kCVPixelFormatType_420YpCbCr8Planar:
        format_name = "I420";
        break;
      }
      log_debug("Camera delivering pixels in %s format (0x%x)", format_name, pixelFormat);
    }

    // Convert pixel buffer to RGB24 (handles format conversion if needed)
    if (!convert_pixel_buffer_to_rgb24(pixelBuffer, img->pixels, (int)width, (int)height)) {
      CVPixelBufferRelease(pixelBuffer);
      log_error("Failed to convert pixel buffer to RGB24");
      return NULL;
    }

    // Release our retained reference
    CVPixelBufferRelease(pixelBuffer);

    return img;
  }
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

  @autoreleasepool {
    // Get all video capture devices using the helper that handles deprecated types
    NSArray *deviceTypes = getSupportedDeviceTypes();
    AVCaptureDeviceDiscoverySession *discoverySession =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionUnspecified];

    NSArray<AVCaptureDevice *> *av_devices = discoverySession.devices;
    NSUInteger device_count = [av_devices count];

    if (device_count == 0) {
      // No devices found - not an error
      return ASCIICHAT_OK;
    }

    // Allocate device array
    webcam_device_info_t *devices =
        SAFE_CALLOC((size_t)device_count, sizeof(webcam_device_info_t), webcam_device_info_t *);
    if (!devices) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate device info array");
    }

    // Populate device info
    for (NSUInteger i = 0; i < device_count; i++) {
      AVCaptureDevice *device = av_devices[i];
      devices[i].index = (unsigned int)i;

      const char *name = [[device localizedName] UTF8String];
      if (name) {
        SAFE_STRNCPY(devices[i].name, name, WEBCAM_DEVICE_NAME_MAX);
      } else {
        SAFE_STRNCPY(devices[i].name, "<Unknown>", WEBCAM_DEVICE_NAME_MAX);
      }
    }

    *out_devices = devices;
    *out_count = (unsigned int)device_count;
  }

  return ASCIICHAT_OK;
}

void webcam_free_device_list(webcam_device_info_t *devices) {
  SAFE_FREE(devices);
}

#endif // __APPLE__
