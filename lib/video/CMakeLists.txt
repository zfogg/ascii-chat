# lib/video - Video processing and SIMD

set(VIDEO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/video_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ansi_fast.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ansi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/palette.c
    ${CMAKE_CURRENT_SOURCE_DIR}/color_filter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/anim/digital_rain.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webcam/webcam.c
    # H.265 codec (FFmpeg-based) - required for HEVC video support
    ${CMAKE_CURRENT_SOURCE_DIR}/h265/encoder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h265/decoder.c
    # ASCII rendering pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ascii.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/dispatcher.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sgr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/common.c
)

# Media source files (only when FFmpeg is available)
if(FFMPEG_FOUND)
    list(APPEND VIDEO_SRCS
        ${CMAKE_SOURCE_DIR}/lib/media/source.c
        ${CMAKE_SOURCE_DIR}/lib/media/ffmpeg_decoder.c
        ${CMAKE_SOURCE_DIR}/lib/media/yt_dlp.c
    )
    # Render-to-file (Unix only)
    if(NOT WIN32)
        list(APPEND VIDEO_SRCS
            ${CMAKE_CURRENT_SOURCE_DIR}/ascii/file/renderer.c
            ${CMAKE_SOURCE_DIR}/lib/media/ffmpeg_encoder.c
            # Bundled fonts and generated C code (generated at CMake configure time)
            ${CMAKE_CURRENT_SOURCE_DIR}/font.c
            "${CMAKE_BINARY_DIR}/generated/data/fonts/matrix_resurrected.c"
            "${CMAKE_BINARY_DIR}/generated/data/fonts/default.c"
            # libvterm + FreeType2 render-file backend
            ${CMAKE_CURRENT_SOURCE_DIR}/ascii/file/terminal.c
        )
    endif()
endif()

# SIMD sources (reorganized into ascii/{arch}/{halfblock,foreground,background})
set(SIMD_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/output_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rle.c
    # Scalar ASCII render implementations
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/scalar/halfblock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/scalar/foreground.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/scalar/background.c
)

if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/background.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/background.c
        PROPERTIES COMPILE_FLAGS "-msse2"
    )
endif()

if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/background.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/background.c
        PROPERTIES COMPILE_FLAGS "-mssse3"
    )
endif()

if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/background.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/background.c
        PROPERTIES COMPILE_FLAGS "-mavx2"
    )
endif()

if(ENABLE_SIMD_NEON)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/halfblock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/background.c
    )
endif()

if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/background.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/foreground.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/background.c
        PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve"
    )
endif()

set(VIDEO_SRCS ${VIDEO_SRCS} PARENT_SCOPE)
set(SIMD_SRCS ${SIMD_SRCS} PARENT_SCOPE)
