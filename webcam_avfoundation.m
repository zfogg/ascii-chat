#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <dispatch/dispatch.h>

#include "webcam_platform.h"
#include "common.h"

// AVFoundation timeout configuration
#define AVFOUNDATION_FRAME_TIMEOUT_NS 500000000  // 500ms timeout (adjustable for slow cameras)
#define AVFOUNDATION_INIT_TIMEOUT_NS (2 * NSEC_PER_SEC)  // 2 second timeout for initialization

@interface WebcamCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, strong) dispatch_semaphore_t frameSemaphore;
@property (nonatomic) CVPixelBufferRef currentFrame;
@property (nonatomic) int frameWidth;
@property (nonatomic) int frameHeight;
@property (nonatomic) BOOL isActive;
@property (nonatomic) BOOL hasNewFrame;
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
    }
    return self;
}

- (void)dealloc {
    if (_currentFrame) {
        CVPixelBufferRelease(_currentFrame);
    }
    [super dealloc];
}

- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    // Check if delegate is still active to prevent callbacks during cleanup
    if (!self.isActive) {
        return;
    }
    
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;
    
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
};

int webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index) {
    webcam_context_t *context = malloc(sizeof(webcam_context_t));
    if (!context) {
        log_error("Failed to allocate webcam context");
        return -1;
    }
    
    memset(context, 0, sizeof(webcam_context_t));
    
    @autoreleasepool {
        // Create capture session
        context->session = [[AVCaptureSession alloc] init];
        if (!context->session) {
            log_error("Failed to create AVCaptureSession");
            free(context);
            return -1;
        }
        
        // Set session preset for quality
        [context->session setSessionPreset:AVCaptureSessionPreset640x480];
        
        // Find camera device using newer discovery session API
        AVCaptureDevice *device = nil;
        AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
            mediaType:AVMediaTypeVideo
            position:AVCaptureDevicePositionUnspecified];
        
        NSArray *devices = discoverySession.devices;
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
            free(context);
            return -1;
        }
        
        // Create device input
        NSError *error = nil;
        context->input = [[AVCaptureDeviceInput alloc] initWithDevice:device error:&error];
        if (!context->input || error) {
            log_error("Failed to create device input: %s", [[error localizedDescription] UTF8String]);
            [context->session release];
            free(context);
            return -1;
        }
        
        // Add input to session
        if ([context->session canAddInput:context->input]) {
            [context->session addInput:context->input];
        } else {
            log_error("Cannot add input to capture session");
            [context->input release];
            [context->session release];
            free(context);
            return -1;
        }
        
        // Create video output
        context->output = [[AVCaptureVideoDataOutput alloc] init];
        if (!context->output) {
            log_error("Failed to create video output");
            [context->input release];
            [context->session release];
            free(context);
            return -1;
        }
        
        // Configure video output for RGB format
        NSDictionary *videoSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_24RGB)
        };
        [context->output setVideoSettings:videoSettings];
        
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
            free(context);
            return -1;
        }
        
        // Start the session
        [context->session startRunning];
        
        // Wait for first frame to get dimensions
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, AVFOUNDATION_INIT_TIMEOUT_NS);
        if (dispatch_semaphore_wait(context->delegate.frameSemaphore, timeout) == 0) {
            context->width = context->delegate.frameWidth;
            context->height = context->delegate.frameHeight;
            log_info("AVFoundation webcam initialized: %dx%d", context->width, context->height);
        } else {
            log_error("Timeout waiting for first frame");
            [context->session stopRunning];
            [context->delegate release];
            [context->output release];
            [context->input release];
            [context->session release];
            dispatch_release(context->queue);
            free(context);
            return -1;
        }
    }
    
    *ctx = context;
    return 0;
}

void webcam_platform_cleanup(webcam_context_t *ctx) {
    if (!ctx) return;
    
    @autoreleasepool {
        // Stop the session first to prevent new callbacks
        if (ctx->session && [ctx->session isRunning]) {
            [ctx->session stopRunning];
            
            // Wait a bit for any pending callbacks to complete
            usleep(100000); // 100ms
        }
        
        // Deactivate delegate to prevent callbacks during cleanup
        if (ctx->delegate) {
            [ctx->delegate deactivate];
        }
        
        // Remove delegate to prevent callbacks during cleanup
        if (ctx->output && ctx->delegate) {
            [ctx->output setSampleBufferDelegate:nil queue:nil];
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
    
    free(ctx);
    log_info("AVFoundation webcam cleaned up");
}

image_t *webcam_platform_read(webcam_context_t *ctx) {
    if (!ctx || !ctx->delegate || !ctx->delegate.isActive) return NULL;
    
    @autoreleasepool {
        // Wait for a frame (configurable timeout for different camera speeds)
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, AVFOUNDATION_FRAME_TIMEOUT_NS);
        if (dispatch_semaphore_wait(ctx->delegate.frameSemaphore, timeout) != 0) {
            return NULL; // No frame available within timeout
        }
        
        // Reset the new frame flag so we can get the next semaphore signal
        ctx->delegate.hasNewFrame = NO;
        
        CVPixelBufferRef pixelBuffer = ctx->delegate.currentFrame;
        if (!pixelBuffer) return NULL;
        
        // Lock the pixel buffer
        if (CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
            log_error("Failed to lock pixel buffer");
            return NULL;
        }
        
        // Get buffer info
        size_t width = CVPixelBufferGetWidth(pixelBuffer);
        size_t height = CVPixelBufferGetHeight(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
        uint8_t *baseAddress = (uint8_t*)CVPixelBufferGetBaseAddress(pixelBuffer);
        
        if (!baseAddress) {
            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            log_error("Failed to get pixel buffer base address");
            return NULL;
        }
        
        // Create image_t structure
        image_t *img = image_new((int)width, (int)height);
        if (!img) {
            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            log_error("Failed to allocate image buffer");
            return NULL;
        }
        
        // Copy pixel data (handle potential row padding)
        if (bytesPerRow == width * 3) {
            // No padding, direct copy
            memcpy(img->pixels, baseAddress, width * height * 3);
        } else {
            // Copy row by row to handle padding
            for (size_t y = 0; y < height; y++) {
                memcpy(&img->pixels[y * width], &baseAddress[y * bytesPerRow], width * 3);
            }
        }
        
        // Unlock the pixel buffer
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        
        return img;
    }
}

int webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
    if (!ctx || !width || !height) return -1;
    
    *width = ctx->width;
    *height = ctx->height;
    return 0;
}

#endif // __APPLE__