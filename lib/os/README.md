# Operating System Abstraction Layer

## Overview

The OS abstraction layer provides unified, cross-platform APIs for hardware device access and media operations that are essential to ASCII-Chat's real-time video chat functionality. This layer abstracts platform-specific media APIs (webcam capture, audio I/O) into a common interface, enabling ASCII-Chat to seamlessly capture video and audio across Windows, Linux, and macOS.

Unlike the platform abstraction layer which handles system primitives (threads, sockets, etc.), the OS layer focuses on multimedia hardware interaction and real-time media processing.

## Architecture

### File Structure

```
lib/os/
├── README.md              # This file
├── audio.h                # Audio capture/playback interface
├── audio.c                # Audio implementation using PortAudio
├── webcam.h               # Webcam capture interface
├── webcam.c               # Common webcam abstraction layer
├── linux/                 # Linux-specific implementations
│   ├── audio_alsa.c       # ALSA audio backend (future)
│   └── webcam_v4l2.c      # Video4Linux2 webcam implementation
├── macos/                 # macOS-specific implementations
│   ├── audio_coreaudio.c  # Core Audio backend (future)
│   └── webcam_avfoundation.m  # AVFoundation webcam implementation
└── windows/               # Windows-specific implementations
    ├── audio_wasapi.c     # WASAPI audio backend (future)
    └── webcam_mediafoundation.c  # Media Foundation webcam implementation
```

### Key Components

#### Core Headers

1. **audio.h** - Audio capture and playback interface
   - PortAudio-based implementation (cross-platform)
   - Real-time audio streaming with ring buffers
   - Thread-safe audio context management
   - Platform-specific real-time priority settings

2. **webcam.h** - Webcam capture interface
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

## API Categories

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

### Audio I/O (`audio.h`)

**Audio Context Management:**
```c
typedef struct {
  PaStream *input_stream;
  PaStream *output_stream;
  audio_ring_buffer_t *capture_buffer;
  audio_ring_buffer_t *playback_buffer;
  bool initialized;
  bool recording;
  bool playing;
  mutex_t state_mutex;
} audio_context_t;

int audio_init(audio_context_t *ctx);
void audio_destroy(audio_context_t *ctx);
```

**Audio Capture/Playback:**
```c
int audio_start_capture(audio_context_t *ctx);
int audio_stop_capture(audio_context_t *ctx);
int audio_start_playback(audio_context_t *ctx);
int audio_stop_playback(audio_context_t *ctx);
```

**Audio Data Transfer:**
```c
int audio_read_samples(audio_context_t *ctx, float *buffer, int num_samples);
int audio_write_samples(audio_context_t *ctx, const float *buffer, int num_samples);
```

**Real-time Priority:**
```c
int audio_set_realtime_priority(void);  // Platform-specific thread priority boost
```

**Ring Buffer Operations:**
```c
audio_ring_buffer_t *audio_ring_buffer_create(void);
void audio_ring_buffer_destroy(audio_ring_buffer_t *rb);
int audio_ring_buffer_write(audio_ring_buffer_t *rb, const float *data, int samples);
int audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, int samples);
int audio_ring_buffer_available_read(audio_ring_buffer_t *rb);
int audio_ring_buffer_available_write(audio_ring_buffer_t *rb);
```

## Platform-Specific Features

### Linux Specifics

**V4L2 Webcam Support:**
- Direct kernel interface through `/dev/video*` devices
- Memory-mapped I/O for zero-copy frame capture
- YUYV format with software RGB conversion
- Requires user to be in 'video' group
- Multiple device support through enumeration

**Audio (PortAudio):**
- ALSA backend for low-latency audio
- Real-time thread priority via `sched_setscheduler()`
- PulseAudio compatibility layer

### macOS Specifics

**AVFoundation Webcam Support:**
- Native Objective-C implementation
- Automatic format conversion
- Hardware acceleration when available
- System permission dialogs for camera access
- Retina display awareness

**Audio (PortAudio):**
- Core Audio backend
- Real-time thread priority via Mach thread policies
- Low-latency audio with small buffer sizes

### Windows Specifics

**Media Foundation Webcam Support:**
- COM-based initialization
- Automatic format negotiation
- YUY2/RGB conversion
- Fallback test pattern for missing cameras
- Device enumeration through Windows APIs

**Audio (PortAudio):**
- WASAPI backend for low-latency
- MME fallback for compatibility
- DirectSound support

## Audio Configuration

### Constants
```c
#define AUDIO_SAMPLE_RATE 44100         // CD-quality sample rate
#define AUDIO_FRAMES_PER_BUFFER 256     // Low latency buffer size
#define AUDIO_CHANNELS 1                // Mono audio
#define AUDIO_BUFFER_SIZE (AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS)
```

