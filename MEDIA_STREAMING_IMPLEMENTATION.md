# Media File Streaming Implementation Guide

## STATUS: Core libraries implemented, integration in progress

### ✅ Completed
1. **Media Source Abstraction** (`lib/media/source.h/c`)
2. **FFmpeg Decoder** (`lib/media/ffmpeg_decoder.h/c`)
3. **Error Codes** (ERROR_MEDIA_* added to error_codes.h)
4. **Options Structure** (media_file, media_loop, media_from_stdin fields added)

### 🔨 Remaining Integration Work

## 1. Options Parsing (lib/options/options.c)

Add to CLIENT mode option parsing:

```c
// In parse_client_options() function
{"file", required_argument, 0, 'f'},
{"loop", no_argument, 0, 'l'},

// In switch statement
case 'f':
    SAFE_STRNCPY(opts->media_file, optarg, sizeof(opts->media_file));
    if (strcmp(optarg, "-") == 0) {
        opts->media_from_stdin = true;
    }
    break;
case 'l':
    opts->media_loop = true;
    break;
```

Initialize defaults in options_init():
```c
opts->media_file[0] = '\0';
opts->media_loop = false;
opts->media_from_stdin = false;
```

## 2. Client Capture Integration (src/client/capture.c)

Replace webcam-only code with media source:

```c
#include "media/source.h"

static media_source_t *g_media_source = NULL;

// In capture_init():
asciichat_error_t capture_init() {
    // Check if media file specified
    const char *media_file = GET_OPTION(media_file);
    if (media_file && media_file[0] != '\0') {
        // Use media file or stdin
        media_source_type_t type = GET_OPTION(media_from_stdin)
            ? MEDIA_SOURCE_STDIN
            : MEDIA_SOURCE_FILE;

        g_media_source = media_source_create(type, media_file);
        if (!g_media_source) {
            log_error("Failed to open media file: %s", media_file);
            return ERROR_MEDIA_OPEN;
        }

        if (GET_OPTION(media_loop)) {
            media_source_set_loop(g_media_source, true);
        }

        log_info("Using media file: %s (loop=%s)",
                 media_file, GET_OPTION(media_loop) ? "yes" : "no");
    } else if (GET_OPTION(test_pattern)) {
        g_media_source = media_source_create(MEDIA_SOURCE_TEST, NULL);
    } else {
        // Default: use webcam
        char webcam_idx_str[16];
        snprintf(webcam_idx_str, sizeof(webcam_idx_str), "%u", GET_OPTION(webcam_index));
        g_media_source = media_source_create(MEDIA_SOURCE_WEBCAM, webcam_idx_str);
    }

    if (!g_media_source) {
        return ERROR_MEDIA_INIT;
    }

    return ASCIICHAT_OK;
}

// In capture_thread_main():
while (!should_exit()) {
    image_t *frame = media_source_read_video(g_media_source);

    if (!frame) {
        if (media_source_at_end(g_media_source)) {
            log_info("Media file ended");
            break;  // Exit thread when file ends (if not looping)
        }
        continue;  // Temporary error, try next frame
    }

    // Existing frame processing and transmission...
}

// In capture_cleanup():
if (g_media_source) {
    media_source_destroy(g_media_source);
    g_media_source = NULL;
}
```

## 3. CMakeLists.txt Updates

Add FFmpeg dependencies:

```cmake
# Find FFmpeg libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

# Add media source library
add_library(media_source STATIC
    lib/media/source.c
    lib/media/ffmpeg_decoder.c
)

target_include_directories(media_source PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(media_source
    ${FFMPEG_LIBRARIES}
    image
    webcam
)

# Link to client
target_link_libraries(ascii-chat
    media_source
    ${FFMPEG_LIBRARIES}
)
```

## 4. Dependency Installation

Update `scripts/install-deps.sh`:

