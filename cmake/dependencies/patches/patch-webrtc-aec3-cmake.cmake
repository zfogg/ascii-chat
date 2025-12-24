# =============================================================================
# Patch for WebRTC AEC3 CMakeLists.txt
# =============================================================================
# Complete integration of WebRTC AEC3 with proper dependency management.
#
# This patch enables full compilation of:
# - AudioProcess (core audio processing library)
# - api (public API including EchoCanceller3Factory)
# - base (utility and system wrappers)
# - aec3 (acoustic echo cancellation v3)
#
# The key fix: Prevent duplicate compilation of ooura_fft.cc by ensuring
# it's only compiled once in AudioProcess, then linked by other modules.
#
# Compatibility improvements:
# 1. Updates cmake_minimum_required to 3.15
# 2. Adds C++14 standard requirement (WebRTC uses std::make_unique)
# 3. Conditionally compiles SSE files based on ascii-chat's SIMD configuration
# =============================================================================

if(NOT WEBRTC_AEC3_SOURCE_DIR)
    message(FATAL_ERROR "WEBRTC_AEC3_SOURCE_DIR not provided to patch script")
endif()

# =============================================================================
# Patch 1: Main CMakeLists.txt - Update CMake compatibility
# =============================================================================
file(READ "${WEBRTC_AEC3_SOURCE_DIR}/CMakeLists.txt" CMAKE_CONTENT)

# Fix: Update cmake_minimum_required from 3.1 to 3.15
string(REPLACE
    "cmake_minimum_required (VERSION 3.1)"
    "cmake_minimum_required (VERSION 3.15)"
    CMAKE_CONTENT
    "${CMAKE_CONTENT}"
)

# Fix: Add C++17 requirement (Abseil requires C++17 for std::result_of)
string(REPLACE
    "set(CMAKE_CXX_FLAGS \"\${CMAKE_CXX_FLAGS} -fvisibility=hidden -fPIC -pthread\")"
    "set(CMAKE_CXX_FLAGS \"\${CMAKE_CXX_FLAGS} -fvisibility=hidden -fPIC -pthread -std=c++17\")\nset(CMAKE_CXX_STANDARD 17)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)"
    CMAKE_CONTENT
    "${CMAKE_CONTENT}"
)

# Write the patched main CMakeLists.txt back
file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/CMakeLists.txt" "${CMAKE_CONTENT}")

message(STATUS "Patched WebRTC AEC3 CMakeLists.txt - updated cmake version and C++ standard")

# =============================================================================
# Patch 2: audio_processing/CMakeLists.txt - Conditionally compile SSE
# =============================================================================
# Make SSE compilation conditional based on ascii-chat's SIMD configuration
file(READ "${WEBRTC_AEC3_SOURCE_DIR}/audio_processing/CMakeLists.txt" AP_CMAKE_CONTENT)

# Add api and base to EXTRA_LIBS (needed for AudioProcess dependencies)
string(REPLACE
    "include_directories( \${PROJECT_SOURCE_DIR} aec3 include utility logging)
add_subdirectory(aec3)
set (EXTRA_LIBS \${EXTRA_LIBS} aec3)"
    "include_directories( \${PROJECT_SOURCE_DIR} aec3 include utility logging)
add_subdirectory(aec3)
# AudioProcess depends on api and base libraries
set (EXTRA_LIBS \${EXTRA_LIBS} aec3 api base)"
    AP_CMAKE_CONTENT
    "${AP_CMAKE_CONTENT}"
)

# Replace the hardcoded SSE file with conditional logic
string(REPLACE
    "add_library(AudioProcess
            STATIC
            audio_frame.cc
            audio_buffer.cc
            high_pass_filter.cc
            splitting_filter.cc
            splitting_filter_c.c
            sparse_fir_filter.cc
            three_band_filter_bank.cc
            channel_buffer.cc
            #utility/pffft_wrapper.cc
            utility/cascaded_biquad_filter.cc
            utility/delay_estimator.cc
            utility/delay_estimator_wrapper.cc
            utility/ooura_fft.cc
            channel_layout.cc
            resampler/push_sinc_resampler.cc
            resampler/sinc_resampler.cc
            resampler/sinc_resampler_sse.cc
            )"
    "# Conditionally include SIMD-specific files based on ascii-chat's SIMD configuration
