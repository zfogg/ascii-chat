# =============================================================================
# WebRTC AEC3 - Extracted Audio Processing Library
# =============================================================================
# This integrates the extracted WebRTC AEC3 library.
#
# Source: https://github.com/zhixingheyixsh/webrtc_AEC3
# (Extracted WebRTC audio processing components)
#
# WebRTC AEC3 Features:
#   - Acoustic Echo Cancellation v3 (AEC3)
#   - Automatic network delay estimation (0-500ms)
#   - Adaptive filtering to actual echo path
#   - Residual echo suppression
#   - Jitter buffer handling
#   - Production-grade (used by Google Meet, Zoom, Discord, Teams, Slack, Jitsi)
#
# Build Strategy:
#   1. CMake fetches the AEC3 repository
#   2. We build it as a static library with minimal configuration
#   3. ascii-chat links against it
# =============================================================================

include(FetchContent)

set(WEBRTC_AEC3_REPO "https://github.com/zhixingheyixsh/webrtc_AEC3")
set(WEBRTC_AEC3_TAG "main")

message(STATUS "${BoldCyan}WebRTC AEC3 Audio Processing Library${ColorReset}")
message(STATUS "  Repository: ${WEBRTC_AEC3_REPO}")
message(STATUS "  Branch: ${WEBRTC_AEC3_TAG}")

# Fetch the AEC3 source
FetchContent_Declare(
    webrtc_aec3
    GIT_REPOSITORY ${WEBRTC_AEC3_REPO}
    GIT_TAG ${WEBRTC_AEC3_TAG}
)

# We need to apply patches before add_subdirectory(), so we manually control the population.
# We set CMP0169 policy to OLD to use FetchContent_Populate() directly.
# This is intentional and necessary for our patching workflow.
cmake_policy(SET CMP0169 OLD)