```bash
# FFmpeg development libraries
if command -v apt-get &> /dev/null; then
    # Debian/Ubuntu
    sudo apt-get install -y \
        libavformat-dev \
        libavcodec-dev \
        libavutil-dev \
        libswscale-dev \
        libswresample-dev
elif command -v dnf &> /dev/null; then
    # Fedora/RHEL
    sudo dnf install -y \
        ffmpeg-devel
elif command -v pacman &> /dev/null; then
    # Arch Linux
    sudo pacman -S --noconfirm ffmpeg
fi
```

Update `scripts/install-deps-macos.sh`:

```bash
# Add to Homebrew dependencies
brew install ffmpeg
```

Update `docs/dependencies.md`:

```markdown
### FFmpeg (Required for media file streaming)

- **Purpose**: Decode video/audio files for streaming
- **Debian/Ubuntu**: `sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev`
- **Fedora/RHEL**: `sudo dnf install ffmpeg-devel`
- **Arch Linux**: `sudo pacman -S ffmpeg`
- **macOS**: `brew install ffmpeg`
```

## 5. Manpage Updates (docs/ascii-chat.1)

Add to OPTIONS section:

```
.TP
.BR \-f ", " \-\-file " " \fIPATH\fR
Stream video/audio from media file instead of webcam.
Use "-" for stdin (pipe or redirect).
Supports mp4, avi, mkv, webm, mov, gif, mp3, wav, and more.

.TP
.BR \-l ", " \-\-loop
Loop media file playback. When the file ends, restart from beginning.
Not supported for stdin input.

.SH EXAMPLES
.TP
Stream a video file:
.B ascii-chat client example.com --file video.mp4

.TP
Loop an animated GIF:
.B ascii-chat client --file animation.gif --loop

.TP
Pipe video from ffmpeg:
.B cat video.mp4 | ascii-chat client --file -

.TP
Redirect from file:
.B ascii-chat client --file - < video.mp4
```

## 6. Shell Completions

### Bash (completions/ascii-chat.bash)

```bash
_ascii_chat_client_options="${_ascii_chat_client_options} --file --loop"
```

### Zsh (completions/_ascii-chat)

```zsh
'--file[Stream from media file]:file:_files'
'--loop[Loop media file playback]'
```

### Fish (completions/ascii-chat.fish)

```fish
complete -c ascii-chat -l file -d 'Stream from media file' -r
complete -c ascii-chat -l loop -d 'Loop media file playback'
```

## 7. Usage Help Text

Update `usage_client()` in `lib/options/options.c`:

```c
fprintf(desc, "  Media Streaming Options:\n");
fprintf(desc, "    -f, --file PATH          Stream video/audio from file (\"-\" for stdin)\n");
fprintf(desc, "    -l, --loop               Loop media file playback\n");
fprintf(desc, "\n");
fprintf(desc, "  Examples:\n");
fprintf(desc, "    ascii-chat client --file video.mp4\n");
fprintf(desc, "    ascii-chat client --file animation.gif --loop\n");
fprintf(desc, "    cat video.mp4 | ascii-chat client --file -\n");
fprintf(desc, "\n");
```

## 8. Testing

Create test scripts:

```bash
# tests/scripts/test_media_file.sh
#!/bin/bash
set -e

# Test video file
./build/bin/ascii-chat client --snapshot --file tests/media/test.mp4

# Test audio file
./build/bin/ascii-chat client --snapshot --file tests/media/test.mp3 --audio

# Test loop
timeout 10s ./build/bin/ascii-chat client --file tests/media/short.gif --loop || true
```

## Build Instructions

```bash
# Configure with FFmpeg
cmake --preset default -B build

# Build
cmake --build build

# Test
./build/bin/ascii-chat client --file video.mp4
cat video.mp4 | ./build/bin/ascii-chat client --file -
```

## Supported Formats

**Video**: mp4, avi, mkv, webm, mov, flv, wmv, gif (animated)
**Audio**: mp3, aac, opus, flac, wav, ogg, m4a
**Images**: gif, png, jpg (static - single frame)

All formats that FFmpeg supports are theoretically supported.