set(AUDIO_PROCESS_SIMD_SOURCES \"\")

# NEON support (ARM64)
if(ENABLE_SIMD_NEON)
    list(APPEND AUDIO_PROCESS_SIMD_SOURCES utility/ooura_fft_neon.cc)
    list(APPEND AUDIO_PROCESS_SIMD_SOURCES resampler/sinc_resampler_neon.cc)
endif()

# SSE2 support (x86/x86_64) - SSSE3 and AVX2 require SSE2
if(ENABLE_SIMD_SSE2 OR ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2)
    list(APPEND AUDIO_PROCESS_SIMD_SOURCES utility/ooura_fft_sse2.cc)
    list(APPEND AUDIO_PROCESS_SIMD_SOURCES resampler/sinc_resampler_sse.cc)
endif()

add_library(AudioProcess
            STATIC
            audio_frame.cc
            audio_buffer.cc
            high_pass_filter.cc
            splitting_filter.cc
            splitting_filter_c.c
            sparse_fir_filter.cc
            three_band_filter_bank.cc
            channel_buffer.cc
            #utility/pffft_wrapper.cc
            utility/cascaded_biquad_filter.cc
            utility/delay_estimator.cc
            utility/delay_estimator_wrapper.cc
            utility/ooura_fft.cc
            channel_layout.cc
            resampler/push_sinc_resampler.cc
            resampler/sinc_resampler.cc
            \${AUDIO_PROCESS_SIMD_SOURCES}
            )"
    AP_CMAKE_CONTENT
    "${AP_CMAKE_CONTENT}"
)

file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/audio_processing/CMakeLists.txt" "${AP_CMAKE_CONTENT}")

message(STATUS "Patched WebRTC AEC3 audio_processing/CMakeLists.txt - added dependencies and conditional SSE")

# =============================================================================
# Patch 3: aec3/CMakeLists.txt - Conditionally compile SSE in aec3 module
# =============================================================================
# The aec3 module compiles ooura_fft_sse2.cc which should only be included
# on SSE2-capable architectures.

file(READ "${WEBRTC_AEC3_SOURCE_DIR}/audio_processing/aec3/CMakeLists.txt" AEC3_CMAKE_CONTENT)

# Remove ooura_fft files - AudioProcess already compiles them, aec3 links against it
string(REPLACE
    "    ../utility/ooura_fft.cc
    ../utility/ooura_fft_sse2.cc"
    ""
    AEC3_CMAKE_CONTENT
    "${AEC3_CMAKE_CONTENT}"
)

file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/audio_processing/aec3/CMakeLists.txt" "${AEC3_CMAKE_CONTENT}")

message(STATUS "Patched WebRTC AEC3 aec3/CMakeLists.txt - removed duplicate ooura_fft compilation")

# =============================================================================
# Patch 4: base/CMakeLists.txt - Uncomment Abseil include path for C++ consumers
# =============================================================================
# The base module depends on Abseil. We need to expose the Abseil include
# path so that code using base (like client_audio_pipeline.cpp) can access
# Abseil headers that are included by base's headers.

file(READ "${WEBRTC_AEC3_SOURCE_DIR}/base/CMakeLists.txt" BASE_CMAKE_CONTENT)

# Uncomment the Abseil subdirectory
string(REPLACE
    "#add_subdirectory(abseil)"
    "add_subdirectory(abseil)"
    BASE_CMAKE_CONTENT
    "${BASE_CMAKE_CONTENT}"
)

# Uncomment the Abseil include path
string(REPLACE
    "#include_directories(\"\\${PROJECT_SOURCE_DIR}/base/abseil\")"
    "include_directories(\"\\${PROJECT_SOURCE_DIR}/base/abseil\")"
    BASE_CMAKE_CONTENT
    "${BASE_CMAKE_CONTENT}"
)

file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/base/CMakeLists.txt" "${BASE_CMAKE_CONTENT}")

message(STATUS "Patched WebRTC AEC3 base/CMakeLists.txt - enabled Abseil include path for C++ consumers")

# =============================================================================
# Patch 5: base/abseil/CMakeLists.txt - Disable test framework requirements
# =============================================================================
# Abseil's CMakeLists.txt requires gtest, gtest_main, gmock, gmock_main even though
# we don't need them. We're only using Abseil for utility headers.

file(READ "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/CMakeLists.txt" ABSEIL_CMAKE_CONTENT)

# Disable all test framework requirement checks
# These are checked by check_target() calls but we don't need testing infrastructure
string(REPLACE
    "check_target(gtest)"
    "# check_target(gtest) - disabled for WebRTC AEC3 integration"
    ABSEIL_CMAKE_CONTENT
    "${ABSEIL_CMAKE_CONTENT}"
)

string(REPLACE
    "check_target(gtest_main)"
    "# check_target(gtest_main) - disabled for WebRTC AEC3 integration"
    ABSEIL_CMAKE_CONTENT
    "${ABSEIL_CMAKE_CONTENT}"
)

string(REPLACE
    "check_target(gmock)"
    "# check_target(gmock) - disabled for WebRTC AEC3 integration"
    ABSEIL_CMAKE_CONTENT
    "${ABSEIL_CMAKE_CONTENT}"
)

string(REPLACE
    "check_target(gmock_main)"
    "# check_target(gmock_main) - disabled for WebRTC AEC3 integration"
    ABSEIL_CMAKE_CONTENT
    "${ABSEIL_CMAKE_CONTENT}"
)

file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/CMakeLists.txt" "${ABSEIL_CMAKE_CONTENT}")

message(STATUS "Patched WebRTC AEC3 base/abseil/CMakeLists.txt - disabled test framework requirements")

# =============================================================================
# Patch 6: absl/strings/internal/str_format/extension.h - Add missing includes
# =============================================================================
# Abseil's extension.h uses uint8_t and uint64_t but doesn't include <cstdint>
# This is a compatibility issue with C++23 mode on Linux.

