# lib/video - Video processing and SIMD

set(VIDEO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/rgba/video_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rgba/image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rgba/color_filter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/anim/digital_rain.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webcam/webcam.c
    # H.265 codec (FFmpeg-based) - required for HEVC video support
    ${CMAKE_CURRENT_SOURCE_DIR}/h265/encoder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h265/decoder.c
    # ASCII rendering pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ascii.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sgr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/terminal/ansi.c
    # SIMD-based ASCII render implementations
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/common.c
    
    
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/palette.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/output_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/rle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/frame_validator.c
)

# Media source files (only when FFmpeg is available)
if(FFMPEG_FOUND)
    list(APPEND VIDEO_SRCS
        ${CMAKE_SOURCE_DIR}/lib/media/source.c
        ${CMAKE_SOURCE_DIR}/lib/media/ffmpeg_decoder.c
        ${CMAKE_SOURCE_DIR}/lib/media/yt_dlp.c
        # Media render-file (audio + video) - moved from lib/video/ascii/file/
        ${CMAKE_SOURCE_DIR}/lib/media/ffmpeg_encoder.c
        # Bundled fonts and generated C code (generated at CMake configure time)
        ${CMAKE_SOURCE_DIR}/lib/media/font.c
        "${CMAKE_BINARY_DIR}/generated/data/fonts/matrix_resurrected.c"
        "${CMAKE_BINARY_DIR}/generated/data/fonts/default.c"
    )
    # Render-to-file - libvterm + FreeType2 render-file backend
    list(APPEND VIDEO_SRCS
        ${CMAKE_SOURCE_DIR}/lib/media/render/renderer.c
        ${CMAKE_SOURCE_DIR}/lib/media/render/terminal.c
    )
endif()

# SIMD sources (reorganized into ascii/{arch}/{halfblock,foreground,background})
set(SIMD_SRCS
    # Scalar ASCII render implementations
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/scalar/halfblock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/scalar/foreground.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii/scalar/background.c
)

if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/color.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sse2/color.c
        PROPERTIES COMPILE_FLAGS "-msse2"
    )
endif()

if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/color.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/ssse3/color.c
        PROPERTIES COMPILE_FLAGS "-mssse3"
    )
endif()

if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/common.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/color.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/common.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/color.c
        PROPERTIES COMPILE_FLAGS "-mavx2"
    )
endif()

if(ENABLE_SIMD_NEON)
    # Enable working NEON files; disable mono.c and color.c due to function signature mismatches
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/common.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/halfblock.c
    )
    # TODO: Fix mono.c and color.c function signatures
    # ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/mono.c
    # ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/color.c
endif()

if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/color.c
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/mono.c
        ${CMAKE_CURRENT_SOURCE_DIR}/ascii/sve/color.c
        PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve"
    )
endif()

set(VIDEO_SRCS ${VIDEO_SRCS} PARENT_SCOPE)
set(SIMD_SRCS ${SIMD_SRCS} PARENT_SCOPE)
