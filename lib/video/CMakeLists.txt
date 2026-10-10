# lib/video - Video processing and SIMD

set(VIDEO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/anim/test_pattern.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rgba/video_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rgba/image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rgba/color_filter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/anim/digital_rain.c
    ${CMAKE_CURRENT_SOURCE_DIR}/anim/controller.c
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

# ISA flags apply in the directory that creates ascii-chat-simd (the root).
# Source options also exclude these files from unity builds and shared PCHs.
list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/ascii/simd/dispatch.c)
foreach(isa SSE2 SSSE3 AVX2 NEON SVE)
    if(NOT ENABLE_SIMD_${isa})
        continue()
    endif()
    string(TOLOWER "${isa}" name)
    set(isa_sources ${CMAKE_CURRENT_SOURCE_DIR}/ascii/${name}/luminance.c)
    if(isa STREQUAL "NEON")
        list(APPEND isa_sources ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/common.c ${CMAKE_CURRENT_SOURCE_DIR}/ascii/neon/halfblock.c)
        if(ASCIICHAT_IS_ARM64)
            set(isa_flags -march=armv8-a)
        else()
            set(isa_flags -mfpu=neon)
        endif()
    else()
        list(APPEND isa_sources ${CMAKE_CURRENT_SOURCE_DIR}/ascii/${name}/mono.c ${CMAKE_CURRENT_SOURCE_DIR}/ascii/${name}/color.c)
        if(isa STREQUAL "AVX2")
            list(APPEND isa_sources ${CMAKE_CURRENT_SOURCE_DIR}/ascii/avx2/common.c)
        endif()
        if(isa STREQUAL "SVE")
            set(isa_flags -march=armv8-a+sve)
        else()
            set(isa_flags -m${name})
        endif()
    endif()
    set_source_files_properties(${isa_sources} DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTIES
        COMPILE_OPTIONS "${isa_flags};-fno-lto"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON)
    list(APPEND SIMD_SRCS ${isa_sources})
endforeach()

set(VIDEO_SRCS ${VIDEO_SRCS} PARENT_SCOPE)
set(SIMD_SRCS ${SIMD_SRCS} PARENT_SCOPE)
