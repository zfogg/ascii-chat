# WebAssembly Build Configuration (Mirror and Client Modes)
# Built as a separate project via execute_process with Emscripten toolchain

# Only call project() if being configured as a standalone project (via execute_process with Emscripten)
# When included as a subdirectory, the parent project() takes care of initialization
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Being used as a subdirectory - don't call project() again
else()
    # Being used as a standalone project - initialize CMake
    cmake_minimum_required(VERSION 3.18)
    project(ascii-chat-wasm)

    # Include sanitizer configuration so apply_sanitizers_to_target() is available
    # Compute path to cmake/platform relative to this file location
    get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../cmake" ABSOLUTE)
    if(EXISTS "${CMAKE_DIR}/platform/Sanitizers.cmake")
        include(${CMAKE_DIR}/platform/Sanitizers.cmake)
    endif()
endif()

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Required for Emscripten
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Note: EMSCRIPTEN_BUILD must be applied PER-TARGET, not globally
# Global add_definitions() affects all targets including native dependency builds (Opus, etc)
# Apply only to WASM executables and common library below

# Cache WASM dependencies outside build/ to persist across rebuilds
# Note: WASM artifacts are build-type independent, shared across all configurations
if(NOT ASCIICHAT_WEB_DEPS_DIR)
    set(ASCIICHAT_WEB_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps-cache/web")
endif()
file(MAKE_DIRECTORY "${ASCIICHAT_WEB_DEPS_DIR}")

# Fetch and build dependencies for WASM
include(FetchContent)

# Pass Emscripten toolchain to FetchContent sub-builds via per-package CMAKE_ARGS
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE)
    set(FetchContent_Toolchain_Args
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    )
else()
    set(FetchContent_Toolchain_Args
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    )
endif()

set(Sodium_CMAKE_ARGS ${FetchContent_Toolchain_Args} -DCMAKE_INSTALL_PREFIX=${SODIUM_PREFIX})
set(pcre2_CMAKE_ARGS ${FetchContent_Toolchain_Args} -DCMAKE_INSTALL_PREFIX=${PCRE2_PREFIX})
set(Opus_CMAKE_ARGS ${FetchContent_Toolchain_Args} -DCMAKE_INSTALL_PREFIX=${OPUS_PREFIX})
set(zstd_CMAKE_ARGS ${FetchContent_Toolchain_Args} -DCMAKE_INSTALL_PREFIX=${ZSTD_PREFIX})
set(raylib_CMAKE_ARGS ${FetchContent_Toolchain_Args} -DCMAKE_INSTALL_PREFIX=${RAYLIB_PREFIX})

# Suppress verbose configure output from dependency builds
set(CMAKE_MESSAGE_LOG_LEVEL_BACKUP "${CMAKE_MESSAGE_LOG_LEVEL}")
set(CMAKE_MESSAGE_LOG_LEVEL "WARNING")
set(FETCHCONTENT_QUIET ON)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Set FetchContent base dir to our cache
set(FETCHCONTENT_BASE_DIR "${ASCIICHAT_WEB_DEPS_DIR}/_deps")

# ==============================================================================
# Simplified approach: just use FetchContent with proper caching via disk
# ==============================================================================
# The dependencies are cached in .deps-cache via FETCHCONTENT_BASE_DIR
# On subsequent builds, FetchContent finds them and reuses them (no rebuild)

set(SODIUM_PREFIX "${ASCIICHAT_WEB_DEPS_DIR}")
set(PCRE2_PREFIX "${ASCIICHAT_WEB_DEPS_DIR}")
set(OPUS_PREFIX "${ASCIICHAT_WEB_DEPS_DIR}")
set(ZSTD_PREFIX "${ASCIICHAT_WEB_DEPS_DIR}")
set(RAYLIB_PREFIX "${ASCIICHAT_WEB_DEPS_DIR}")