FetchContent_GetProperties(webrtc_aec3)
if(NOT webrtc_aec3_POPULATED)
    # Manually populate the source (allows us to apply patches before add_subdirectory)
    FetchContent_Populate(webrtc_aec3)

    # Apply patch after source is populated
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DWEBRTC_AEC3_SOURCE_DIR=${webrtc_aec3_SOURCE_DIR}
            -DPATCH_SCRIPT_DIR=${CMAKE_SOURCE_DIR}/cmake/dependencies/patches
            -P ${CMAKE_SOURCE_DIR}/cmake/dependencies/patches/patch-webrtc-aec3-cmake.cmake
        RESULT_VARIABLE PATCH_RESULT
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    if(PATCH_RESULT)
        message(FATAL_ERROR "Failed to patch WebRTC AEC3 CMakeLists.txt")
    endif()

    # Now add the WebRTC AEC3 as a subdirectory with explicit generator
    # CRITICAL: WebRTC requires C++17 for Abseil compatibility.
    # On macOS with Homebrew LLVM, -resource-dir flag conflicts with libc++ header location.
    # The -resource-dir flag from ascii-chat points to /opt/homebrew/opt/llvm (symlink)
    # but libc++ headers are in /opt/homebrew/Cellar/llvm/VERSION (actual location).
    # This causes clang to look for builtin headers in the wrong place.

    # Save parent flags to restore later
    set(SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

    # On macOS, remove -resource-dir flag from CMAKE_CXX_FLAGS before building WebRTC
    # This allows clang to use its default resource directory detection,
    # which correctly finds libc++ headers relative to the clang++ binary location.
    if(APPLE AND CMAKE_CXX_COMPILER MATCHES "clang")
        string(REGEX REPLACE "-resource-dir [^ ]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REGEX REPLACE "-resource-dir [^ ]+" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        message(STATUS "Removed -resource-dir flag from WebRTC build (allows clang to auto-detect libc++ headers)")
    endif()

    # Build WebRTC subdirectory with clean compiler flags
    add_subdirectory(${webrtc_aec3_SOURCE_DIR} ${CMAKE_BINARY_DIR}/webrtc_aec3-build)

    # Restore the original C++ and C flags for ascii-chat
    set(CMAKE_CXX_FLAGS "${SAVED_CMAKE_CXX_FLAGS}")
    set(CMAKE_C_FLAGS "${SAVED_CMAKE_C_FLAGS}")
endif()

message(STATUS "  ${BoldGreen}✓ WebRTC AEC3 configured and built successfully${ColorReset}")
message(STATUS "  Source dir: ${webrtc_aec3_SOURCE_DIR}")

# The AEC3 project builds libraries: AudioProcess, base, api, aec3
# AudioProcess internally depends on these other libraries (see audio_processing/CMakeLists.txt:40)
# For shared library builds to work correctly, we must link all transitive dependencies
if(TARGET AudioProcess)
    message(STATUS "  Library target: AudioProcess")

    # Suppress all warnings on WebRTC targets using target_compile_options
    # This must be done BEFORE creating webrtc_audio_processing interface library
    # to ensure warnings are suppressed regardless of how CMAKE_CXX_FLAGS are modified
    foreach(target AudioProcess base api aec3)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -w)
        endif()
    endforeach()

    # Suppress warnings on all Abseil targets too
    # Abseil has many targets, so we suppress warnings on all of them
    get_property(_all_targets DIRECTORY ${webrtc_aec3_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _all_targets)
        if(_target MATCHES "^absl_")
            target_compile_options(${_target} PRIVATE -w)
        endif()
    endforeach()

    add_library(webrtc_audio_processing INTERFACE)

    # Link all WebRTC libraries that AudioProcess depends on
    # AudioProcess needs: aec3, api, base (from audio_processing/CMakeLists.txt:4)
    # For shared library builds on Linux/Unix, we must use WHOLE_ARCHIVE to force all symbols
    # to be embedded in the shared object (not just referenced). On macOS use -force_load.
    # This is critical for symbol resolution when the shared library is used.

    if(APPLE)
        # macOS uses -force_load to embed all symbols from static libraries
        # Also need to link C++ standard library for exception handling symbols
        target_link_libraries(webrtc_audio_processing INTERFACE
            -force_load $<TARGET_FILE:AudioProcess>
            -force_load $<TARGET_FILE:aec3>
            -force_load $<TARGET_FILE:api>
            -force_load $<TARGET_FILE:base>
            c++
        )
    else()
        # Linux/Unix: Use WHOLE_ARCHIVE wrapper to embed all symbols
        # This ensures all WebRTC symbols are available when the shared library is used
        # CRITICAL: Must also link C++ standard library for exception handling, RTTI, and C++ runtime
        # Use $<TARGET_FILE:> to get raw library paths without CMake's implicit link dependencies
        # For musl builds, use libc++ (LLVM C++ library). For glibc builds, use libstdc++ (GCC C++ library).
        target_link_libraries(webrtc_audio_processing INTERFACE
            -Wl,--whole-archive
            $<TARGET_FILE:AudioProcess>
            $<TARGET_FILE:aec3>
            $<TARGET_FILE:api>
            $<TARGET_FILE:base>
            -Wl,--no-whole-archive
            $<IF:$<BOOL:${USE_MUSL}>,c++,stdc++>
        )
    endif()

    # Add include path for AEC3 headers
    # Use INTERFACE since this is an INTERFACE library
    target_include_directories(webrtc_audio_processing
        INTERFACE
        "${webrtc_aec3_SOURCE_DIR}"
        "${webrtc_aec3_SOURCE_DIR}/audio_processing/include"
        "${webrtc_aec3_SOURCE_DIR}/api"
        "${webrtc_aec3_SOURCE_DIR}/base"
        "${webrtc_aec3_SOURCE_DIR}/base/abseil"
    )
else()
    message(FATAL_ERROR "AEC3 build failed - Missing targets: AudioProcess=${TARGET AudioProcess} api=${TARGET api}")
endif()

message(STATUS "${BoldGreen}WebRTC AEC3 audio processing ready${ColorReset}")
