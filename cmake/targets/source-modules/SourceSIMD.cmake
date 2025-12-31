# =============================================================================
# Module 4: SIMD (performance-critical - changes weekly)
# =============================================================================
# SIMD-accelerated functions for video processing

set(SIMD_SRCS)

# Always include common SIMD files
list(APPEND SIMD_SRCS
    lib/video/simd/ascii_simd.c
    lib/video/simd/ascii_simd_color.c
    lib/video/simd/common.c
    lib/video/output_buffer.c
    lib/video/rle.c
)

# Architecture-specific SIMD sources based on detection
if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS lib/video/simd/sse2.c)
endif()

if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS lib/video/simd/ssse3.c)
endif()

if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS lib/video/simd/avx2.c)
    set_source_files_properties(lib/video/simd/avx2.c PROPERTIES COMPILE_FLAGS "-mavx2")
endif()

if(ENABLE_SIMD_NEON)
    list(APPEND SIMD_SRCS lib/video/simd/neon.c)
endif()

if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS lib/video/simd/sve.c)
    set_source_files_properties(lib/video/simd/sve.c PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve")
endif()
