# =============================================================================
# SIMD Module (ascii-chat-simd)
# =============================================================================
# Performance-critical SIMD-optimized image processing
# Supports SSE2, SSSE3, AVX2 (x86_64) and NEON, SVE (ARM64)
# =============================================================================

# Common SIMD files (always included)
set(SIMD_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii_simd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii_simd_color.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../output_buffer.c
)

# =============================================================================
# Architecture-Specific SIMD Sources
# =============================================================================
# Each SIMD implementation has specific compiler flags for optimal codegen

# SSE2 (baseline x86_64)
if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/sse2.c)
    # SSE2 is baseline for x86_64, no special flags needed
endif()

# SSSE3 (shuffle instructions)
if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/ssse3.c)
    # SSSE3 is typically enabled with -mssse3 but included in baseline modern clang
endif()

# AVX2 (256-bit vectors)
if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/avx2.c)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/avx2.c
        PROPERTIES COMPILE_FLAGS "-mavx2"
    )
endif()

# NEON (ARM64 baseline)
if(ENABLE_SIMD_NEON)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/neon.c)
    # NEON is baseline for ARM64, no special flags needed
endif()

# SVE (Scalable Vector Extension)
if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/sve.c)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/sve.c
        PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve"
    )
endif()

# =============================================================================
# Create SIMD Module
# =============================================================================

create_ascii_chat_module(ascii-chat-simd "${SIMD_SRCS}")

# =============================================================================
# Dependencies
# =============================================================================
# SIMD depends on util, core, video (circular with video resolved at link time)

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-simd
        ascii-chat-util
        ascii-chat-core
        ascii-chat-video
    )
endif()

# Export sources to parent scope for unified library builds
set(SIMD_SRCS ${SIMD_SRCS} PARENT_SCOPE)
