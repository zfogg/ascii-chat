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

# Standalone and parent-driven builds use the repository dependency cache.
if(NOT WEB_DEPS_CACHE_DIR OR WEB_DEPS_CACHE_DIR MATCHES "[/\\\\]_deps[/\\\\]legacy[/\\\\]")
    set(WEB_DEPS_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_ROOT}/web/${CMAKE_BUILD_TYPE}"
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

if(NOT TARGET ascii-chat-utf8proc)
    add_library(ascii-chat-utf8proc STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/ascii-chat-deps/utf8proc/utf8proc.c)
    target_include_directories(ascii-chat-utf8proc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/ascii-chat-deps/utf8proc)
    set(UTF8PROC_LIBRARIES ascii-chat-utf8proc)
endif()

# TOML (using tomlc17 from deps)
# No FetchContent needed - using vendored tomlc17

# Path to main ascii-chat library
set(LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)
set(INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
set(DEPS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../deps)
set(LIBPLATFORM_ROOT ${DEPS_ROOT}/ascii-chat-deps/libplatform)
set(LIBFLAGS_ROOT ${ASCIICHAT_DEPS_CACHE_ROOT}/libflags-src)
set(LIBFLAGS_SRC
    ${LIBFLAGS_ROOT}/lib/validation.c
    ${LIBFLAGS_ROOT}/lib/json.c
    ${LIBFLAGS_ROOT}/lib/commands.c
    ${LIBFLAGS_ROOT}/lib/options.c
    ${LIBFLAGS_ROOT}/lib/util/url.c
    ${LIBFLAGS_ROOT}/lib/util/ip.c
    ${LIBFLAGS_ROOT}/lib/log.c
    ${LIBFLAGS_ROOT}/lib/parsing/parsers.c
    ${LIBFLAGS_ROOT}/lib/builder/builder.c
    ${LIBFLAGS_ROOT}/lib/config/config.c
    ${LIBFLAGS_ROOT}/lib/completions/completions.c
    ${LIBFLAGS_ROOT}/lib/manpage/manpage.c
    ${LIBFLAGS_ROOT}/lib/manpage/formatter.c)

include(${_cmake_dir}/utils/SourceRoot.cmake)
get_filename_component(_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
asciichat_configure_source_root("${_source_root}" "${CMAKE_BINARY_DIR}")

# Include directories for library headers
# IMPORTANT: WASM stubs must come FIRST to override native headers
include_directories(
    ${CMAKE_BINARY_DIR}/generated
    ${LIBPLATFORM_ROOT}/include
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs  # WASM stub headers (override native)
    ${LIBFLAGS_ROOT}/include
    ${INCLUDE_ROOT}
    ${DEPS_ROOT}/defer/cpp/include
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
    ${FFTW_INCLUDE_DIRS}  # fftw
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
    ${LIBPLATFORM_ROOT}/lib/wasm/init.c
    ${LIBPLATFORM_ROOT}/lib/wasm/terminal.c
    ${LIBPLATFORM_ROOT}/lib/wasm/thread.c
    ${LIBPLATFORM_ROOT}/lib/wasm/filesystem.c
    ${LIBPLATFORM_ROOT}/lib/wasm/system.c        # Provides real platform_write() override for xterm.js
    ${LIBPLATFORM_ROOT}/lib/time.c
    ${LIBPLATFORM_ROOT}/lib/wasm/time.c
    ${LIBPLATFORM_ROOT}/lib/wasm/lifecycle.c
    ${LIBPLATFORM_ROOT}/lib/wasm/string.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/util.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/actions.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/manpage.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/video.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/mode_defaults.c
    ${LIBPLATFORM_ROOT}/lib/wasm/backtrace.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/log.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/audio.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/network.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/ffmpeg_decoder.c
    ${LIB_ROOT}/video/webcam/wasm/webcam_wasm.c
    ${LIB_ROOT}/util/time.c
    ${LIBFLAGS_SRC}
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
    ${_source_root}/src/common/options/runtime.c
    ${LIB_ROOT}/../deps/ascii-chat-deps/yyjson/src/yyjson.c
    ${_source_root}/src/common/options/options.c
    ${_source_root}/src/common/options/common.c
    ${_source_root}/src/common/options/enums.c
    ${_source_root}/src/common/options/strings.c
    ${_source_root}/src/common/options/parsing/parsers.c
    ${_source_root}/src/common/options/parsing/validation.c
    ${_source_root}/src/common/options/colorscheme.c
    ${_source_root}/src/common/options/manpage.c
    ${_source_root}/src/common/options/layout.c
    ${_source_root}/src/common/options/levenshtein.c
    ${_source_root}/src/common/options/help.c
    ${_source_root}/src/common/options/completions/completions.c
    ${_source_root}/src/common/options/completions/bash.c
    ${_source_root}/src/common/options/completions/fish.c
    ${_source_root}/src/common/options/completions/zsh.c
    ${_source_root}/src/common/options/completions/powershell.c
    ${_source_root}/src/common/options/builder/builder.c
    ${_source_root}/src/common/options/builder/handlers.c
    ${_source_root}/src/common/options/builder/help.c
    ${_source_root}/src/common/options/manpage/formatter.c
    ${_source_root}/src/common/options/manpage/merger.c
    ${_source_root}/src/common/options/manpage/parser.c
    ${_source_root}/src/common/options/manpage/resources.c
    ${_source_root}/src/common/options/manpage/content/environment.c
    ${_source_root}/src/common/options/manpage/content/examples.c
    ${_source_root}/src/common/options/manpage/content/options.c
    ${_source_root}/src/common/options/manpage/content/positional.c
    ${_source_root}/src/common/options/manpage/content/usage.c
    ${_source_root}/src/common/options/config/config.c
    ${_source_root}/src/common/options/config/presets.c
    ${_source_root}/src/common/options/config/schema.c
    ${_source_root}/src/common/options/registry/audio.c
    ${_source_root}/src/common/options/registry/configuration.c
    ${_source_root}/src/common/options/registry/core.c
    ${_source_root}/src/common/options/registry/database.c
    ${_source_root}/src/common/options/registry/display.c
    ${_source_root}/src/common/options/registry/general.c
    ${_source_root}/src/common/options/registry/logging.c
    ${_source_root}/src/common/options/registry/media.c
    ${_source_root}/src/common/options/registry/metadata.c
    ${_source_root}/src/common/options/registry/network.c
    ${_source_root}/src/common/options/registry/public_api.c
    ${_source_root}/src/common/options/registry/registry.c
    ${_source_root}/src/common/options/registry/security.c
    ${_source_root}/src/common/options/registry/terminal.c
    ${_source_root}/src/common/options/registry/webcam.c
    ${_source_root}/src/common/options/registry/debug.c

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
    ${LIBPLATFORM_ROOT}/lib/cpu.c
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
    ${LIB_ROOT}/debug/stats.c
    ${LIB_ROOT}/debug/errno.c
    ${LIB_ROOT}/debug/debug_helpers.c
    ${LIBPLATFORM_ROOT}/lib/debug/atomic.c
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
    ${LIBPLATFORM_ROOT}/lib/atomic.c

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
    ${LIB_ROOT}/tui/session_input.c
    ${LIB_ROOT}/tui/session_help.c
    ${LIB_ROOT}/tui/session_splash.c
    ${LIB_ROOT}/tui/status.c
    ${LIB_ROOT}/tui/update_banner.c
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
    ${LIB_ROOT}/stats/runtime.c

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
    ${LIB_ROOT}/network/tcp/transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/security.c
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
    ${CMAKE_CURRENT_SOURCE_DIR}/peer_crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/session/host.c
    ${LIB_ROOT}/network/acip/server.c
    ${LIB_ROOT}/network/acip/settings.c
    ${LIB_ROOT}/network/acip/acds_client.c
    ${LIB_ROOT}/network/consensus/election.c
    ${LIB_ROOT}/network/consensus/handoff.c
    ${LIB_ROOT}/audio/opus.c
    ${LIB_ROOT}/ringbuffer.c
    ${LIB_ROOT}/util/aspect_ratio.c
    ${LIBPLATFORM_ROOT}/lib/wasm/stubs/crypto.c # Client-only stubs for known-hosts and prompts unavailable in browsers
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
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/peer_crypto.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/networking.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/key_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/av_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/client_frames.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/discovery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bindings/session_host.cpp
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

# This file is configured as a separate CMake project for Emscripten, so the
# parent project's defer finalization cannot see these targets. Reuse the same
# repository-level defer module once, after all browser targets are declared.
if(EMSCRIPTEN AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(NOT DEFER_SOURCE_ROOT OR NOT DEFER_NATIVE_BINARY OR NOT EXISTS "${DEFER_CONFIG_FILE}")
        message(FATAL_ERROR
            "The parent CMake project must provide DEFER_SOURCE_ROOT, DEFER_NATIVE_BINARY, and DEFER_CONFIG_FILE")
    endif()
    set(DEFER_SOURCE_DIRS "include;lib;src;tests" CACHE STRING "Source directories to transform")
    include("${DEFER_CONFIG_FILE}")
    set(_ascii_wasm_defer_targets wasm-common mirror-web client-web renderer-web)
    defer_targets(${_ascii_wasm_defer_targets})
endif()
