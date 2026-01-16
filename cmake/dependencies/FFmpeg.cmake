# =============================================================================
# FFmpeg Library Configuration
# =============================================================================
# Find FFmpeg libraries for media file decoding

# Skip pkg-config search if using musl (libraries are built from source in MuslDependencies.cmake)
if(USE_MUSL)
    # MuslDependencies.cmake will set FFMPEG_FOUND and FFMPEG_LINK_LIBRARIES
    if(FFMPEG_FOUND)
        message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg (musl): using musl-built static libraries")
        return()
    endif()
endif()

# Find pkg-config (required for finding FFmpeg)
find_package(PkgConfig QUIET)

if(NOT PkgConfig_FOUND)
    message(WARNING "pkg-config not found - FFmpeg support will be disabled")
    set(FFMPEG_FOUND FALSE)
    return()
endif()

# Find FFmpeg components
pkg_check_modules(FFMPEG
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

if(NOT FFMPEG_FOUND)
    message(WARNING "FFmpeg not found - media file streaming will be disabled")
    message(STATUS "Install FFmpeg development libraries:")
    message(STATUS "  Debian/Ubuntu: sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev")
    message(STATUS "  Fedora/RHEL: sudo dnf install ffmpeg-devel")
    message(STATUS "  Arch Linux: sudo pacman -S ffmpeg")
    message(STATUS "  macOS: brew install ffmpeg")
    set(FFMPEG_FOUND FALSE)
    return()
endif()

message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg found:")
message(STATUS "  - libavformat: ${FFMPEG_libavformat_VERSION}")
message(STATUS "  - libavcodec: ${FFMPEG_libavcodec_VERSION}")
message(STATUS "  - libavutil: ${FFMPEG_libavutil_VERSION}")
message(STATUS "  - libswscale: ${FFMPEG_libswscale_VERSION}")
message(STATUS "  - libswresample: ${FFMPEG_libswresample_VERSION}")
message(STATUS "  - FFMPEG_LIBRARIES: ${FFMPEG_LIBRARIES}")
message(STATUS "  - FFMPEG_LDFLAGS: ${FFMPEG_LDFLAGS}")
message(STATUS "  - FFMPEG_LINK_LIBRARIES: ${FFMPEG_LINK_LIBRARIES}")

# Variables FFMPEG_LIBRARIES, FFMPEG_INCLUDE_DIRS, and FFMPEG_FOUND
# are set by pkg_check_modules() and are available in parent scope
# SourceFiles.cmake will conditionally include media source files when FFMPEG_FOUND is true
