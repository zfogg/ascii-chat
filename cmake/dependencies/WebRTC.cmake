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

# Use FetchContent_MakeAvailable for modern CMake 3.14+
# This will call FetchContent_Populate automatically
FetchContent_GetProperties(webrtc_aec3)
if(NOT webrtc_aec3_POPULATED)
    # Manually populate to have control over patch timing
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
    # CMAKE_CXX_STANDARD cache variables don't override parent settings.
    # Instead, inject C++17 directly into CMAKE_CXX_FLAGS which takes precedence.
    string(REPLACE "-std=c++26" "-std=c++17" WEBRTC_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-std=gnu++26" "-std=c++17" WEBRTC_CXX_FLAGS "${WEBRTC_CXX_FLAGS}")

    # Suppress all warnings for third-party WebRTC code (not our code to fix)
    string(APPEND WEBRTC_CXX_FLAGS " -w")

    set(SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${WEBRTC_CXX_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

    # Build WebRTC subdirectory with C++17 and warnings suppressed
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
    add_library(webrtc_audio_processing INTERFACE)

    # Link all WebRTC libraries that AudioProcess depends on
    # AudioProcess needs: aec3, api, base (from audio_processing/CMakeLists.txt:4)
    # For shared library builds, transitive dependencies must be explicitly linked
    target_link_libraries(webrtc_audio_processing INTERFACE
        AudioProcess
        aec3
        api
        base
    )

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

    # Suppress all warnings on WebRTC targets using target_compile_options
    # This ensures warnings are suppressed regardless of how CMAKE_CXX_FLAGS are modified
    foreach(target AudioProcess base api aec3)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -w)
        endif()
    endforeach()
else()
    message(FATAL_ERROR "AEC3 build failed - Missing targets: AudioProcess=${TARGET AudioProcess} api=${TARGET api}")
endif()

message(STATUS "${BoldGreen}WebRTC AEC3 audio processing ready${ColorReset}")
