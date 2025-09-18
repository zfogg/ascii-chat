# OS Abstraction Layer - Webcam Support

## Overview

The OS abstraction layer provides unified, cross-platform APIs for webcam capture across Windows, Linux, and macOS. This layer abstracts platform-specific webcam APIs into a common interface, enabling ASCII-Chat to seamlessly capture video across all supported platforms.

Unlike the platform abstraction layer which handles system primitives (threads, sockets, etc.), this OS layer focuses specifically on webcam hardware interaction and video capture.

**Note**: Audio I/O is handled directly by PortAudio throughout the codebase since PortAudio already provides excellent cross-platform support without requiring additional abstraction.

## Architecture

### File Structure

```
lib/os/
├── README.md              # This file
├── webcam.h               # Webcam capture interface
├── webcam.c               # Common webcam abstraction layer
├── linux/                 # Linux-specific implementations
│   └── webcam_v4l2.c      # Video4Linux2 webcam implementation
├── macos/                 # macOS-specific implementations
│   └── webcam_avfoundation.m  # AVFoundation webcam implementation
└── windows/               # Windows-specific implementations
    └── webcam_mediafoundation.c  # Media Foundation webcam implementation
```

### Key Components

#### Core Headers

**webcam.h** - Webcam capture interface
- Unified API for video capture across platforms
- Platform-specific backend selection at compile time
- Image format conversion to common RGB format
- Device enumeration and selection

#### Platform Implementations

**Linux Implementation (linux/)**
- **webcam_v4l2.c**: Uses Video4Linux2 API for webcam access
  - Direct kernel interface for video capture
  - YUYV to RGB conversion
  - Memory-mapped I/O for efficient frame capture
  - Device capability querying

**macOS Implementation (macos/)**
- **webcam_avfoundation.m**: Uses AVFoundation framework
  - Objective-C implementation for native macOS support
  - Hardware-accelerated video capture
  - Automatic format conversion
  - Permission handling for camera access

**Windows Implementation (windows/)**
- **webcam_mediafoundation.c**: Uses Windows Media Foundation
  - COM-based video capture
  - Format negotiation and conversion
  - Device enumeration through Windows APIs
  - Fallback test pattern generation

## API Reference

### Webcam Capture (`webcam.h`)

**High-Level Interface (Backwards Compatible):**
```c
int webcam_init(unsigned short int webcam_index);
image_t *webcam_read(void);
void webcam_cleanup(void);
```

**Platform-Specific Interface:**
```c
typedef struct webcam_context_t webcam_context_t;  // Opaque handle

int webcam_init_context(webcam_context_t **ctx, unsigned short int device_index);
void webcam_cleanup_context(webcam_context_t *ctx);
image_t *webcam_read_context(webcam_context_t *ctx);
int webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height);
```

## Platform-Specific Features

### Linux Specifics

**V4L2 Webcam Support:**
- Direct kernel interface through `/dev/video*` devices
- Memory-mapped I/O for zero-copy frame capture
- YUYV format with software RGB conversion
- Requires user to be in 'video' group
- Multiple device support through enumeration


### macOS Specifics

**AVFoundation Webcam Support:**
- Native Objective-C implementation
- Automatic format conversion
- Hardware acceleration when available
- System permission dialogs for camera access
- Retina display awareness


### Windows Specifics

**Media Foundation Webcam Support:**
- COM-based initialization
- Automatic format negotiation
- YUY2/RGB conversion
- Fallback test pattern for missing cameras
- Device enumeration through Windows APIs


## Usage Examples

### Basic Webcam Capture
```c
#include "os/webcam.h"
#include "image2ascii/image.h"

int main() {
    webcam_context_t *ctx = NULL;

    // Initialize webcam (device 0)
    if (webcam_init_context(&ctx, 0) != 0) {
        fprintf(stderr, "Failed to initialize webcam\n");
        return 1;
    }

    int width, height;
    webcam_get_dimensions(ctx, &width, &height);
    printf("Webcam resolution: %dx%d\n", width, height);

    // Capture frames
    for (int i = 0; i < 100; i++) {
        image_t *frame = webcam_read();
        if (frame) {
            printf("Captured frame: %dx%d\n", frame->width, frame->height);
            // Process frame...
            image_free(frame);
        }
    }

    // Cleanup
    webcam_cleanup();
    return 0;
}
```

