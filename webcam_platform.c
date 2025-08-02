#include "webcam_platform.h"
#include "common.h"

// Platform detection
webcam_platform_type_t webcam_get_platform(void) {
#ifdef __linux__
    return WEBCAM_PLATFORM_V4L2;
#elif defined(__APPLE__)
    return WEBCAM_PLATFORM_AVFOUNDATION;
#else
    return WEBCAM_PLATFORM_UNKNOWN;
#endif
}

const char* webcam_platform_name(webcam_platform_type_t platform) {
    switch (platform) {
        case WEBCAM_PLATFORM_V4L2:
            return "V4L2 (Linux)";
        case WEBCAM_PLATFORM_AVFOUNDATION:
            return "AVFoundation (macOS)";
        case WEBCAM_PLATFORM_UNKNOWN:
        default:
            return "Unknown";
    }
}

// Wrapper functions that call platform-specific implementations
// These are implemented in the platform-specific files (webcam_v4l2.c, webcam_avfoundation.m)
// and will be linked in based on the current platform

#if !defined(__linux__) && !defined(__APPLE__)
// Fallback implementations for unsupported platforms
int webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index) {
    (void)ctx;
    (void)device_index;
    log_error("Webcam platform not supported on this system");
    return -1;
}

void webcam_platform_cleanup(webcam_context_t *ctx) {
    (void)ctx;
    log_warn("Webcam cleanup called on unsupported platform");
}

image_t *webcam_platform_read(webcam_context_t *ctx) {
    (void)ctx;
    log_error("Webcam read not supported on this platform");
    return NULL;
}

int webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
    (void)ctx;
    (void)width;
    (void)height;
    log_error("Webcam get dimensions not supported on this platform");
    return -1;
}
#endif