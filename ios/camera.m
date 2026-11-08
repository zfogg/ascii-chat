#import <AVFoundation/AVFoundation.h>

#include <ascii-chat/ios/camera.h>

#include <stdint.h>
#include <stdlib.h>

@interface AsciiChatCameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, assign) CVPixelBufferRef latestBuffer;
@end

@implementation AsciiChatCameraDelegate

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
  (void)output;
  (void)connection;
  CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!buffer) return;
  CVPixelBufferRetain(buffer);
  @synchronized(self) {
    if (_latestBuffer) CVPixelBufferRelease(_latestBuffer);
    _latestBuffer = buffer;
  }
}

- (void)dealloc {
  if (_latestBuffer) CVPixelBufferRelease(_latestBuffer);
}

@end

static AVCaptureSession *g_session;
static AsciiChatCameraDelegate *g_delegate;

int asciichat_ios_camera_start(unsigned short device_index) {
  if (g_session) return 1;
  NSArray<AVCaptureDevice *> *devices =
      [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
  if (device_index >= devices.count) return 0;

  AVCaptureDevice *device = devices[device_index];
  AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:nil];
  if (!input) return 0;

  AVCaptureSession *session = [AVCaptureSession new];
  session.sessionPreset = AVCaptureSessionPreset640x480;
  if (![session canAddInput:input]) return 0;
  [session addInput:input];

  AVCaptureVideoDataOutput *output = [AVCaptureVideoDataOutput new];
  output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey:
                               @(kCVPixelFormatType_32BGRA)};
  dispatch_queue_t queue = dispatch_queue_create("com.ascii-chat.camera", DISPATCH_QUEUE_SERIAL);
  g_delegate = [AsciiChatCameraDelegate new];
  [output setSampleBufferDelegate:g_delegate queue:queue];
  if (![session canAddOutput:output]) return 0;
  [session addOutput:output];
  g_session = session;
  [session startRunning];
  return 1;
}

int asciichat_ios_camera_read(uint32_t *pixels, size_t capacity, size_t *width, size_t *height) {
  if (!g_delegate || !pixels || !width || !height) return 0;
  @synchronized(g_delegate) {
    CVPixelBufferRef buffer = g_delegate.latestBuffer;
    if (!buffer) return 0;
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    size_t w = CVPixelBufferGetWidth(buffer), h = CVPixelBufferGetHeight(buffer);
    if (capacity < w * h) {
      CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
      return 0;
    }
    const uint8_t *src = CVPixelBufferGetBaseAddress(buffer);
    size_t stride = CVPixelBufferGetBytesPerRow(buffer);
    for (size_t y = 0; y < h; ++y) {
      const uint8_t *row = src + y * stride;
      for (size_t x = 0; x < w; ++x) {
        const uint8_t *p = row + x * 4;
        pixels[y * w + x] = 0xFF000000U | ((uint32_t)p[2] << 16) |
                            ((uint32_t)p[1] << 8) | p[0];
      }
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    *width = w; *height = h;
    return 1;
  }
}

int asciichat_ios_camera_read_ascii(char *output, size_t capacity, size_t columns,
                                    size_t rows, size_t *length) {
  static const char palette[] = " .:-=+*#%@";
  if (!output || !length || !columns || !rows) return 0;
  size_t required = rows * (columns + 1) + 1;
  if (capacity < required) return 0;
  size_t max_pixels = 1920 * 1080;
  uint32_t *pixels = calloc(max_pixels, sizeof(*pixels));
  if (!pixels) return 0;
  size_t w = 0, h = 0;
  int ok = asciichat_ios_camera_read(pixels, max_pixels, &w, &h);
  if (ok) {
    size_t cursor = 0;
    for (size_t row = 0; row < rows; ++row) {
      size_t y = row * h / rows;
      for (size_t column = 0; column < columns; ++column) {
        size_t x = column * w / columns;
        uint32_t pixel = pixels[y * w + x];
        unsigned r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
        unsigned luminance = (77 * r + 150 * g + 29 * b) / 256;
        output[cursor++] = palette[luminance * (sizeof(palette) - 2) / 255];
      }
      output[cursor++] = '\n';
    }
    output[cursor] = '\0'; *length = cursor;
  }
  free(pixels);
  return ok;
}

void asciichat_ios_camera_stop(void) {
  [g_session stopRunning];
  g_session = nil;
  g_delegate = nil;
}