if(BUILD_WEB)
    # libsodium - check cache BEFORE FetchContent declaration
    # Support both old and new cache locations for backwards compatibility
    set(sodium_a_path "${FETCHCONTENT_BASE_DIR}/sodium-build/libsodium.a")
    if(EXISTS "${sodium_a_path}")
        message(STATUS "Using cached Sodium: ${sodium_a_path}")
        if(NOT TARGET sodium)
            add_library(sodium STATIC IMPORTED GLOBAL)
            set_target_properties(sodium PROPERTIES
                IMPORTED_LOCATION "${sodium_a_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${FETCHCONTENT_BASE_DIR}/sodium-src/libsodium/src/libsodium/include"
            )
        endif()
    else()
        FetchContent_Declare(Sodium
            GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
            GIT_TAG e5b985ad0dd235d8c4307ea3a385b45e76c74c6a
            UPDATE_DISCONNECTED ON
            CMAKE_ARGS ${Sodium_CMAKE_ARGS}
        )
        set(SODIUM_DISABLE_TESTS ON)
        FetchContent_MakeAvailable(Sodium)
    endif()

    # PCRE2 - check cache BEFORE FetchContent declaration
    if(EXISTS "${FETCHCONTENT_BASE_DIR}/pcre2-build/libpcre2-8.a")
        message(STATUS "Using cached PCRE2: ${FETCHCONTENT_BASE_DIR}/pcre2-build/libpcre2-8.a")
        if(NOT TARGET pcre2-8)
            add_library(pcre2-8 STATIC IMPORTED GLOBAL)
            set_target_properties(pcre2-8 PROPERTIES
                IMPORTED_LOCATION "${FETCHCONTENT_BASE_DIR}/pcre2-build/libpcre2-8.a"
                INTERFACE_INCLUDE_DIRECTORIES "${FETCHCONTENT_BASE_DIR}/pcre2-build;${FETCHCONTENT_BASE_DIR}/pcre2-src"
            )
        endif()
    else()
        FetchContent_Declare(pcre2
            GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
            GIT_TAG pcre2-10.44
            UPDATE_DISCONNECTED ON
            CMAKE_ARGS ${pcre2_CMAKE_ARGS}
        )
        set(PCRE2_BUILD_TESTS OFF CACHE BOOL "Build PCRE2 tests")
        set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "Build pcre2grep")
        FetchContent_MakeAvailable(pcre2)
    endif()

    # Opus - check cache BEFORE FetchContent declaration
    if(EXISTS "${FETCHCONTENT_BASE_DIR}/opus-build/libopus.a")
        message(STATUS "Using cached Opus: ${FETCHCONTENT_BASE_DIR}/opus-build/libopus.a")
        if(NOT TARGET opus)
            add_library(opus STATIC IMPORTED GLOBAL)
            set_target_properties(opus PROPERTIES
                IMPORTED_LOCATION "${FETCHCONTENT_BASE_DIR}/opus-build/libopus.a"
                INTERFACE_INCLUDE_DIRECTORIES "${FETCHCONTENT_BASE_DIR}/opus-src/include"
            )
        endif()
    else()
        FetchContent_Declare(Opus
            GIT_REPOSITORY https://github.com/xiph/opus.git
            GIT_TAG v1.5.2
            UPDATE_DISCONNECTED ON
            CMAKE_ARGS ${Opus_CMAKE_ARGS}
        )
        set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Build Opus programs")
        set(OPUS_BUILD_TESTING OFF CACHE BOOL "Build Opus tests")
        set(OPUS_INSTALL OFF CACHE BOOL "Install Opus")
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        FetchContent_MakeAvailable(Opus)
    endif()

    # zstd - check cache BEFORE FetchContent declaration
    if(EXISTS "${FETCHCONTENT_BASE_DIR}/zstd-build/lib/libzstd.a")
        message(STATUS "Using cached zstd: ${FETCHCONTENT_BASE_DIR}/zstd-build/lib/libzstd.a")
        if(NOT TARGET zstd::libzstd)
            add_library(zstd::libzstd STATIC IMPORTED GLOBAL)
            set_target_properties(zstd::libzstd PROPERTIES
                IMPORTED_LOCATION "${FETCHCONTENT_BASE_DIR}/zstd-build/lib/libzstd.a"
                INTERFACE_INCLUDE_DIRECTORIES "${FETCHCONTENT_BASE_DIR}/zstd-src/lib"
            )
        endif()
    else()
        FetchContent_Declare(zstd
            GIT_REPOSITORY https://github.com/facebook/zstd.git
            GIT_TAG v1.5.5
            SOURCE_SUBDIR build/cmake
            UPDATE_DISCONNECTED ON
            CMAKE_ARGS ${zstd_CMAKE_ARGS}
        )
        set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build zstd programs")
        set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build zstd tests")
        set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Build shared library")
        set(ZSTD_BUILD_STATIC ON CACHE BOOL "Build static library")
        set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "Disable zstd threading for WASM")
        FetchContent_MakeAvailable(zstd)
    endif()

    # raylib - check cache BEFORE FetchContent declaration
    if(EXISTS "${FETCHCONTENT_BASE_DIR}/raylib_wasm-build/libraylib.a")
        message(STATUS "Using cached raylib: ${FETCHCONTENT_BASE_DIR}/raylib_wasm-build/libraylib.a")
        if(NOT TARGET raylib)
            add_library(raylib STATIC IMPORTED GLOBAL)
            set_target_properties(raylib PROPERTIES
                IMPORTED_LOCATION "${FETCHCONTENT_BASE_DIR}/raylib_wasm-build/libraylib.a"
                INTERFACE_INCLUDE_DIRECTORIES "${FETCHCONTENT_BASE_DIR}/raylib_wasm-src/src"
            )
        endif()
    else()
        FetchContent_Declare(raylib_wasm
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            UPDATE_DISCONNECTED ON
            CMAKE_ARGS ${raylib_CMAKE_ARGS}
        )
        set(PLATFORM "Web" CACHE STRING "" FORCE)
        set(GRAPHICS "GRAPHICS_API_OPENGL_ES2" CACHE STRING "" FORCE)
        FetchContent_MakeAvailable(raylib_wasm)
    endif()
