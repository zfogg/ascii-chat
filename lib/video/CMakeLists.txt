# lib/video - Video processing and SIMD

set(VIDEO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/video_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ansi_fast.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ansi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/palette.c
    ${CMAKE_CURRENT_SOURCE_DIR}/color_filter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/digital_rain.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webcam/webcam.c
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
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer.c
            ${CMAKE_SOURCE_DIR}/lib/media/ffmpeg_encoder.c
            # Bundled fonts and generated C code (generated at CMake configure time)
            ${CMAKE_CURRENT_SOURCE_DIR}/font.c
            "${CMAKE_BINARY_DIR}/generated/data/fonts/matrix_resurrected.c"
            "${CMAKE_BINARY_DIR}/generated/data/fonts/default.c"
        )
    endif()
endif()

# SIMD sources
set(SIMD_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/simd/ascii_simd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/simd/ascii_simd_color.c
    ${CMAKE_CURRENT_SOURCE_DIR}/simd/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/output_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rle.c
)

if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/simd/sse2.c)
endif()

if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/simd/ssse3.c)
endif()

if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/simd/avx2.c)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/lib/video/simd/avx2.c
        PROPERTIES COMPILE_FLAGS "-mavx2"
        DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

if(ENABLE_SIMD_NEON)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/simd/neon.c)
endif()

if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/simd/sve.c)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/lib/video/simd/sve.c
        PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve"
        DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

set(VIDEO_SRCS ${VIDEO_SRCS} PARENT_SCOPE)
set(SIMD_SRCS ${SIMD_SRCS} PARENT_SCOPE)