## Build Integration

The OS abstraction layer is integrated into the CMake build system:

### CMake Configuration
```cmake
# Platform-specific webcam sources are automatically selected
if(APPLE)
    set(WEBCAM_SOURCES lib/os/macos/webcam_avfoundation.m)
elseif(UNIX)
    set(WEBCAM_SOURCES lib/os/linux/webcam_v4l2.c)
elseif(WIN32)
    set(WEBCAM_SOURCES lib/os/windows/webcam_mediafoundation.c)
endif()

# Audio is handled directly by PortAudio without OS abstraction
```

### Dependencies
- **Linux**: V4L2 development headers (`libv4l-dev`)
- **macOS**: AVFoundation framework (built-in)
- **Windows**: Media Foundation (built-in)

**Note**: Audio dependencies (PortAudio) are managed at the main project level, not in this OS abstraction layer.

## Error Handling

### Webcam Errors
The webcam subsystem provides detailed platform-specific error messages:

**Linux:**
- Permission errors (user not in 'video' group)
- Device busy (camera in use by another application)
- Device not found (no `/dev/video*` devices)

**macOS:**
- Permission denied (camera access not granted)
- Device not found (no cameras available)
- Format negotiation failures

**Windows:**
- COM initialization failures
- Device enumeration errors
- Format conversion failures


## Testing

### Unit Tests
- `tests/unit/webcam_test.c` - Webcam interface tests (planned)

### Integration Tests
- `tests/integration/video_pipeline_test.c` - End-to-end video capture

### Platform Coverage
- CI/CD tests on Linux, macOS, and Windows
- Fallback test patterns for headless environments
- Mock devices for automated testing

## Known Limitations

### Linux
- V4L2 requires specific pixel formats (YUYV preferred)
- Some USB cameras may have compatibility issues
- User must be in 'video' group for device access

### macOS
- Requires user permission for camera access
- First-time permission dialog may interrupt flow
- Some virtual cameras may not be detected

### Windows
- Media Foundation requires Windows 7 or later
- Some older webcams may not support required formats
- COM initialization required before use
- Currently uses test pattern fallback (full implementation pending)


## Future Enhancements

### Planned Features
- [ ] Hardware video encoding/decoding
- [ ] Camera hot-plug detection
- [ ] Video resolution selection
- [ ] Frame rate control
- [ ] Multiple camera support

### Platform-Specific Plans
- **Linux**: PipeWire webcam support for modern distributions
- **macOS**: Metal Performance Shaders for GPU-accelerated processing
- **Windows**: DirectShow fallback for older systems

## Migration from Direct Platform Calls

When migrating code to use the OS abstraction:

1. **Replace OpenCV calls:**
   - `cv::VideoCapture` → `webcam_init()` / `webcam_read()`
   - Direct V4L2 calls → `webcam_*` functions


2. **Replace webcam platform calls:**
   - `webcam_platform_*` functions → `webcam_*` functions

3. **Update error handling:**
   - Check return codes from init functions
   - Handle platform-specific error messages
   - Implement graceful fallbacks

## Contributing

When adding new OS abstractions:

1. Define the interface in the appropriate header file
2. Implement platform-specific versions in `linux/`, `macos/`, `windows/`
3. Add fallback implementations for unsupported platforms
4. Write tests for all platforms
5. Update this README with new functionality
6. Ensure thread-safety for multi-threaded access
7. Document platform-specific requirements and limitations

## Dependencies

### Required Libraries
- **PortAudio**: Cross-platform audio I/O (all platforms)
- **libv4l2**: Video4Linux2 (Linux only)
- **AVFoundation**: Apple's media framework (macOS only, built-in)
- **Media Foundation**: Windows media framework (Windows only, built-in)


## Author

OS abstraction layer developed by Zachary Fogg <me@zfo.gg>
September 2025
