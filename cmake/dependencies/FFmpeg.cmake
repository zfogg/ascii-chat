# =============================================================================
# FFmpeg Library Configuration
# =============================================================================
# Find FFmpeg libraries for media file decoding

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

# Export FFmpeg libraries and include directories for use in Libraries.cmake
# SourceFiles.cmake will conditionally include media source files when FFMPEG_FOUND is true
set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} PARENT_SCOPE)
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS} PARENT_SCOPE)
set(FFMPEG_FOUND ${FFMPEG_FOUND} PARENT_SCOPE)
