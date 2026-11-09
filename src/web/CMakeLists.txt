# WebAssembly Build Configuration (Mirror and Client Modes)
# Built as a separate project via execute_process with Emscripten toolchain

# Only call project() if being configured as a standalone project (via execute_process with Emscripten)
# When included as a subdirectory, the parent project() takes care of initialization
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Being used as a subdirectory - don't call project() again
else()
    # Being used as a standalone project - initialize CMake
    cmake_minimum_required(VERSION 3.30)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/install/Version.cmake")
    version_detect()
    project(ascii-chat-wasm VERSION ${PROJECT_VERSION_FROM_GIT})
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "WASM build type" FORCE)
    endif()
    version_setup_targets()

    # Include sanitizer configuration so apply_sanitizers_to_target() is available
    # Compute path to cmake/platform relative to this file location
    get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../cmake" ABSOLUTE)
    if(EXISTS "${CMAKE_DIR}/platform/Sanitizers.cmake")
        include(${CMAKE_DIR}/platform/Sanitizers.cmake)
    endif()
endif()

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(EMSCRIPTEN)
    find_program(ASCIICHAT_CCACHE_EXECUTABLE NAMES ccache)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/compiler/CCache.cmake")
endif()

# Required for Emscripten
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Note: EMSCRIPTEN_BUILD must be applied PER-TARGET, not globally
# Global add_definitions() affects all targets including native dependency builds (Opus, etc)
# Apply only to WASM executables and common library below

# Inherit FETCHCONTENT_BASE_DIR and ASCIICHAT_DEPS_CACHE_ROOT from parent cmake
# These are passed via -D flags in the WASM execute_process call (CMakeLists.txt)
# Fallback: compute repo root from CMAKE_CURRENT_SOURCE_DIR (src/web) by going up two levels
if(NOT FETCHCONTENT_BASE_DIR)
    get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(FETCHCONTENT_BASE_DIR "${_repo_root}/.deps-cache/src" CACHE PATH "FetchContent source cache" FORCE)
endif()
if(NOT ASCIICHAT_DEPS_CACHE_ROOT)
    get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(ASCIICHAT_DEPS_CACHE_ROOT "${_repo_root}/.deps-cache" CACHE PATH "Dependency cache root" FORCE)
endif()

# Standalone and parent-driven builds use the same build-local artifact layout.
if(NOT WEB_DEPS_CACHE_DIR)
    set(WEB_DEPS_CACHE_DIR "${CMAKE_BINARY_DIR}/_deps/legacy/${CMAKE_BUILD_TYPE}"
        CACHE PATH "WASM dependency build/install cache" FORCE)
endif()

