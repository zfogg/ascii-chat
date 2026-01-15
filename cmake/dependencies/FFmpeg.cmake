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

# Create media source library
set(MEDIA_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/lib/media/source.c
    ${CMAKE_SOURCE_DIR}/lib/media/ffmpeg_decoder.c
)

add_library(media_source STATIC ${MEDIA_SOURCE_FILES})

target_include_directories(media_source PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(media_source PUBLIC
    ${FFMPEG_LIBRARIES}
)

# Ensure media_source is available globally
set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} PARENT_SCOPE)
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS} PARENT_SCOPE)
set(FFMPEG_FOUND ${FFMPEG_FOUND} PARENT_SCOPE)
