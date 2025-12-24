# Building WebRTC AEC3 for ascii-chat

This document explains how to build WebRTC from source for use with ascii-chat's echo cancellation features.

## Overview

ascii-chat integrates WebRTC's AEC3 (Acoustic Echo Cancellation v3) for high-quality echo suppression. This requires building WebRTC from source using the GN build system.

## Prerequisites

- **GN build system**: Required for WebRTC builds
- **Ninja**: Build tool used by GN/WebRTC
- **Clang/GCC**: C++ compiler
- **Python 3**: For GN bootstrap
- **Git**: For cloning repositories

## Setup Instructions

### 1. Install Dependencies

**macOS:**
```bash
# GN build system
git clone https://gn.googlesource.com/gn ~/.local/gn-src
cd ~/.local/gn-src
python3 build/gen.py
ninja -C out
ln -sf $(pwd)/out/gn /usr/local/bin/gn

# Ninja
brew install ninja
```

**Linux:**
```bash
# GN (build from source or package)
git clone https://gn.googlesource.com/gn ~/.local/gn-src
cd ~/.local/gn-src
python3 build/gen.py
ninja -C out
sudo ln -sf $(pwd)/out/gn /usr/local/bin/gn

# Ninja
sudo apt install ninja-build
```

###  2. Build WebRTC

```bash
cd /path/to/ascii-chat

# Build WebRTC from source
./scripts/build-webrtc.sh --debug
# or for Release build:
./scripts/build-webrtc.sh --release
```

This will:
1. Clone WebRTC source if not already present (large download)
2. Generate build files with GN
3. Build audio-only WebRTC library with ninja (10-30 minutes)
4. Store output at `.deps-cache/Debug/webrtc-src/build-Debug/obj/libwebrtc.a`

### 3. Configure and Build ascii-chat

```bash
# Configure CMake (will find WebRTC library)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build ascii-chat with WebRTC AEC3
cmake --build build
```

## Build Times

- **First GN setup**: 2-5 minutes (Python bootstrap)
- **First WebRTC build**: 20-45 minutes (full compilation)
- **Subsequent builds**: 5-15 minutes (incremental)

WebRTC is quite large (~2-3GB of source), so the first clone and build will take time.

## Troubleshooting

### GN not found
```bash
# Build GN from source
git clone https://gn.googlesource.com/gn
cd gn
python3 build/gen.py
ninja -C out
./out/gn --version
```

### Ninja not found
```bash
# macOS
brew install ninja

# Linux
sudo apt install ninja-build
```

### WebRTC build errors
- Ensure you have a recent C++ compiler (Clang 12+ or GCC 9+)
- Check that all dependencies are installed
- Try a clean rebuild: `./scripts/build-webrtc.sh --clean --debug`

### WebRTC takes too long
- This is normal - WebRTC is a large codebase
- The build script only builds audio components (saves time)
- Subsequent builds will be much faster due to incremental compilation

## Manual WebRTC Build

If the script fails, you can build WebRTC manually:

```bash
# Clone WebRTC
git clone https://github.com/webrtc-mirror/webrtc.git webrtc-src
cd webrtc-src

# Configure build
gn gen build-debug --args='target_cpu="arm64" is_debug=true is_component_build=false rtc_build_examples=false rtc_build_tools=false rtc_include_tests=false use_lld=false'

# Build audio components
ninja -C build-debug audio

# Output library
ls -lh build-debug/obj/libwebrtc.a
```

## Verifying the Build

After building, verify the library exists:

```bash
ls -lh .deps-cache/Debug/webrtc-src/build-Debug/obj/libwebrtc.a
file .deps-cache/Debug/webrtc-src/build-Debug/obj/libwebrtc.a
```

Then configure CMake and it will automatically detect and link against the library.

## WebRTC AEC3 Configuration

ascii-chat's audio pipeline uses WebRTC AEC3 with these settings:

- **Sample rate**: 48 kHz (Opus native rate)
- **Frame size**: 20 ms (960 samples at 48kHz)
- **Network delay estimation**: Automatic (0-500ms)
- **Adaptive filtering**: Enabled
- **Residual echo suppression**: Enabled

See `lib/audio/client_audio_pipeline.h` for detailed configuration options.

## References

- [GN build system](https://gn.googlesource.com/gn/)
- [WebRTC source](https://github.com/webrtc-mirror/webrtc)
- [WebRTC build instructions](https://webrtc.org/getting-started/build-instructions)
- [AEC3 documentation](https://webrtc.org/getting-started/build-instructions)