# Include all WASM dependency configuration files
# Each file handles EMSCRIPTEN builds and sets output variables:
# - FOO_LIBRARIES: libraries to link
# - FOO_INCLUDE_DIRS: include directories
include(FetchContent)
get_filename_component(_cmake_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake" ABSOLUTE)
include(${_cmake_dir}/deps/Libsodium.cmake)
include(${_cmake_dir}/deps/PCRE2.cmake)
include(${_cmake_dir}/deps/ICU.cmake)
include(${_cmake_dir}/deps/Opus.cmake)
include(${_cmake_dir}/deps/Zstd.cmake)
include(${_cmake_dir}/deps/Libvterm.cmake)
include(${_cmake_dir}/deps/FFTW.cmake)
include(${_cmake_dir}/deps/Utf8proc.cmake)

# TOML (using tomlc17 from deps)
# No FetchContent needed - using vendored tomlc17

# Path to main ascii-chat library
set(LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)
set(INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
set(DEPS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../deps)

include(${_cmake_dir}/utils/SourceRoot.cmake)
get_filename_component(_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
asciichat_configure_source_root("${_source_root}" "${CMAKE_BINARY_DIR}")

# Include directories for library headers
# IMPORTANT: WASM stubs must come FIRST to override native headers
include_directories(
    ${CMAKE_BINARY_DIR}/generated
    ${LIB_ROOT}/platform/wasm/stubs  # WASM stub headers (override native)
    ${INCLUDE_ROOT}
    ${LIB_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common  # For session/*.h relative includes
    ${DEPS_ROOT}
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc
    ${DEPS_ROOT}/ascii-chat-deps/yyjson/src
    # Dependency includes from cmake/deps/*.cmake output variables
    ${VTERM_INCLUDE_DIRS}  # libvterm
    ${LIBSODIUM_INCLUDE_DIRS}  # libsodium
    ${PCRE2_INCLUDE_DIRS}  # pcre2
    ${OPUS_INCLUDE_DIRS}  # opus
    ${ZSTD_INCLUDE_DIRS}  # zstd
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
    ${LIB_ROOT}/platform/wasm/terminal.c
    ${LIB_ROOT}/platform/wasm/thread.c
    ${LIB_ROOT}/platform/wasm/filesystem.c
    ${LIB_ROOT}/platform/wasm/system.c        # Provides real platform_write() override for xterm.js
    ${LIB_ROOT}/platform/wasm/time.c
    ${LIB_ROOT}/platform/wasm/string.c
    ${LIB_ROOT}/platform/wasm/lifecycle.c
    ${LIB_ROOT}/platform/process_title.c
    ${LIB_ROOT}/platform/filesystem.c
    ${LIB_ROOT}/platform/wasm/stubs/util.c
    ${LIB_ROOT}/platform/wasm/stubs/actions.c
    ${LIB_ROOT}/platform/wasm/stubs/manpage.c
    ${LIB_ROOT}/platform/wasm/stubs/video.c
    ${LIB_ROOT}/platform/wasm/stubs/mode_defaults.c
    ${LIB_ROOT}/platform/wasm/stubs/backtrace.c
    ${LIB_ROOT}/platform/wasm/stubs/log.c
    ${LIB_ROOT}/platform/wasm/stubs/audio.c
    ${LIB_ROOT}/platform/wasm/stubs/network.c
    ${LIB_ROOT}/platform/wasm/stubs/misc.c
    ${LIB_ROOT}/platform/wasm/stubs/comprehensive.c
    ${LIB_ROOT}/platform/wasm/ffmpeg_decoder.c
    ${LIB_ROOT}/video/webcam/wasm/webcam_wasm.c
)

# WASM rendering components (libvterm + FreeType)
# Uses the cross-platform terminal renderer instead of WASM-specific implementation
set(WASM_RENDERER_SRC
    ${LIB_ROOT}/video/ascii/zalgo.c
    ${LIB_ROOT}/media/playlist.c
    ${LIB_ROOT}/media/input.c
    ${LIB_ROOT}/media/render/terminal.c
)

# Third-party dependencies (source-included)
set(TOML_SRC
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src/tomlc17.c
)

# Generated font data (created by cmake/targets/Fonts.cmake at configure time)
# Fonts are generated in the parent build directory before WASM configure runs
# Note: In web builds, CMAKE_BINARY_DIR points to build/web/, but fonts are generated in root build/
get_filename_component(PARENT_BUILD_DIR "${CMAKE_BINARY_DIR}/.." ABSOLUTE)

# Use parent cmake binary dir if provided (from execute_process in parent CMakeLists.txt)
if(DEFINED PARENT_CMAKE_BINARY_DIR)
    set(PARENT_BUILD_DIR "${PARENT_CMAKE_BINARY_DIR}")
    message(STATUS "WASM: Using parent build directory: ${PARENT_BUILD_DIR}")
else()
    message(STATUS "WASM: Using computed parent build directory: ${PARENT_BUILD_DIR}")
endif()

set(GENERATED_FONT_FILE "${PARENT_BUILD_DIR}/generated/data/fonts/default.c")
set(GENERATED_FONT_MATRIX_FILE "${PARENT_BUILD_DIR}/generated/data/fonts/matrix_resurrected.c")

# Fonts are guaranteed to exist because Fonts.cmake generates them at configure time
# before WASM configure runs (fonts are a dependency of WASM build)
set(GENERATED_FONT_SRC "${GENERATED_FONT_FILE}" "${GENERATED_FONT_MATRIX_FILE}")
message(STATUS "WASM: Using generated font at ${GENERATED_FONT_FILE}")

# Library source files common to both mirror and client modes
set(LIB_SRC_COMMON
    ${CMAKE_CURRENT_SOURCE_DIR}/common/errors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/session/frame_validation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/session/av_state.c
    # Core
    ${LIB_ROOT}/common.c
    ${LIB_ROOT}/asciichat_errno.c
    ${LIB_ROOT}/buffer.c
    ${LIB_ROOT}/video/ascii/rle.c

    # Options system
    ${LIB_ROOT}/options/rcu.c
    ${LIB_ROOT}/../deps/ascii-chat-deps/yyjson/src/yyjson.c
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
    ${LIB_ROOT}/options/registry/debug.c

    # Crypto (needed for logging headers)
    ${LIB_ROOT}/crypto/crypto.c
    ${LIB_ROOT}/crypto/record.c
    ${LIB_ROOT}/crypto/rekey.c
    ${LIB_ROOT}/crypto/session.c
    ${LIB_ROOT}/crypto/policy.c

    # Video processing (needed by logging and config systems)
    ${LIB_ROOT}/video/ascii/ascii.c
    ${LIB_ROOT}/video/terminal/ansi.c
    ${LIB_ROOT}/video/rgba/color_filter.c
    ${LIB_ROOT}/video/rgba/video_frame.c
    ${LIB_ROOT}/video/ascii/palette.c
    ${LIB_ROOT}/video/ascii/output_buffer.c
    ${LIB_ROOT}/video/ascii/sgr.c
    ${LIB_ROOT}/video/ascii/render.c
    ${LIB_ROOT}/video/ascii/simd/cells.c
    ${LIB_ROOT}/video/ascii/simd/dispatch.c
    ${LIB_ROOT}/platform/cpu.c
    ${LIB_ROOT}/video/ascii/common.c
    ${LIB_ROOT}/video/ascii/scalar/background.c
    ${LIB_ROOT}/video/ascii/scalar/foreground.c
    ${LIB_ROOT}/video/ascii/scalar/halfblock.c

    # Utilities (minimal set needed by options)
    ${LIB_ROOT}/util/format.c
    ${LIB_ROOT}/util/flow_control.c
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
    ${LIB_ROOT}/debug/stats.c
    ${LIB_ROOT}/debug/errno.c
    ${LIB_ROOT}/debug/debug_helpers.c
    ${LIB_ROOT}/debug/atomic.c
    ${LIB_ROOT}/debug/mutex.c

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
    ${LIB_ROOT}/log/wasm.c

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
    ${LIB_ROOT}/video/anim/test_pattern.c

    # Shared session UI; scheduling and media side effects belong to the browser host.
    ${LIB_ROOT}/tui/session.c
    ${LIB_ROOT}/tui/session_status.c
    ${LIB_ROOT}/tui/terminal_screen.c
    ${LIB_ROOT}/platform/keyboard.c
    ${LIB_ROOT}/tui/session_input.c
    ${LIB_ROOT}/tui/session_help.c
    ${LIB_ROOT}/tui/session_splash.c
    ${LIB_ROOT}/tui/invitation.c
    ${LIB_ROOT}/tui/too_small.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/session/session_log_buffer.c
    ${LIB_ROOT}/ui/output.c
    ${LIB_ROOT}/ui/text.c
    ${LIB_ROOT}/ui/popup.c
    ${LIB_ROOT}/ui/help_menu.c
    ${LIB_ROOT}/ui/viewport.c
    ${LIB_ROOT}/ui/selectable_list.c
    ${LIB_ROOT}/ui/scrollbar.c
    ${LIB_ROOT}/ui/table.c
    ${LIB_ROOT}/ui/buttons.c
    ${LIB_ROOT}/ui/layout.c
    ${LIB_ROOT}/ui/input.c
    ${LIB_ROOT}/ui/progress.c
    ${LIB_ROOT}/tui/shortcuts.c
    ${LIB_ROOT}/ui/frame_buffer.c
    ${LIB_ROOT}/ui/fps_counter.c
    ${LIB_ROOT}/stats/stats.c
    ${LIB_ROOT}/stats/descriptors.c
    ${LIB_ROOT}/platform/wasm/stubs/stats.c

    # Video animations (used by display.c in both mirror and client modes)
    ${LIB_ROOT}/video/anim/digital_rain.c
    ${LIB_ROOT}/video/anim/controller.c
)

# Mirror-specific sources (not needed by client mode)
set(LIB_SRC_MIRROR_ONLY
    ${LIB_ROOT}/util/aspect_ratio.c    # Video scaling - mirror only
    ${LIB_ROOT}/audio/visualization.c
    ${LIB_ROOT}/video/anim/waveform.c
    ${LIB_ROOT}/video/anim/fft.c
)

# Client-specific sources (crypto + network)
set(LIB_SRC_CLIENT_CRYPTO
    ${LIB_ROOT}/crypto/handshake/common.c
    ${LIB_ROOT}/crypto/handshake/client.c
    ${LIB_ROOT}/crypto/handshake/server.c
    ${LIB_ROOT}/crypto/regex.c
    ${LIB_ROOT}/crypto/ssh/ssh_keys.c
    ${LIB_ROOT}/crypto/gpg/openpgp.c
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
    ${LIB_ROOT}/network/acip/traffic.c
    ${LIB_ROOT}/network/acip/prepare.c
    ${LIB_ROOT}/network/acip/security.c
    ${LIB_ROOT}/network/browser/transport.c
    ${LIB_ROOT}/network/browser/security.c
)

# Common WASM abstraction layer (shared between mirror and client)
set(COMMON_WEB_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/font.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/capture.c
)

# Compile shared lib sources once for both WASM targets (OBJECT library)
add_library(wasm-common OBJECT
    # Debug features are not available in WASM environment
    ${COMMON_WEB_SRC}
    ${PLATFORM_SRC}
    ${LIB_SRC_COMMON}
    ${WASM_RENDERER_SRC}
    ${TOML_SRC}
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
# Suppress regparm attribute errors from system pthread.h in Emscripten builds
target_compile_options(wasm-common PRIVATE -Wno-error -Qunused-arguments)
target_link_options(wasm-common PRIVATE -pthread -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=4 -Wno-error=pthreads-mem-growth)

# Add FreeType support via the Emscripten port
if(DEFINED EMSCRIPTEN)
    target_compile_options(wasm-common PRIVATE -sUSE_FREETYPE=1)
    target_link_options(wasm-common PRIVATE -sUSE_FREETYPE=1)
endif()

# Link dependencies using output variables from cmake/deps/*.cmake
# These are set by Libsodium.cmake, PCRE2.cmake, Libvterm.cmake, etc.

add_dependencies(wasm-common generate_version)
target_link_libraries(wasm-common PUBLIC asciichat-icu)

target_link_libraries(wasm-common PRIVATE
    ${UTF8PROC_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${PCRE2_LIBRARIES}
    ${VTERM_LDFLAGS}
)

target_include_directories(wasm-common PRIVATE
    ${LIBSODIUM_INCLUDE_DIRS}
    ${PCRE2_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${VTERM_INCLUDE_DIRS}
)

# NOTE: FFmpeg is loaded at runtime via ffmpeg.wasm (npm package)
# See web/web/package.json for ffmpeg.wasm dependency

# Apply sanitizers
apply_sanitizers_to_target(wasm-common)

set(WEB_SRC_FACE_FOLLOW
    ${LIB_ROOT}/video/face_follow.c
)
include(${_cmake_dir}/deps/FaceDetection.cmake)

# Create WASM mirror module
add_executable(mirror-web
    ${WEB_SRC_MIRROR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/session_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/mirror_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/terminal_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/face.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/test_pattern.cpp
    ${LIB_SRC_MIRROR_ONLY}
    ${WEB_SRC_FACE_FOLLOW}
    $<TARGET_OBJECTS:wasm-common>
)
ascii_chat_add_face_detector(mirror-web)

# Apply EMSCRIPTEN_BUILD and mode-specific define
target_compile_definitions(mirror-web PRIVATE
    __EMSCRIPTEN__
    EMSCRIPTEN_BUILD=1
)

# Enable Emscripten's pthread support and SIMD for video processing
target_compile_options(mirror-web PRIVATE -pthread -msimd128 -sUSE_FREETYPE=1)
target_link_options(mirror-web PRIVATE -pthread -sUSE_PTHREADS=1 "-sPTHREAD_POOL_SIZE=Module['pthreadPoolSize']??4" -Wno-error=pthreads-mem-growth -sUSE_FREETYPE=1)

# Link dependencies with explicit library paths for wasm-ld

target_link_libraries(mirror-web PRIVATE
    asciichat-icu
    ${UTF8PROC_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${PCRE2_LIBRARIES}
    ${VTERM_LDFLAGS}
)

# Apply sanitizers
apply_sanitizers_to_target(mirror-web)

# Source files for WASM client entry point
# The browser schedules the shared session host's packet and video steps.
# Native client entry points and their blocking main loops remain excluded.
set(WEB_SRC_CLIENT
    # Local file previews share the mirror frame converter.
    ${CMAKE_CURRENT_SOURCE_DIR}/mirror.c
    ${LIB_ROOT}/audio/visualization.c
    ${LIB_ROOT}/video/anim/waveform.c
    ${LIB_ROOT}/video/anim/fft.c
    ${LIB_ROOT}/audio/processor.c
    ${LIB_ROOT}/audio/format.c
    ${LIB_ROOT}/audio/effects.c
    ${LIB_ROOT}/audio/spectral.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_wasm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client.c
    ${LIB_ROOT}/network/browser/session.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/session/host.c
    ${LIB_ROOT}/network/acip/server.c
    ${LIB_ROOT}/network/acip/settings.c
    ${LIB_ROOT}/network/acip/acds_client.c
    ${LIB_ROOT}/network/consensus/election.c
    ${LIB_ROOT}/network/consensus/handoff.c
    ${LIB_ROOT}/audio/opus.c
    ${LIB_ROOT}/ringbuffer.c
    ${LIB_ROOT}/util/aspect_ratio.c
    ${LIB_ROOT}/platform/wasm/stubs/crypto.c # Client-only stubs for known-hosts and prompts unavailable in browsers
)

# Create WASM client module
add_executable(client-web
    ${WEB_SRC_CLIENT}
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/session_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/mirror_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/terminal_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/face.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/test_pattern.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/audio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/client_opts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/browser_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/networking.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/key_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/av_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/client_frames.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/discovery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/session_host.cpp
    ${LIB_ROOT}/network/consensus/handoff.c
    ${LIB_SRC_CLIENT_CRYPTO}
    ${LIB_SRC_CLIENT_NETWORK}
    ${WEB_SRC_FACE_FOLLOW}
    $<TARGET_OBJECTS:wasm-common>
)
ascii_chat_add_face_detector(client-web)

# Apply EMSCRIPTEN_BUILD and mode-specific define
target_compile_definitions(client-web PRIVATE
    __EMSCRIPTEN__
    EMSCRIPTEN_BUILD=1
)

# Enable Emscripten's pthread support and FreeType, add library search paths
target_compile_options(client-web PRIVATE -pthread -sUSE_FREETYPE=1)
target_link_options(client-web PRIVATE
    -pthread -sUSE_PTHREADS=1 "-sPTHREAD_POOL_SIZE=Module['pthreadPoolSize']??4" -Wno-error=pthreads-mem-growth -sUSE_FREETYPE=1
)

# Add library search paths if needed

# Add include directories for dependencies (needed because wasm-common object files don't auto-inherit INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(client-web PRIVATE
    ${FFTW_INCLUDE_DIRS}
    ${LIBSODIUM_INCLUDE_DIRS}
    ${PCRE2_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
)

# Link dependencies for client mode
target_link_libraries(client-web PRIVATE
    asciichat-icu
    ${UTF8PROC_LIBRARIES}
    ${FFTW_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${PCRE2_LIBRARIES}
    ${OPUS_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${VTERM_LDFLAGS}
)

# Apply sanitizers
apply_sanitizers_to_target(client-web)

# Embind is the JavaScript API. FS and the current heap view are retained for
# key-file loading and detecting input arrays that alias growable WASM memory.
set(EXPORTED_RUNTIME_METHODS_CSV "HEAPU8,FS")

# Emscripten writes declarations as a side effect of linking. Ninja must know
# about them so a missing declaration causes the link to run again.
add_custom_command(TARGET mirror-web POST_BUILD
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mirror-bindings.d.ts"
    COMMAND ${CMAKE_COMMAND} -E true)
add_custom_command(TARGET client-web POST_BUILD
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/client-bindings.d.ts"
    COMMAND ${CMAKE_COMMAND} -E true)

target_link_options(mirror-web PRIVATE
    -lembind
    --emit-tsd "${CMAKE_CURRENT_BINARY_DIR}/mirror-bindings.d.ts"
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    "-sEXPORT_NAME=MirrorModule"
    "-sEXPORTED_FUNCTIONS=[]"
    "-sEXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS_CSV}"
    -sENVIRONMENT=web,worker
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=32MB
    -sMAXIMUM_MEMORY=128MB
    -sSTACK_SIZE=2097152
    -sUSE_WEBGL2=1
    -sFULL_ES2=1
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    -sASSERTIONS=1
    -msimd128
    -O3
    --no-entry
    -Wno-js-compiler
    -Wno-pthreads-mem-growth
)

target_link_options(client-web PRIVATE
    -lembind
    --emit-tsd "${CMAKE_CURRENT_BINARY_DIR}/client-bindings.d.ts"
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    "-sEXPORT_NAME=ClientModule"
    "-sEXPORTED_FUNCTIONS=[]"
    "-sEXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS_CSV}"
    -sENVIRONMENT=web,worker
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=32MB
    -sMAXIMUM_MEMORY=256MB
    -sSTACK_SIZE=2097152
    -sUSE_WEBGL2=1
    -sFULL_ES2=1
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    -sASSERTIONS=1
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

    add_custom_target(web
        DEPENDS mirror-web client-web
        COMMENT "All WASM modules built"
    )
endif()

# Enable SIMD at compilation, not only linking, for the pixel compositor.
set_source_files_properties("${LIB_ROOT}/media/render/terminal.c"
    PROPERTIES COMPILE_OPTIONS "-msimd128")

if(EMSCRIPTEN)
    enable_testing()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../tests/wasm/dependencies"
        "${CMAKE_CURRENT_BINARY_DIR}/tests/dependencies" EXCLUDE_FROM_ALL)
endif()

# Standalone embeddable renderer. Its artifacts stay in the build tree until
# the npm package copies them into its ignored dist directory.
add_executable(renderer-web
    ${CMAKE_CURRENT_SOURCE_DIR}/common/opts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/frame.cpp
    ${WEB_SRC_MIRROR}
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/terminal_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/test_pattern.cpp
    ${LIB_SRC_MIRROR_ONLY}
    $<TARGET_OBJECTS:wasm-common>
)
target_compile_definitions(renderer-web PRIVATE __EMSCRIPTEN__ EMSCRIPTEN_BUILD=1)
target_compile_options(renderer-web PRIVATE -pthread -msimd128 -sUSE_FREETYPE=1)
target_link_libraries(renderer-web PRIVATE asciichat-icu ${UTF8PROC_LIBRARIES} ${LIBSODIUM_LIBRARIES} ${PCRE2_LIBRARIES} ${VTERM_LDFLAGS})
add_custom_command(TARGET renderer-web POST_BUILD
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/renderer-bindings.d.ts"
    COMMAND ${CMAKE_COMMAND} -E true)
target_link_options(renderer-web PRIVATE
    --emit-tsd "${CMAKE_CURRENT_BINARY_DIR}/renderer-bindings.d.ts"
    -pthread -sUSE_PTHREADS=1 "-sPTHREAD_POOL_SIZE=Module['pthreadPoolSize']??4" -Wno-error=pthreads-mem-growth -sUSE_FREETYPE=1
    -lembind -sMODULARIZE=1 -sEXPORT_ES6=1 "-sEXPORT_NAME=RendererModule"
    "-sEXPORTED_FUNCTIONS=[]" "-sEXPORTED_RUNTIME_METHODS=HEAPU8,FS"
    -sENVIRONMENT=web,worker -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=32MB -sMAXIMUM_MEMORY=128MB
    -sSTACK_SIZE=2097152 -sASSERTIONS=1 -msimd128 -O3 --no-entry -Wno-js-compiler -Wno-pthreads-mem-growth
)
set_target_properties(renderer-web PROPERTIES OUTPUT_NAME "renderer")
apply_sanitizers_to_target(renderer-web)

target_sources(mirror-web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common/opts.cpp ${CMAKE_CURRENT_SOURCE_DIR}/bindings/frame.cpp)
target_sources(client-web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common/opts.cpp ${CMAKE_CURRENT_SOURCE_DIR}/bindings/frame.cpp)

# The WASM project is configured as its own Emscripten CMake project, so it
# does not pass through the parent project's defer finalization hook. Apply
# the same source transformation here before the nested targets compile; the
# shared crypto/session C sources now intentionally reject untransformed
# defer() calls.
if(EMSCRIPTEN)
    set(_ascii_defer_nested_source_dir "${CMAKE_SOURCE_DIR}")
    set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    include(${_cmake_dir}/tooling/Defer.cmake)
    # The nested Emscripten project does not inherit the parent project's
    # defer setup. Use the Python backend here: the WASM toolchain provides
    # clang and libclang.dll, but not LLVM's C++ development headers needed
    # by the native transformer.
    set(ASCIICHAT_DEFER_BACKEND PYTHON CACHE STRING "Defer transformer backend" FORCE)
    set(_ascii_defer_batch_stage web)
    set(_ascii_defer_plan_records "${CMAKE_BINARY_DIR}/defer-records-${_ascii_defer_batch_stage}.txt")
    file(WRITE "${_ascii_defer_plan_records}" "")
    find_program(ASCIICHAT_DEFER_UV_EXECUTABLE NAMES uv REQUIRED)
    set(ASCIICHAT_DEFER_PYTHON_COMMAND
        "${CMAKE_COMMAND};-E;env;UV_PROJECT_ENVIRONMENT=${CMAKE_BINARY_DIR}/defer-python-venv;${ASCIICHAT_DEFER_UV_EXECUTABLE};run;--project;${CMAKE_SOURCE_DIR}/src/tooling/defer;--frozen;--managed-python;python"
        CACHE INTERNAL "uv command for the Python defer backend" FORCE)
    if(NOT ASCIICHAT_LIBCLANG_PATH)
        get_filename_component(_ascii_defer_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        find_file(_ascii_defer_libclang NAMES libclang.dll
            HINTS "${_ascii_defer_compiler_dir}" "${_ascii_defer_compiler_dir}/../lib"
            NO_CACHE)
        if(NOT _ascii_defer_libclang)
            message(FATAL_ERROR "defer requires LLVM 19 or newer libclang; set ASCIICHAT_LIBCLANG_PATH")
        endif()
        set(ASCIICHAT_LIBCLANG_PATH "${_ascii_defer_libclang}" CACHE FILEPATH "LLVM 19 or newer libclang shared library" FORCE)
    endif()
    execute_process(
        COMMAND ${ASCIICHAT_DEFER_PYTHON_COMMAND} -c
            "import sys; assert sys.version_info >= (3, 10), 'Python 3.10 or newer is required'; sys.path.insert(0, sys.argv[1]); from clang import cindex; cindex.Config.set_compatibility_check(False); cindex.Config.set_library_file(sys.argv[2]); print(cindex.conf.get_clang_version())"
            "${CMAKE_SOURCE_DIR}/src/tooling/defer/vendor" "${ASCIICHAT_LIBCLANG_PATH}"
        RESULT_VARIABLE _ascii_defer_clang_check_result OUTPUT_VARIABLE _ascii_defer_clang_check_output ERROR_VARIABLE _ascii_defer_clang_check_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _ascii_defer_clang_check_result EQUAL 0)
        message(FATAL_ERROR "defer libclang check failed: ${_ascii_defer_clang_check_output} ${_ascii_defer_clang_check_error}")
    endif()
    ascii_defer_scan_targets(_defer_sources wasm-common mirror-web client-web renderer-web)
    foreach(_ascii_defer_target wasm-common mirror-web client-web renderer-web)
        ascii_defer_apply_target("${_ascii_defer_target}")
    endforeach()
    ascii_defer_finish_batches()
    set(CMAKE_SOURCE_DIR "${_ascii_defer_nested_source_dir}")
endif()
