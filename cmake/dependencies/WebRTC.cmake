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
#   1. CMake fetches the AEC3 repository at configure time
#   2. We build it completely in the deps cache (NOT in build/)
#   3. Imports pre-built libraries, NOT adding targets to main project
#   4. This prevents the 673 WebRTC/Abseil targets from bloating the build
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

cmake_policy(SET CMP0169 OLD)

FetchContent_GetProperties(webrtc_aec3)
if(NOT webrtc_aec3_POPULATED)
    # Manually populate the source
    FetchContent_Populate(webrtc_aec3)

    # Apply patches
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

    # Set up build directory in deps cache
    set(WEBRTC_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/webrtc_aec3-build")
    file(MAKE_DIRECTORY "${WEBRTC_BUILD_DIR}")

    # Only build if libraries don't exist in cache
    if(NOT EXISTS "${WEBRTC_BUILD_DIR}/lib/libAudioProcess.a" OR NOT EXISTS "${WEBRTC_BUILD_DIR}/lib/libaec3.a")
        message(STATUS "${BoldYellow}WebRTC AEC3${ColorReset} libraries not found in cache, building from source...")

        # Prepare CMake args for WebRTC build
        set(WEBRTC_CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
            -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
            -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${WEBRTC_BUILD_DIR}/lib
            -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${WEBRTC_BUILD_DIR}/lib
            -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${WEBRTC_BUILD_DIR}/bin
        )

        # On macOS with Homebrew LLVM, fix -resource-dir to point to actual Cellar path
        if(APPLE AND CMAKE_CXX_COMPILER MATCHES "clang")
            get_filename_component(LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            get_filename_component(LLVM_ROOT "${LLVM_BIN_DIR}/.." ABSOLUTE)
            set(CLANG_RESOURCE_DIR "${LLVM_ROOT}/lib/clang")
            if(EXISTS "${CLANG_RESOURCE_DIR}")
                file(GLOB CLANG_VERSION_DIRS "${CLANG_RESOURCE_DIR}/*")
                list(LENGTH CLANG_VERSION_DIRS CLANG_VERSION_COUNT)
                if(CLANG_VERSION_COUNT GREATER 0)
                    list(GET CLANG_VERSION_DIRS 0 CLANG_VERSION_DIR)
                    list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=-resource-dir ${CLANG_VERSION_DIR} -w")
                    list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_C_FLAGS=-resource-dir ${CLANG_VERSION_DIR} -w")
                    message(STATUS "Fixed -resource-dir for WebRTC: ${CLANG_VERSION_DIR}")
                endif()
            endif()
        endif()

        # Build WebRTC at configure time (not part of main build)
        execute_process(
            COMMAND ${CMAKE_COMMAND}
                ${WEBRTC_CMAKE_ARGS}
                ${webrtc_aec3_SOURCE_DIR}
            WORKING_DIRECTORY "${WEBRTC_BUILD_DIR}"
            RESULT_VARIABLE WEBRTC_CONFIG_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(NOT WEBRTC_CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure WebRTC AEC3")
        endif()

        # Build it
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE} -j${CMAKE_BUILD_PARALLEL_LEVEL}
            WORKING_DIRECTORY "${WEBRTC_BUILD_DIR}"
            RESULT_VARIABLE WEBRTC_BUILD_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(NOT WEBRTC_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build WebRTC AEC3")
        endif()

        message(STATUS "${BoldGreen}WebRTC AEC3${ColorReset} libraries built and cached successfully")
    else()
        message(STATUS "${BoldGreen}WebRTC AEC3${ColorReset} libraries found in cache: ${BoldCyan}${WEBRTC_BUILD_DIR}/lib${ColorReset}")
    endif()

endif()

# Import pre-built libraries as INTERFACE library
# This way, WebRTC targets are NOT part of the main project's target list
set(WEBRTC_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/webrtc_aec3-build")

set(WEBRTC_AUDIO_PROCESS_LIB "${WEBRTC_BUILD_DIR}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}AudioProcess${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(WEBRTC_AEC3_LIB "${WEBRTC_BUILD_DIR}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}aec3${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(WEBRTC_API_LIB "${WEBRTC_BUILD_DIR}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}api${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(WEBRTC_BASE_LIB "${WEBRTC_BUILD_DIR}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}base${CMAKE_STATIC_LIBRARY_SUFFIX}")

add_library(webrtc_audio_processing INTERFACE)

if(APPLE)
    # macOS uses -force_load to embed all symbols from static libraries
    target_link_libraries(webrtc_audio_processing INTERFACE
        -force_load "${WEBRTC_AUDIO_PROCESS_LIB}"
        -force_load "${WEBRTC_AEC3_LIB}"
        -force_load "${WEBRTC_API_LIB}"
        -force_load "${WEBRTC_BASE_LIB}"
        c++
    )
else()
    # Linux/Unix: Use WHOLE_ARCHIVE wrapper to embed all symbols
    target_link_libraries(webrtc_audio_processing INTERFACE
        -Wl,--whole-archive
        "${WEBRTC_AUDIO_PROCESS_LIB}"
        "${WEBRTC_AEC3_LIB}"
        "${WEBRTC_API_LIB}"
        "${WEBRTC_BASE_LIB}"
        -Wl,--no-whole-archive
        $<IF:$<BOOL:${USE_MUSL}>,c++,stdc++>
    )
endif()

# Add include path for AEC3 headers
target_include_directories(webrtc_audio_processing
    INTERFACE
    "${webrtc_aec3_SOURCE_DIR}"
    "${webrtc_aec3_SOURCE_DIR}/audio_processing/include"
    "${webrtc_aec3_SOURCE_DIR}/api"
    "${webrtc_aec3_SOURCE_DIR}/base"
    "${webrtc_aec3_SOURCE_DIR}/base/abseil"
)

message(STATUS "  ${BoldGreen}✓ WebRTC AEC3 configured and built successfully${ColorReset}")
message(STATUS "  Source dir: ${webrtc_aec3_SOURCE_DIR}")
message(STATUS "  Library dir: ${WEBRTC_BUILD_DIR}/lib")
message(STATUS "${BoldGreen}WebRTC AEC3 audio processing ready${ColorReset}")