### Ring Buffer
The audio system uses lock-free ring buffers for thread-safe audio data transfer between capture/playback threads and the main application. Ring buffer size is defined in `ringbuffer.h`.

## Usage Examples

### Basic Webcam Capture
```c
#include "os/webcam.h"
#include "image2ascii/image.h"

int main() {
    // Initialize webcam (device 0)
    if (webcam_init(0) != 0) {
        fprintf(stderr, "Failed to initialize webcam\n");
        return 1;
    }

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

### Audio Capture and Playback
```c
#include "os/audio.h"

int main() {
    audio_context_t audio_ctx = {0};

    // Initialize audio system
    if (audio_init(&audio_ctx) != 0) {
        fprintf(stderr, "Failed to initialize audio\n");
        return 1;
    }

    // Set real-time priority for low latency
    audio_set_realtime_priority();

    // Start capture and playback
    audio_start_capture(&audio_ctx);
    audio_start_playback(&audio_ctx);

    // Audio loop
    float buffer[AUDIO_BUFFER_SIZE];
    while (running) {
        // Read from microphone
        int samples = audio_read_samples(&audio_ctx, buffer, AUDIO_BUFFER_SIZE);

        // Process audio...

        // Write to speakers
        audio_write_samples(&audio_ctx, buffer, samples);
    }

    // Cleanup
    audio_stop_capture(&audio_ctx);
    audio_stop_playback(&audio_ctx);
    audio_destroy(&audio_ctx);
    return 0;
}
```

### Platform-Specific Webcam Access
```c
#include "os/webcam.h"

int main() {
    webcam_context_t *ctx = NULL;

    // Platform-specific initialization
    if (webcam_init_context(&ctx, 0) != 0) {
        fprintf(stderr, "Failed to initialize platform webcam\n");
        return 1;
    }

    // Get dimensions
    int width, height;
    webcam_get_dimensions(ctx, &width, &height);
    printf("Webcam resolution: %dx%d\n", width, height);

    // Capture loop
    while (running) {
        image_t *frame = webcam_read_context(ctx);
        if (frame) {
            // Process frame...
            image_free(frame);
        }
    }

    // Cleanup
    webcam_cleanup_context(ctx);
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

# Audio uses PortAudio for all platforms
set(AUDIO_SOURCES lib/os/audio.c)
```

### Dependencies
- **All platforms**: PortAudio for audio I/O
- **Linux**: V4L2 development headers (`libv4l-dev`)
- **macOS**: AVFoundation framework (built-in)
- **Windows**: Media Foundation (built-in)

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

### Audio Errors
PortAudio provides standardized error codes across all platforms:
- Device not found
- Format not supported
- Buffer underrun/overrun
- Stream errors

## Testing

### Unit Tests
- `tests/unit/audio_test.c` - Audio subsystem tests
- `tests/unit/webcam_test.c` - Webcam interface tests

### Integration Tests
- `tests/integration/video_pipeline_test.c` - End-to-end video capture
- Audio streaming tests with real devices

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

### Audio (All Platforms)
- Fixed sample rate (44.1 kHz)
- Mono audio only
- Latency depends on system audio stack
- Real-time priority requires elevated privileges on some systems

## Future Enhancements

### Planned Features
- [ ] Native audio backends (ALSA, Core Audio, WASAPI)
- [ ] Multi-channel audio support
- [ ] Hardware video encoding/decoding
- [ ] Camera hot-plug detection
- [ ] Audio device selection UI
- [ ] Video resolution selection
- [ ] Frame rate control
- [ ] Audio effects processing

### Platform-Specific Plans
- **Linux**: PipeWire support for modern distributions
- **macOS**: Metal Performance Shaders for GPU acceleration
- **Windows**: DirectShow fallback for older systems

## Migration from Direct Platform Calls

When migrating code to use the OS abstraction:

1. **Replace OpenCV calls:**
   - `cv::VideoCapture` → `webcam_init()` / `webcam_read()`
   - Direct V4L2 calls → `webcam_*` functions

2. **Replace audio library calls:**
   - Direct ALSA/Core Audio → `audio_*` functions
   - Custom ring buffers → `audio_ring_buffer_*` functions

3. **Replace webcam platform calls:**
   - `webcam_platform_*` functions → `webcam_*` functions

4. **Update error handling:**
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

### Optional Libraries
- **ALSA**: Advanced Linux Sound Architecture (future)
- **Core Audio**: macOS audio framework (future)
- **WASAPI**: Windows Audio Session API (future)

## Author

OS abstraction layer developed by Zachary Fogg <me@zfo.gg>
September 2025