file(READ "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/strings/internal/str_format/extension.h" EXTENSION_H_CONTENT)

# Add include directive at the beginning of the file if not already present
if(NOT EXTENSION_H_CONTENT MATCHES "#include <cstdint>")
    string(REPLACE
        "#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_"
        "#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_\n#include <cstdint>"
        EXTENSION_H_CONTENT
        "${EXTENSION_H_CONTENT}"
    )
    file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/strings/internal/str_format/extension.h" "${EXTENSION_H_CONTENT}")
    message(STATUS "Patched Abseil extension.h - added missing <cstdint> include")
endif()

# =============================================================================
# Patch 7: absl/synchronization/internal/graphcycles.cc - Add missing includes
# =============================================================================
# graphcycles.cc uses std::numeric_limits but doesn't include <limits>
# This causes compilation failures in C++23 mode on Linux.

file(READ "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/synchronization/internal/graphcycles.cc" GRAPHCYCLES_CONTENT)

# Add include directive if not already present
if(NOT GRAPHCYCLES_CONTENT MATCHES "#include <limits>")
    string(REPLACE
        "#include <algorithm>"
        "#include <algorithm>\n#include <limits>"
        GRAPHCYCLES_CONTENT
        "${GRAPHCYCLES_CONTENT}"
    )
    file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/synchronization/internal/graphcycles.cc" "${GRAPHCYCLES_CONTENT}")
    message(STATUS "Patched Abseil graphcycles.cc - added missing <limits> include")
endif()

# =============================================================================
# Patch 8: absl/base/config.h - Add required standard library includes
# =============================================================================
# Abseil's headers use std::numeric_limits, uint32_t, uint64_t, intptr_t
# but don't include the necessary headers. Add them to config.h since
# nearly all Abseil files include this header.

file(READ "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/base/config.h" CONFIG_H_CONTENT)

# Add standard library includes after the copyright notice and before other content
if(NOT CONFIG_H_CONTENT MATCHES "#include <cstdint>")
    string(REPLACE
        "#ifndef ABSL_BASE_CONFIG_H_"
        "#ifndef ABSL_BASE_CONFIG_H_\n#include <cstdint>\n#include <cstddef>\n#include <climits>\n#include <limits>\n#include <memory>\n#include <utility>\n#include <vector>\n#include <string>\n#include <cstring>\n#include <algorithm>\n#include <cstdlib>"
        CONFIG_H_CONTENT
        "${CONFIG_H_CONTENT}"
    )
    file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/base/config.h" "${CONFIG_H_CONTENT}")
    message(STATUS "Patched Abseil config.h - added missing standard library includes")
endif()

# =============================================================================
# Patch 9: audio_processing/aec3/clockdrift_detector.h - Add missing includes
# =============================================================================
# WebRTC headers use size_t without including <cstddef>

if(EXISTS "${WEBRTC_AEC3_SOURCE_DIR}/audio_processing/aec3/clockdrift_detector.h")
    file(READ "${WEBRTC_AEC3_SOURCE_DIR}/audio_processing/aec3/clockdrift_detector.h" CLOCKDRIFT_CONTENT)

    if(NOT CLOCKDRIFT_CONTENT MATCHES "#include <cstddef>")
        string(REPLACE
            "#ifndef MODULES_AUDIO_PROCESSING_AEC3_CLOCKDRIFT_DETECTOR_H_"
            "#ifndef MODULES_AUDIO_PROCESSING_AEC3_CLOCKDRIFT_DETECTOR_H_\n#include <cstddef>"
            CLOCKDRIFT_CONTENT
            "${CLOCKDRIFT_CONTENT}"
        )
        file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/audio_processing/aec3/clockdrift_detector.h" "${CLOCKDRIFT_CONTENT}")
        message(STATUS "Patched WebRTC clockdrift_detector.h - added missing <cstddef> include")
    endif()
endif()

# =============================================================================
# Patch 10: absl/debugging/failure_signal_handler.cc - Fix std::max type mismatch
# =============================================================================
# failure_signal_handler.cc has std::max(SIGSTKSZ, 65536) where SIGSTKSZ is long
# but 65536 is int. Cast to size_t to fix the type mismatch.

file(READ "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/debugging/failure_signal_handler.cc" FSH_CONTENT)

if(FSH_CONTENT MATCHES "std::max\\(SIGSTKSZ, 65536\\)")
    string(REPLACE
        "std::max(SIGSTKSZ, 65536)"
        "std::max(static_cast<size_t>(SIGSTKSZ), static_cast<size_t>(65536))"
        FSH_CONTENT
        "${FSH_CONTENT}"
    )
    file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/base/abseil/absl/debugging/failure_signal_handler.cc" "${FSH_CONTENT}")
    message(STATUS "Patched Abseil failure_signal_handler.cc - fixed std::max type mismatch")
endif()

message(STATUS "WebRTC AEC3 patching complete: Full stack (api/aec3/base/AudioProcess) enabled")