endif()

# ==============================================================================
# libvterm - handled by Libvterm.cmake
# ==============================================================================
# Download and extract libvterm source (no CMakeLists.txt build step)
set(libvterm_wasm_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/libvterm-src")
set(libvterm_wasm_ARCHIVE "${FETCHCONTENT_BASE_DIR}/libvterm-0.3.3.tar.gz")

if(NOT EXISTS "${libvterm_wasm_SOURCE_DIR}")
    # Download tarball if not cached
    if(NOT EXISTS "${libvterm_wasm_ARCHIVE}")
        message(STATUS "Downloading libvterm v0.3.3...")
        file(DOWNLOAD
            "https://github.com/neovim/libvterm/archive/refs/tags/v0.3.3.tar.gz"
            "${libvterm_wasm_ARCHIVE}"
            TIMEOUT 60
            STATUS DOWNLOAD_STATUS
        )
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_CODE)
        if(NOT DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download libvterm (code ${DOWNLOAD_CODE})")
        endif()
    endif()

    # Create temporary extraction directory
    set(libvterm_EXTRACT_DIR "${FETCHCONTENT_BASE_DIR}/libvterm-extract-tmp")
    file(MAKE_DIRECTORY "${libvterm_EXTRACT_DIR}")

    message(STATUS "Extracting libvterm...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${libvterm_wasm_ARCHIVE}"
        WORKING_DIRECTORY "${libvterm_EXTRACT_DIR}"
        RESULT_VARIABLE TAR_RESULT
    )
    if(NOT TAR_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract libvterm (code ${TAR_RESULT})")
    endif()

    # Move extracted content to final location
    # GitHub releases extract to repo-version/ format (libvterm-0.3.3/)
    if(EXISTS "${libvterm_EXTRACT_DIR}/libvterm-0.3.3")
        file(RENAME "${libvterm_EXTRACT_DIR}/libvterm-0.3.3" "${libvterm_wasm_SOURCE_DIR}")
    else()
        message(FATAL_ERROR "Extracted libvterm not found in ${libvterm_EXTRACT_DIR}")
    endif()
endif()

message(STATUS "libvterm source at: ${libvterm_wasm_SOURCE_DIR}")

set(CMAKE_SOURCE_DIR_PARENT "${CMAKE_CURRENT_SOURCE_DIR}/../../")

# Check if libvterm.a is already cached - if so, skip full cmake build
set(libvterm_cached_a "${libvterm_wasm_SOURCE_DIR}/libvterm.a")
if(EXISTS "${libvterm_cached_a}")
    message(STATUS "Using cached libvterm: ${libvterm_cached_a}")
    set(VTERM_LDFLAGS "${libvterm_cached_a}")
    set(VTERM_INCLUDE_DIRS "${libvterm_wasm_SOURCE_DIR}/include")
else()
    # No cached .a file, need to build it
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/deps/Libvterm.cmake")
endif()

# Restore original message log level
set(CMAKE_MESSAGE_LOG_LEVEL "${CMAKE_MESSAGE_LOG_LEVEL_BACKUP}")

# TOML (using tomlc17 from deps)
# No FetchContent needed - using vendored tomlc17

# Path to main ascii-chat library
set(LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)
set(INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
set(DEPS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../deps)

# Include directories for library headers
# IMPORTANT: WASM stubs must come FIRST to override native headers
include_directories(
    ${LIB_ROOT}/platform/wasm/stubs  # WASM stub headers (override native)
    ${INCLUDE_ROOT}
    ${LIB_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common  # For session/*.h relative includes
    ${DEPS_ROOT}
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc
    ${libvterm_wasm_SOURCE_DIR}/include  # libvterm headers
    ${raylib_wasm_SOURCE_DIR}/src  # raylib headers
    # FetchContent dependency includes (needed for all targets)
    ${FETCHCONTENT_BASE_DIR}/sodium-src/libsodium/src/libsodium/include
    ${FETCHCONTENT_BASE_DIR}/pcre2-build
    ${FETCHCONTENT_BASE_DIR}/pcre2-src
    ${FETCHCONTENT_BASE_DIR}/opus-src/include
    ${FETCHCONTENT_BASE_DIR}/zstd-src/lib
    # FreeType for rendering
    /usr/include/freetype2
)

# Source files for WASM mirror entry point
# IMPORTANT: Do NOT include src/mirror/main.c or session/*.c files
# Those implement terminal-based rendering and blocking main loops that hang in WASM.
# JavaScript handles the render loop by calling mirror_convert_frame() directly.
set(WEB_SRC_MIRROR
    ${CMAKE_CURRENT_SOURCE_DIR}/main_wasm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mirror.c
)

# Platform abstraction layer for WASM
set(PLATFORM_SRC
    ${LIB_ROOT}/platform/wasm/init.c
    ${LIB_ROOT}/platform/wasm/console.c
    ${LIB_ROOT}/platform/wasm/terminal.c
    ${LIB_ROOT}/platform/wasm/threading.c
    ${LIB_ROOT}/platform/wasm/system.c        # Provides real platform_write() override for xterm.js
    ${LIB_ROOT}/platform/wasm/time.c
    ${LIB_ROOT}/platform/wasm/string.c
    ${LIB_ROOT}/platform/wasm/lifecycle.c
    ${LIB_ROOT}/platform/wasm/stubs/util.c
    ${LIB_ROOT}/platform/wasm/stubs/actions.c
    ${LIB_ROOT}/platform/wasm/stubs/terminal.c
    ${LIB_ROOT}/platform/wasm/stubs/manpage.c
    ${LIB_ROOT}/platform/wasm/stubs/filesystem.c  # Provides filesystem stubs (platform_write removed)
    ${LIB_ROOT}/platform/wasm/stubs/video.c
    ${LIB_ROOT}/platform/wasm/stubs/mode_defaults.c
    ${LIB_ROOT}/platform/wasm/stubs/crypto.c
    ${LIB_ROOT}/platform/wasm/stubs/backtrace.c
    ${LIB_ROOT}/platform/wasm/stubs/log.c
    ${LIB_ROOT}/platform/wasm/stubs/audio.c
    ${LIB_ROOT}/platform/wasm/stubs/network.c
    ${LIB_ROOT}/platform/wasm/stubs/threading.c
    ${LIB_ROOT}/platform/wasm/stubs/misc.c
    ${LIB_ROOT}/platform/wasm/stubs/comprehensive.c
    ${LIB_ROOT}/platform/wasm/ffmpeg_decoder.c
    ${LIB_ROOT}/video/webcam/wasm/webcam_wasm.c
)

# WASM rendering components (libvterm + FreeType + raylib)
set(WASM_RENDERER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/ascii_renderer.c
)

# Third-party dependencies (source-included)
set(TOML_SRC
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src/tomlc17.c
)

set(UTF8PROC_SRC
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc/utf8proc.c
)

# Generated font data (created by cmake/targets/Fonts.cmake)
# Note: In web builds, CMAKE_BINARY_DIR points to build/web/, but fonts are generated in root build/
# Get the parent build directory
get_filename_component(PARENT_BUILD_DIR "${CMAKE_BINARY_DIR}/.." ABSOLUTE)
set(GENERATED_FONT_SRC
    "${PARENT_BUILD_DIR}/generated/data/fonts/default.c"
)

# Library source files common to both mirror and client modes
set(LIB_SRC_COMMON
    # Core
    ${LIB_ROOT}/common.c
    ${LIB_ROOT}/asciichat_errno.c
    ${LIB_ROOT}/buffer_pool.c

    # Options system
    ${LIB_ROOT}/options/rcu.c
    ${LIB_ROOT}/options/options.c
    ${LIB_ROOT}/options/common.c
    ${LIB_ROOT}/options/enums.c
    ${LIB_ROOT}/options/strings.c
    ${LIB_ROOT}/options/parsing/parsers.c
    ${LIB_ROOT}/options/parsing/validation.c
    ${LIB_ROOT}/options/colorscheme.c
    ${LIB_ROOT}/options/manpage.c
    ${LIB_ROOT}/options/layout.c
    ${LIB_ROOT}/options/levenshtein.c
    ${LIB_ROOT}/options/help_api.c
    ${LIB_ROOT}/options/completions/completions.c
    ${LIB_ROOT}/options/completions/bash.c
    ${LIB_ROOT}/options/completions/fish.c
    ${LIB_ROOT}/options/completions/zsh.c
    ${LIB_ROOT}/options/completions/powershell.c
    ${LIB_ROOT}/options/builder/builder.c
    ${LIB_ROOT}/options/builder/handlers.c
    ${LIB_ROOT}/options/builder/help.c
    ${LIB_ROOT}/options/manpage/formatter.c
    ${LIB_ROOT}/options/manpage/merger.c
    ${LIB_ROOT}/options/manpage/parser.c
    ${LIB_ROOT}/options/manpage/resources.c
    ${LIB_ROOT}/options/manpage/content/environment.c
    ${LIB_ROOT}/options/manpage/content/examples.c
    ${LIB_ROOT}/options/manpage/content/options.c
    ${LIB_ROOT}/options/manpage/content/positional.c
    ${LIB_ROOT}/options/manpage/content/usage.c
    ${LIB_ROOT}/options/config/config.c
    ${LIB_ROOT}/options/config/presets.c
    ${LIB_ROOT}/options/config/schema.c
    ${LIB_ROOT}/options/registry/audio.c
    ${LIB_ROOT}/options/registry/configuration.c
    ${LIB_ROOT}/options/registry/core.c
    ${LIB_ROOT}/options/registry/database.c
    ${LIB_ROOT}/options/registry/display.c
    ${LIB_ROOT}/options/registry/general.c
    ${LIB_ROOT}/options/registry/logging.c
    ${LIB_ROOT}/options/registry/media.c
    ${LIB_ROOT}/options/registry/metadata.c
    ${LIB_ROOT}/options/registry/network.c
    ${LIB_ROOT}/options/registry/public_api.c
    ${LIB_ROOT}/options/registry/registry.c
    ${LIB_ROOT}/options/registry/security.c
    ${LIB_ROOT}/options/registry/terminal.c
    ${LIB_ROOT}/options/registry/webcam.c

    # Crypto (needed for logging headers)
    ${LIB_ROOT}/crypto/crypto.c

    # Video processing (needed by logging and config systems)
    ${LIB_ROOT}/video/ascii/ascii.c
    ${LIB_ROOT}/video/terminal/ansi.c
    ${LIB_ROOT}/video/rgba/color_filter.c
    ${LIB_ROOT}/video/rgba/video_frame.c
    ${LIB_ROOT}/video/ascii/palette.c
    ${LIB_ROOT}/video/ascii/output_buffer.c
    ${LIB_ROOT}/video/ascii/common.c
    ${LIB_ROOT}/video/ascii/scalar/background.c
    ${LIB_ROOT}/video/ascii/scalar/foreground.c
    ${LIB_ROOT}/video/ascii/scalar/halfblock.c

    # Utilities (minimal set needed by options)
    ${LIB_ROOT}/util/format.c
    ${LIB_ROOT}/util/string.c
    ${LIB_ROOT}/util/utf8.c
    ${LIB_ROOT}/util/display.c
    ${LIB_ROOT}/util/env.c
    ${LIB_ROOT}/util/pcre2.c
    ${LIB_ROOT}/util/time.c
    ${LIB_ROOT}/util/path.c
    ${LIB_ROOT}/util/url.c
    ${LIB_ROOT}/util/ip.c
    ${LIB_ROOT}/util/parsing.c
    ${LIB_ROOT}/util/fps.c

    # Discovery (for session string validation)
    ${LIB_ROOT}/discovery/strings.c
    ${LIB_ROOT}/discovery/adjectives.c
    ${LIB_ROOT}/discovery/nouns.c

    # Debug/naming (required by crypto handshake and other modules)
    ${LIB_ROOT}/debug/named.c
    ${LIB_ROOT}/debug/sync.c

    # Note: platform/system.c is NOT included here - it's included by the WASM-specific
    # wasm/system.c file, which follows the same pattern as posix/system.c and windows/system.c

    # Logging (real logging system with full header formatting)
    ${LIB_ROOT}/log/log.c
    ${LIB_ROOT}/log/format.c
    ${LIB_ROOT}/log/colorize.c
    ${LIB_ROOT}/log/named.c
    ${LIB_ROOT}/log/grep.c
    ${LIB_ROOT}/log/search.c
    ${LIB_ROOT}/log/io.c
    ${LIB_ROOT}/log/raylib.c

    # Debug/memory tracking (sync/mutex/atomic debugging excluded for WASM)
    ${LIB_ROOT}/debug/memory.c
    ${LIB_ROOT}/atomic.c

    # Terminal abstraction (using WASM-specific version from PLATFORM_SRC)
    # ${LIB_ROOT}/platform/terminal.c - EXCLUDED: using wasm/terminal.c instead

    # Media source handling (webcam, files, URLs)
    ${LIB_ROOT}/media/source.c
    # FFmpeg support not available in Emscripten - would require ffmpeg.wasm integration
    # ${LIB_ROOT}/media/ffmpeg_decoder.c
    # ${LIB_ROOT}/media/ffmpeg_encoder.c
    ${LIB_ROOT}/media/yt_dlp.c

    # Video/image handling (needed by webcam and ffmpeg backends)
    ${LIB_ROOT}/video/rgba/image.c

    # UI components (splash screen, status screen, keyboard help)
    ${LIB_ROOT}/ui/splash.c
    ${LIB_ROOT}/ui/status.c
    ${LIB_ROOT}/ui/keyboard_help.c
    ${LIB_ROOT}/ui/terminal_screen.c
    ${LIB_ROOT}/ui/frame_buffer.c
    ${LIB_ROOT}/ui/fps_counter.c

    # Video animations (used by display.c in both mirror and client modes)
    ${LIB_ROOT}/video/anim/digital_rain.c
)

# Mirror-specific sources (not needed by client mode)
set(LIB_SRC_MIRROR_ONLY
    ${LIB_ROOT}/video/ascii/rle.c            # RLE codec for server ASCII frames - mirror only
    ${LIB_ROOT}/util/aspect_ratio.c    # Video scaling - mirror only
)

# Client-specific sources (crypto + network)
# Note: Only minimal crypto handshake for web client (no SSH keys, known_hosts, etc.)
set(LIB_SRC_CLIENT_CRYPTO
    ${LIB_ROOT}/crypto/handshake/common.c
    ${LIB_ROOT}/crypto/handshake/client.c
)

set(LIB_SRC_CLIENT_NETWORK
    ${LIB_ROOT}/network/network.c
    ${LIB_ROOT}/network/packet/packet.c
    ${LIB_ROOT}/network/packet/parsing.c
    ${LIB_ROOT}/network/crc32.c
    ${LIB_ROOT}/network/compression.c
    ${LIB_ROOT}/network/errors.c
    ${LIB_ROOT}/network/frame_validator.c
    ${LIB_ROOT}/network/acip/send.c
)

# Common WASM abstraction layer (shared between mirror and client)
set(COMMON_WEB_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/options.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/init.c
)

# Compile shared lib sources once for both WASM targets (OBJECT library)
add_library(wasm-common OBJECT
    # Debug features are not available in WASM environment
    ${COMMON_WEB_SRC}
    ${PLATFORM_SRC}
    ${LIB_SRC_COMMON}
    ${WASM_RENDERER_SRC}
    ${TOML_SRC}
    ${UTF8PROC_SRC}
    ${GENERATED_FONT_SRC}
)


# Ensure font generation happens before compiling wasm-common
if(TARGET generate_fonts)
    add_dependencies(wasm-common generate_fonts)
endif()

# Apply EMSCRIPTEN_BUILD only to WASM targets (not globally)
target_compile_definitions(wasm-common PRIVATE
    __EMSCRIPTEN__
    EMSCRIPTEN_BUILD=1
)

# Enable Emscripten's pthread support
target_compile_options(wasm-common PRIVATE -pthread)
target_link_options(wasm-common PRIVATE -pthread -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=4 -Wno-error=pthreads-mem-growth)

# Add FreeType via Emscripten port, libvterm, and raylib support
if(DEFINED EMSCRIPTEN)
    target_compile_options(wasm-common PRIVATE -sUSE_FREETYPE=1)
    target_link_options(wasm-common PRIVATE -sUSE_FREETYPE=1)
endif()

# Libvterm was built by cmake/deps/Libvterm.cmake when EMSCRIPTEN is defined
# VTERM_LDFLAGS and VTERM_INCLUDE_DIRS are set by that file

# Link dependencies for shared library with explicit library paths
target_link_options(wasm-common PRIVATE
    -L${FETCHCONTENT_BASE_DIR}/sodium-build
    -L${FETCHCONTENT_BASE_DIR}/pcre2-build
    -L${FETCHCONTENT_BASE_DIR}/raylib_wasm-build
)

target_link_libraries(wasm-common PRIVATE
    sodium
    pcre2-8
    ${VTERM_LDFLAGS}
    raylib
)

target_include_directories(wasm-common PRIVATE
    ${VTERM_INCLUDE_DIRS}
)

# NOTE: FFmpeg is loaded at runtime via ffmpeg.wasm (npm package)
# See web/web/package.json for ffmpeg.wasm dependency

# Add include directories for wasm-common (FetchContent handles these via IMPORTED targets)
target_include_directories(wasm-common PRIVATE
    ${FETCHCONTENT_BASE_DIR}/sodium-src/libsodium/src/libsodium/include
    ${FETCHCONTENT_BASE_DIR}/pcre2-build
    ${FETCHCONTENT_BASE_DIR}/pcre2-src
    ${FETCHCONTENT_BASE_DIR}/opus-src/include
    ${FETCHCONTENT_BASE_DIR}/zstd-src/lib
    ${FETCHCONTENT_BASE_DIR}/raylib_wasm-src/src
)

# Apply sanitizers
apply_sanitizers_to_target(wasm-common)

# Create WASM mirror module
message("DEBUG: WEB_SRC_MIRROR=${WEB_SRC_MIRROR}")
message("DEBUG: LIB_SRC_MIRROR_ONLY=${LIB_SRC_MIRROR_ONLY}")
message("DEBUG: CMAKE_CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(mirror-web
    ${WEB_SRC_MIRROR}
    ${LIB_SRC_MIRROR_ONLY}
    $<TARGET_OBJECTS:wasm-common>
)

# Apply EMSCRIPTEN_BUILD and mode-specific define
target_compile_definitions(mirror-web PRIVATE
    __EMSCRIPTEN__
    EMSCRIPTEN_BUILD=1
    BUILD_WASM_MIRROR=1
)

# Enable Emscripten's pthread support and SIMD for video processing
target_compile_options(mirror-web PRIVATE -pthread -msimd128 -sUSE_FREETYPE=1)
target_link_options(mirror-web PRIVATE -pthread -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=4 -Wno-error=pthreads-mem-growth -sUSE_FREETYPE=1)

# Link dependencies with explicit library paths for wasm-ld
target_link_options(mirror-web PRIVATE
    -L${FETCHCONTENT_BASE_DIR}/sodium-build
    -L${FETCHCONTENT_BASE_DIR}/pcre2-build
    -L${FETCHCONTENT_BASE_DIR}/raylib_wasm-build/raylib
)

target_link_libraries(mirror-web PRIVATE
    sodium
    pcre2-8
    ${VTERM_LDFLAGS}
    raylib
)

# Apply sanitizers
apply_sanitizers_to_target(mirror-web)

# Source files for WASM client entry point
# IMPORTANT: Do NOT include src/client/main.c or session/*.c files
# Those implement terminal-based rendering and blocking main loops that hang in WASM.
# JavaScript handles the render loop by calling client_convert_frame() directly.
set(WEB_SRC_CLIENT
    ${CMAKE_CURRENT_SOURCE_DIR}/main_wasm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/client/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/client/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/client/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/client/keepalive.c
)

# Create WASM client module
add_executable(client-web
    ${WEB_SRC_CLIENT}
    ${LIB_SRC_CLIENT_CRYPTO}
    ${LIB_SRC_CLIENT_NETWORK}
    $<TARGET_OBJECTS:wasm-common>
)

# Apply EMSCRIPTEN_BUILD and mode-specific define
target_compile_definitions(client-web PRIVATE
    __EMSCRIPTEN__
    EMSCRIPTEN_BUILD=1
    BUILD_WASM_CLIENT=1
)

# Enable Emscripten's pthread support and FreeType, add library search paths
target_compile_options(client-web PRIVATE -pthread -sUSE_FREETYPE=1)
target_link_options(client-web PRIVATE
    -pthread -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=4 -Wno-error=pthreads-mem-growth -sUSE_FREETYPE=1
    -L${FETCHCONTENT_BASE_DIR}/sodium-build
    -L${FETCHCONTENT_BASE_DIR}/pcre2-build
    -L${FETCHCONTENT_BASE_DIR}/opus-build
    -L${FETCHCONTENT_BASE_DIR}/zstd-build/lib
    -L${FETCHCONTENT_BASE_DIR}/raylib_wasm-build/raylib
)

# Add include directories for dependencies (needed because wasm-common object files don't auto-inherit INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(client-web PRIVATE
    ${FETCHCONTENT_BASE_DIR}/sodium-src/libsodium/src/libsodium/include
    ${FETCHCONTENT_BASE_DIR}/pcre2-src
    ${FETCHCONTENT_BASE_DIR}/opus-src/include
    ${FETCHCONTENT_BASE_DIR}/zstd-src/lib
    ${FETCHCONTENT_BASE_DIR}/raylib_wasm-src/src
)

# Link dependencies for client mode
target_link_libraries(client-web PRIVATE
    sodium
    pcre2-8
    opus
    ${VTERM_LDFLAGS}
    raylib
)

# Apply sanitizers
apply_sanitizers_to_target(client-web)

# Exported functions for mirror module (main() entry point via main_wasm.c)
set(EXPORTED_FUNCTIONS_MIRROR
    # Frame processing for JavaScript control
    _mirror_init_with_args
    _mirror_cleanup
    _mirror_convert_frame
    _mirror_free_string
    _get_help_text
    # ASCII renderer functions (raylib + libvterm + FreeType)
    _ascii_renderer_init
    _ascii_renderer_render_frame
    _ascii_renderer_resize
    _ascii_renderer_get_cols
    _ascii_renderer_get_rows
    _ascii_renderer_shutdown
    # Option getters/setters for live option updates (no restart needed)
    _set_width
    _set_height
    _get_width
    _get_height
    _set_render_mode
    _get_render_mode
    _set_color_mode
    _get_color_mode
    _set_color_filter
    _get_color_filter
    _set_palette
    _get_palette
    _set_palette_chars
    _get_palette_chars
    _set_matrix_rain
    _get_matrix_rain
    _set_flip_x
    _get_flip_x
    _set_flip_y
    _get_flip_y
    _set_target_fps
    _get_target_fps
    _malloc
    _free
)

# Exported functions for client module
set(EXPORTED_FUNCTIONS_CLIENT
    # ASCII renderer functions (raylib + libvterm + FreeType)
    _ascii_renderer_init
    _ascii_renderer_render_frame
    _ascii_renderer_resize
    _ascii_renderer_get_cols
    _ascii_renderer_get_rows
    _ascii_renderer_shutdown
    # Option getters/setters for live option updates (no restart needed)
    _set_width
    _set_height
    _get_width
    _get_height
    _set_color_mode
    _get_color_mode
    _set_color_filter
    _get_color_filter
    _set_palette
    _get_palette
    _set_palette_chars
    _get_palette_chars
    _set_matrix_rain
    _get_matrix_rain
    _set_flip_x
    _get_flip_x
    _set_flip_y
    _get_flip_y
    _set_target_fps
    _get_target_fps
    _malloc
    _free
)

# Convert lists to JSON array format for Emscripten
# Use comma-separated format without outer brackets (emcc will add them)
string(REPLACE ";" "," EXPORTED_FUNCTIONS_MIRROR_CSV "${EXPORTED_FUNCTIONS_MIRROR}")
string(REPLACE ";" "," EXPORTED_FUNCTIONS_CLIENT_CSV "${EXPORTED_FUNCTIONS_CLIENT}")

set(EXPORTED_RUNTIME_METHODS_CSV "ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,getValue,setValue,HEAPU8")

# Use target_link_options for better cross-platform quoting
target_link_options(mirror-web PRIVATE
    -sWASM=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    "-sEXPORT_NAME=MirrorModule"
    "-sEXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS_MIRROR_CSV}"
    "-sEXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS_CSV}"
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=32MB
    -sMAXIMUM_MEMORY=128MB
    -sSTACK_SIZE=2097152
    -sWASM_BIGINT=1
    -sUSE_WEBGL2=1
    -sFULL_ES2=1
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    -msimd128
    -O3
    --no-entry
    -Wno-js-compiler
    -Wno-pthreads-mem-growth
)

target_link_options(client-web PRIVATE
    -sWASM=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    "-sEXPORT_NAME=ClientModule"
    "-sEXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS_CLIENT_CSV}"
    "-sEXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS_CSV}"
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=32MB
    -sMAXIMUM_MEMORY=256MB
    -sSTACK_SIZE=2097152
    -sWASM_BIGINT=1
    -sUSE_WEBGL2=1
    -sFULL_ES2=1
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    -msimd128
    -O3
    --no-entry
    -Wno-js-compiler
    -Wno-pthreads-mem-growth
)

# Set output names to match target names
set_target_properties(mirror-web PROPERTIES OUTPUT_NAME "mirror")
set_target_properties(client-web PROPERTIES OUTPUT_NAME "client")

# Copy outputs to web/src/wasm/dist/ (so Vite can import them)
if(BUILD_WEB)
    add_custom_command(TARGET mirror-web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../../web/web/src/wasm/dist
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/mirror.js
            ${CMAKE_CURRENT_BINARY_DIR}/mirror.wasm
            ${CMAKE_SOURCE_DIR}/../../web/web/src/wasm/dist/
        COMMENT "Copying mirror WASM files to web/src/wasm/dist/"
    )

    add_custom_command(TARGET client-web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../../web/web/src/wasm/dist
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/client.js
            ${CMAKE_CURRENT_BINARY_DIR}/client.wasm
            ${CMAKE_SOURCE_DIR}/../../web/web/src/wasm/dist/
        COMMENT "Copying client WASM files to web/src/wasm/dist/"
    )

# Combined WASM target (builds both mirror and client)
    add_custom_target(web
        DEPENDS mirror-web client-web
        COMMENT "All WASM modules built"
    )
endif()

