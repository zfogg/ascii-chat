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
#
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
    GIT_SHALLOW TRUE
    GIT_SHALLOW_EXCLUDE_DEPS TRUE  # Don't shallow-clone submodules
    UPDATE_DISCONNECTED TRUE  # Don't update if already populated (avoids SSH auth issues)
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

    # Create a configuration string to detect when rebuild is needed
    # This ensures cached WebRTC libs match the current build settings
    # Include build type because Debug uses ASan which affects ABI (annotate_string mismatch)
    set(WEBRTC_BUILD_CONFIG "BUILD_TYPE=${CMAKE_BUILD_TYPE};SSE2=${ENABLE_SIMD_SSE2};SSSE3=${ENABLE_SIMD_SSSE3};AVX2=${ENABLE_SIMD_AVX2};NEON=${ENABLE_SIMD_NEON};SVE=${ENABLE_SIMD_SVE}")
    set(WEBRTC_CONFIG_MARKER "${WEBRTC_BUILD_DIR}/.build_config")

    # Determine platform-correct library names for cache check
    if(WIN32)
        set(_webrtc_check_prefix "")
    else()
        set(_webrtc_check_prefix "lib")
    endif()

    # Check if cached build exists with matching build config
    set(WEBRTC_NEEDS_REBUILD FALSE)
    if(NOT EXISTS "${WEBRTC_BUILD_DIR}/lib/${_webrtc_check_prefix}AudioProcess${CMAKE_STATIC_LIBRARY_SUFFIX}" OR NOT EXISTS "${WEBRTC_BUILD_DIR}/lib/${_webrtc_check_prefix}aec3${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set(WEBRTC_NEEDS_REBUILD TRUE)
        message(STATUS "WebRTC AEC3 libraries not found, will build from source")
    elseif(NOT EXISTS "${WEBRTC_CONFIG_MARKER}")
        set(WEBRTC_NEEDS_REBUILD TRUE)
        message(STATUS "WebRTC AEC3 build config marker not found, will rebuild")
    else()
        file(READ "${WEBRTC_CONFIG_MARKER}" CACHED_BUILD_CONFIG)
        string(STRIP "${CACHED_BUILD_CONFIG}" CACHED_BUILD_CONFIG)
        if(NOT "${CACHED_BUILD_CONFIG}" STREQUAL "${WEBRTC_BUILD_CONFIG}")
            set(WEBRTC_NEEDS_REBUILD TRUE)
            message(STATUS "WebRTC AEC3 build config changed (was: ${CACHED_BUILD_CONFIG}, now: ${WEBRTC_BUILD_CONFIG}), will rebuild")
        endif()
    endif()

    if(WEBRTC_NEEDS_REBUILD)
        message(STATUS "${BoldYellow}WebRTC AEC3${ColorReset} building from source...")

        # Clean old build to ensure fresh compilation with new build config
        if(EXISTS "${WEBRTC_BUILD_DIR}/lib")
            file(REMOVE_RECURSE "${WEBRTC_BUILD_DIR}/lib")
            message(STATUS "Cleaned old WebRTC AEC3 build artifacts")
        endif()
        file(MAKE_DIRECTORY "${WEBRTC_BUILD_DIR}/lib")

        # Prepare CMake args for WebRTC build
        # On Windows Debug builds, use debug CRT (/MDd) to match the main project
        set(_webrtc_msvc_runtime "")
        if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_webrtc_msvc_runtime "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL")
        endif()
        set(WEBRTC_CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
            -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
            ${_webrtc_msvc_runtime}
            -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${WEBRTC_BUILD_DIR}/lib
            -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${WEBRTC_BUILD_DIR}/lib
            -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${WEBRTC_BUILD_DIR}/bin
            # Pass SIMD flags to WebRTC build
            -DENABLE_SIMD_SSE2=${ENABLE_SIMD_SSE2}
            -DENABLE_SIMD_SSSE3=${ENABLE_SIMD_SSSE3}
            -DENABLE_SIMD_AVX2=${ENABLE_SIMD_AVX2}
            -DENABLE_SIMD_NEON=${ENABLE_SIMD_NEON}
            -DENABLE_SIMD_SVE=${ENABLE_SIMD_SVE}
            # Pass shared deps preference for Abseil linking
            -DASCIICHAT_SHARED_DEPS=${ASCIICHAT_SHARED_DEPS}
        )

        # Pass CMAKE_PREFIX_PATH so WebRTC can find system Abseil (Homebrew, vcpkg, etc.)
        if(CMAKE_PREFIX_PATH)
            list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
        endif()

        # For musl builds, add target triple and disable FORTIFY_SOURCE
        # WebRTC must be built with the same target as the main binary to avoid glibc dependencies
        if(USE_MUSL)
            # Determine musl target architecture
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(MUSL_ARCH "aarch64")
            elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7")
                set(MUSL_ARCH "armv7")
            else()
                set(MUSL_ARCH "x86_64")
            endif()
            set(WEBRTC_MUSL_TARGET "${MUSL_ARCH}-linux-musl")

            # Completely replace CMAKE_C_FLAGS and CMAKE_CXX_FLAGS for WebRTC musl builds
            # Don't inherit any FORTIFY_SOURCE settings from parent build environment
            # The musl target triple ensures glibc-specific code paths are not used
            # Use libc++ to match the main build (Alpine libc++ for musl)
            set(_webrtc_c_flags "-O3 -target ${WEBRTC_MUSL_TARGET}")
            set(_webrtc_cxx_flags "-O3 -target ${WEBRTC_MUSL_TARGET} -stdlib=libc++")
            list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_webrtc_c_flags}")
            list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_webrtc_cxx_flags}")
            message(STATUS "WebRTC will be built for musl target: ${WEBRTC_MUSL_TARGET}")
        endif()

        # On macOS with Homebrew LLVM, set explicit flags for WebRTC build
        # We DON'T inherit CMAKE_C/CXX_FLAGS because they may contain -resource-dir
        # from clang config files, and we need to set it to the correct Cellar path
        if(APPLE AND CMAKE_CXX_COMPILER MATCHES "clang")
            get_filename_component(LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            get_filename_component(LLVM_ROOT "${LLVM_BIN_DIR}/.." ABSOLUTE)
            set(CLANG_RESOURCE_DIR "${LLVM_ROOT}/lib/clang")

            # Get SDK path - needed for libc++ headers
            # CMAKE_OSX_SYSROOT is set by the main build
            if(CMAKE_OSX_SYSROOT)
                set(_sysroot_flag "-isysroot ${CMAKE_OSX_SYSROOT}")
            else()
                # Fallback: try to detect SDK
                execute_process(
                    COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE _detected_sdk
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if(_detected_sdk)
                    set(_sysroot_flag "-isysroot ${_detected_sdk}")
                else()
                    set(_sysroot_flag "")
                endif()
            endif()

            # Check for libc++ include path (needed for C++ wrapper headers)
            set(LIBCXX_INCLUDE_DIR "${LLVM_ROOT}/include/c++/v1")
            if(EXISTS "${LIBCXX_INCLUDE_DIR}")
                set(_libcxx_include_flag "-isystem ${LIBCXX_INCLUDE_DIR}")
            else()
                set(_libcxx_include_flag "")
            endif()

            # For Homebrew LLVM: explicitly add libc++ include path but NOT -isysroot.
            #
            # Key insight: ARM Macs (Homebrew in /opt/homebrew/) auto-detect paths,
            # but Intel Macs (Homebrew in /usr/local/) need explicit -isystem for libc++.
            #
            # We use -isystem to add libc++ headers at high priority, BEFORE SDK headers.
            # This ensures libc++'s C header wrappers (stddef.h, stdio.h) are found first,
            # so their #include_next chains work correctly.
            #
            # IMPORTANT: We use --no-default-config to prevent clang from loading
            # config files (e.g., ~/.config/clang/<triple>.cfg) that may contain
            # -isysroot or -isystem flags with higher priority than our flags.
            # CI workflows often create these config files with SDK paths.
            set(_webrtc_c_flags "--no-default-config -w")
            set(_webrtc_cxx_flags "--no-default-config -stdlib=libc++ ${_libcxx_include_flag} -w")
            # Remove any inherited CMAKE_*_FLAGS that might contain -resource-dir or -isysroot
            list(FILTER WEBRTC_CMAKE_ARGS EXCLUDE REGEX "^-DCMAKE_C_FLAGS=")
            list(FILTER WEBRTC_CMAKE_ARGS EXCLUDE REGEX "^-DCMAKE_CXX_FLAGS=")
            list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_webrtc_cxx_flags}")
            list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_webrtc_c_flags}")
            message(STATUS "WebRTC macOS build: libc++ include=${LIBCXX_INCLUDE_DIR}")
        endif()

        # On Windows, force Ninja generator to use Clang instead of MSVC
        # Visual Studio generator ignores CMAKE_C_COMPILER and uses cl.exe
        if(WIN32)
            list(PREPEND WEBRTC_CMAKE_ARGS -G Ninja)
            # Prevent ABI detection from trying to link an executable.
            # CMake's Windows-Clang platform adds -nostartfiles -nostdlib to link rules,
            # which causes lld-link to hang during ABI detection.
            list(APPEND WEBRTC_CMAKE_ARGS -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY)
            # Add WIN32_LEAN_AND_MEAN to prevent winsock.h/winsock2.h conflicts
            set(_webrtc_win_c_flags "-DWIN32_LEAN_AND_MEAN")
            set(_webrtc_win_cxx_flags "-DWIN32_LEAN_AND_MEAN")

            # For Debug builds, add sanitizer flags to match the main project
            # Use release CRT settings (_MT _DLL without _DEBUG) because ASan requires release CRT
            # Set _ITERATOR_DEBUG_LEVEL=0 to match the main project's settings
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(_webrtc_sanitizer_flags "-fsanitize=address -fsanitize=undefined -fsanitize=integer -fsanitize=nullability -fsanitize=implicit-conversion -fsanitize=float-divide-by-zero")
                set(_webrtc_crt_flags "-D_MT -D_DLL -D_ITERATOR_DEBUG_LEVEL=0")
                string(APPEND _webrtc_win_c_flags " ${_webrtc_sanitizer_flags} ${_webrtc_crt_flags}")
                string(APPEND _webrtc_win_cxx_flags " ${_webrtc_sanitizer_flags} ${_webrtc_crt_flags}")
                message(STATUS "WebRTC Windows Debug build: Adding sanitizer flags with release CRT")
            endif()

            # On Windows ARM64, explicitly set the processor so Abseil doesn't try
            # to use x86 SIMD intrinsics (-maes, -msse4.1) which fail on ARM64
            # NEON SIMD is still supported and enabled for ARM64
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
                list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_SYSTEM_PROCESSOR=ARM64")
                # Disable x86 SIMD flags - filter out any previously added, then set OFF
                list(FILTER WEBRTC_CMAKE_ARGS EXCLUDE REGEX "^-DENABLE_SIMD_SSE2=")
                list(FILTER WEBRTC_CMAKE_ARGS EXCLUDE REGEX "^-DENABLE_SIMD_SSSE3=")
                list(FILTER WEBRTC_CMAKE_ARGS EXCLUDE REGEX "^-DENABLE_SIMD_AVX2=")
                list(APPEND WEBRTC_CMAKE_ARGS "-DENABLE_SIMD_SSE2=OFF")
                list(APPEND WEBRTC_CMAKE_ARGS "-DENABLE_SIMD_SSSE3=OFF")
                list(APPEND WEBRTC_CMAKE_ARGS "-DENABLE_SIMD_AVX2=OFF")
                # Explicitly clear Abseil's HWAES x64 flags to prevent -maes/-msse4.1
                # Abseil's AbseilConfigureCopts.cmake checks CMAKE_SYSTEM_PROCESSOR for "ARM"
                # but Windows ARM64 reports "ARM64", causing it to fall through to x64 flags
                list(APPEND WEBRTC_CMAKE_ARGS "-DABSL_RANDOM_HWAES_X64_FLAGS=")
                list(APPEND WEBRTC_CMAKE_ARGS "-DABSL_RANDOM_HWAES_MSVC_X64_FLAGS=")
                message(STATUS "WebRTC Windows ARM64 build: disabling x86 SIMD and Abseil HWAES x64 flags, NEON still enabled")
            endif()

            # Pass Windows SDK and MSVC library paths to WebRTC build
            # These are set by WindowsSDK.cmake via link_directories() but that doesn't
            # propagate to external CMake invocations - we need to set LIB environment variable
            if(WINDOWS_KITS_DIR AND WINDOWS_SDK_VERSION)
                if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                    set(_win_arch x64)
                else()
                    set(_win_arch x86)
                endif()
                set(_webrtc_lib_paths "")
                # Windows SDK lib paths
                set(_ucrt_lib "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/ucrt/${_win_arch}")
                set(_um_lib "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/um/${_win_arch}")
                if(EXISTS "${_ucrt_lib}")
                    list(APPEND _webrtc_lib_paths "${_ucrt_lib}")
                endif()
                if(EXISTS "${_um_lib}")
                    list(APPEND _webrtc_lib_paths "${_um_lib}")
                endif()
                # Find MSVC lib path (same logic as WindowsSDK.cmake)
                set(_msvc_base_paths
                    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Tools/MSVC"
                )
                foreach(_msvc_base IN LISTS _msvc_base_paths)
                    if(EXISTS "${_msvc_base}")
                        file(GLOB _msvc_versions "${_msvc_base}/*")
                        if(_msvc_versions)
                            list(SORT _msvc_versions COMPARE NATURAL ORDER DESCENDING)
                            list(GET _msvc_versions 0 _msvc_latest)
                            set(_msvc_lib_candidate "${_msvc_latest}/lib/${_win_arch}")
                            if(EXISTS "${_msvc_lib_candidate}")
                                list(APPEND _webrtc_lib_paths "${_msvc_lib_candidate}")
                                break()
                            endif()
                        endif()
                    endif()
                endforeach()
                # Convert to Windows-style path list (semicolons) for LIB env var
                string(REPLACE "/" "\\" _webrtc_lib_paths_win "${_webrtc_lib_paths}")
                string(REPLACE ";" ";" WEBRTC_LIB_ENV "${_webrtc_lib_paths_win}")
                message(STATUS "WebRTC Windows build: LIB paths set for linker")
            endif()

            list(FILTER WEBRTC_CMAKE_ARGS EXCLUDE REGEX "^-DCMAKE_C_FLAGS=")
            list(FILTER WEBRTC_CMAKE_ARGS EXCLUDE REGEX "^-DCMAKE_CXX_FLAGS=")
            list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_webrtc_win_c_flags}")
            list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_webrtc_win_cxx_flags}")
            # For Debug builds with ASan, use MultiThreadedDLL (required by ASan)
            # For Release builds, use MultiThreaded (static) to match main project
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
                message(STATUS "WebRTC Windows Debug build: using DLL CRT (ASan requirement)")
            else()
                list(APPEND WEBRTC_CMAKE_ARGS "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
                message(STATUS "WebRTC Windows Release build: using static CRT")
            endif()
        endif()

        # Build WebRTC at configure time (not part of main build)
        # On Windows, set LIB environment variable so lld-link can find SDK/MSVC libs
        if(WIN32 AND WEBRTC_LIB_ENV)
            set(ENV{LIB} "${WEBRTC_LIB_ENV}")
        endif()

        # Explicitly set generator to match parent build (for non-Windows, prepend to args)
        if(NOT WIN32)
            list(PREPEND WEBRTC_CMAKE_ARGS "-G${CMAKE_GENERATOR}")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND}
                ${WEBRTC_CMAKE_ARGS}
                ${webrtc_aec3_SOURCE_DIR}
            WORKING_DIRECTORY "${WEBRTC_BUILD_DIR}"
            RESULT_VARIABLE WEBRTC_CONFIG_RESULT
        )

        if(NOT WEBRTC_CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure WebRTC AEC3")
        endif()

        # Build it
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE} -j${CMAKE_BUILD_PARALLEL_LEVEL}
            WORKING_DIRECTORY "${WEBRTC_BUILD_DIR}"
            RESULT_VARIABLE WEBRTC_BUILD_RESULT
        )

        if(NOT WEBRTC_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build WebRTC AEC3")
        endif()

        # Write build config marker for future cache validation
        file(WRITE "${WEBRTC_CONFIG_MARKER}" "${WEBRTC_BUILD_CONFIG}")
        message(STATUS "${BoldGreen}WebRTC AEC3${ColorReset} libraries built and cached successfully (${WEBRTC_BUILD_CONFIG})")
    else()
        message(STATUS "${BoldGreen}WebRTC AEC3${ColorReset} libraries found in cache: ${BoldCyan}${WEBRTC_BUILD_DIR}/lib${ColorReset}")
    endif()

endif()

# Import pre-built libraries as INTERFACE library
# This way, WebRTC targets are NOT part of the main project's target list
set(WEBRTC_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/webrtc_aec3-build")

# Windows doesn't use "lib" prefix, Unix does
if(WIN32)
    set(_webrtc_lib_prefix "")
else()
    set(_webrtc_lib_prefix "lib")
endif()

set(WEBRTC_AUDIO_PROCESS_LIB "${WEBRTC_BUILD_DIR}/lib/${_webrtc_lib_prefix}AudioProcess${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(WEBRTC_AEC3_LIB "${WEBRTC_BUILD_DIR}/lib/${_webrtc_lib_prefix}aec3${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(WEBRTC_API_LIB "${WEBRTC_BUILD_DIR}/lib/${_webrtc_lib_prefix}api${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(WEBRTC_BASE_LIB "${WEBRTC_BUILD_DIR}/lib/${_webrtc_lib_prefix}base${CMAKE_STATIC_LIBRARY_SUFFIX}")

add_library(webrtc_audio_processing INTERFACE)

if(APPLE)
    # macOS uses -force_load to embed all symbols from static libraries
    # For Release builds, use static libc++; for Debug/Dev, use dynamic -lc++
    if(ASCIICHAT_STATIC_LIBCXX_LIBS)
        set(_webrtc_cxx_libs ${ASCIICHAT_STATIC_LIBCXX_LIBS})
    else()
        set(_webrtc_cxx_libs "-lc++")
    endif()
    target_link_libraries(webrtc_audio_processing INTERFACE
        -force_load "${WEBRTC_AUDIO_PROCESS_LIB}"
        -force_load "${WEBRTC_AEC3_LIB}"
        -force_load "${WEBRTC_API_LIB}"
        -force_load "${WEBRTC_BASE_LIB}"
        ${_webrtc_cxx_libs}
    )
elseif(WIN32)
    # Windows with Clang: Use /WHOLEARCHIVE linker flag via -Wl
    # lld-link accepts /WHOLEARCHIVE:<lib> to include all symbols
    target_link_libraries(webrtc_audio_processing INTERFACE
        -Wl,/WHOLEARCHIVE:${WEBRTC_AUDIO_PROCESS_LIB}
        -Wl,/WHOLEARCHIVE:${WEBRTC_AEC3_LIB}
        -Wl,/WHOLEARCHIVE:${WEBRTC_API_LIB}
        -Wl,/WHOLEARCHIVE:${WEBRTC_BASE_LIB}
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
        # Don't link stdlib for musl builds - Alpine libc++ is linked via link_alpine_libcxx()
        $<$<NOT:$<BOOL:${USE_MUSL}>>:stdc++>
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

# Link against Abseil (matches what base library uses)
# The base library links against absl::strings, absl::base, absl::optional via find_package
# Since we import the static .a files, we need to also link Abseil here for transitive deps
# For musl builds, skip system Abseil (shared .so files are glibc-linked, incompatible with static musl)
# and use the musl-built static Abseil from MuslDependencies.cmake instead
if(NOT USE_MUSL)
    find_package(absl QUIET CONFIG)
endif()
if(USE_MUSL)
    # Use musl-built static Abseil libraries
    set(MUSL_ABSEIL_LIB_DIR "${MUSL_DEPS_DIR_STATIC}/abseil/lib")
    file(GLOB MUSL_ABSEIL_LIBS "${MUSL_ABSEIL_LIB_DIR}/libabsl_*.a")
    if(MUSL_ABSEIL_LIBS)
        target_link_libraries(webrtc_audio_processing INTERFACE ${MUSL_ABSEIL_LIBS})
        list(LENGTH MUSL_ABSEIL_LIBS _absl_count)
        message(STATUS "  WebRTC AEC3: Linking against ${_absl_count} musl-built static Abseil libraries")
    else()
        message(WARNING "  WebRTC AEC3: No musl-built Abseil libraries found in ${MUSL_ABSEIL_LIB_DIR}")
    endif()
elseif(absl_FOUND)
    # On Windows with Clang, vcpkg's Abseil CMake targets include MSVC-specific flags
    # like -ignore:4221 that Clang doesn't understand. Link directly to the DLL instead.
    if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Find abseil_dll.lib in vcpkg
        find_library(ABSEIL_DLL_LIB abseil_dll HINTS "${CMAKE_PREFIX_PATH}/lib" "${CMAKE_PREFIX_PATH}/debug/lib")
        if(ABSEIL_DLL_LIB)
            target_link_libraries(webrtc_audio_processing INTERFACE ${ABSEIL_DLL_LIB})
            message(STATUS "  WebRTC AEC3: Linking against Abseil DLL (Clang-compatible)")
        else()
            # Fallback: try CMake targets anyway
            target_link_libraries(webrtc_audio_processing INTERFACE
                absl::strings
                absl::base
                absl::optional
            )
            message(STATUS "  WebRTC AEC3: Linking against system Abseil (CMake targets)")
        endif()
    else()
        target_link_libraries(webrtc_audio_processing INTERFACE
            absl::strings
            absl::base
            absl::optional
        )
        message(STATUS "  WebRTC AEC3: Linking against system Abseil")
    endif()
else()
    # No system Abseil - use bundled Abseil libraries from WebRTC build
    # Collect all absl_absl_*.lib files from the WebRTC build directory
    file(GLOB WEBRTC_BUNDLED_ABSL_LIBS "${WEBRTC_BUILD_DIR}/lib/absl_absl_*${CMAKE_STATIC_LIBRARY_SUFFIX}")
    if(WEBRTC_BUNDLED_ABSL_LIBS)
        target_link_libraries(webrtc_audio_processing INTERFACE ${WEBRTC_BUNDLED_ABSL_LIBS})
        list(LENGTH WEBRTC_BUNDLED_ABSL_LIBS _absl_count)
        message(STATUS "  WebRTC AEC3: Linking against ${_absl_count} bundled Abseil libraries")
    else()
        message(WARNING "  WebRTC AEC3: No Abseil libraries found (bundled or system)")
    endif()
endif()

message(STATUS "  ${BoldGreen}✓ WebRTC AEC3 configured and built successfully${ColorReset}")
message(STATUS "  Source dir: ${webrtc_aec3_SOURCE_DIR}")
message(STATUS "  Library dir: ${WEBRTC_BUILD_DIR}/lib")
message(STATUS "${BoldGreen}WebRTC AEC3 audio processing ready${ColorReset}")
