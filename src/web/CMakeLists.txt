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
if(ASCIICHAT_WEB_DEPS_DIR)
    set(FETCHCONTENT_BASE_DIR "${ASCIICHAT_WEB_DEPS_DIR}" CACHE PATH "FetchContent cache for web deps")
endif()

# Fetch and build dependencies for WASM
include(FetchContent)

# Pass Emscripten toolchain to FetchContent sub-builds via per-package CMAKE_ARGS
# Only include CMAKE_TOOLCHAIN_FILE if it's actually set (avoid empty -DCMAKE_TOOLCHAIN_FILE=)
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

# Set CMAKE_ARGS for each dependency using the toolchain args
set(Sodium_CMAKE_ARGS ${FetchContent_Toolchain_Args})
set(pcre2_CMAKE_ARGS ${FetchContent_Toolchain_Args})
set(Opus_CMAKE_ARGS ${FetchContent_Toolchain_Args})
set(zstd_CMAKE_ARGS ${FetchContent_Toolchain_Args})

# Suppress verbose configure output from all dependency builds
set(CMAKE_MESSAGE_LOG_LEVEL_BACKUP "${CMAKE_MESSAGE_LOG_LEVEL}")
set(CMAKE_MESSAGE_LOG_LEVEL "WARNING")
set(FETCHCONTENT_QUIET ON)

# Suppress CMake find_package output
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# libsodium
FetchContent_Declare(Sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG e5b985ad0dd235d8c4307ea3a385b45e76c74c6a
)
set(SODIUM_DISABLE_TESTS ON)
if(BUILD_WEB AND NOT sodium_POPULATED)
    FetchContent_MakeAvailable(Sodium)
endif()

# PCRE2
FetchContent_Declare(pcre2
    GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
    GIT_TAG pcre2-10.44
)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "Build PCRE2 tests")
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "Build pcre2grep")
if(BUILD_WEB AND NOT pcre2_POPULATED)
    FetchContent_MakeAvailable(pcre2)
endif()

# Opus codec (for client mode audio encoding/decoding)
FetchContent_Declare(Opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG v1.5.2
)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Build Opus programs")
set(OPUS_BUILD_TESTING OFF CACHE BOOL "Build Opus tests")
set(OPUS_INSTALL OFF CACHE BOOL "Install Opus")
# Set minimum CMake version for Opus
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
if(BUILD_WEB AND NOT opus_POPULATED)
    FetchContent_MakeAvailable(Opus)
endif()

# Ensure Opus source headers take precedence over system headers
if(TARGET opus)
    # Add source includes BEFORE inherited system Opus headers
    target_include_directories(opus BEFORE PRIVATE
        $<BUILD_INTERFACE:${opus_SOURCE_DIR}/silk>
        $<BUILD_INTERFACE:${opus_SOURCE_DIR}/silk/float>
        $<BUILD_INTERFACE:${opus_SOURCE_DIR}/silk/fixed>
    )
endif()

# zstd (for compression in client mode)
FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.5
    SOURCE_SUBDIR build/cmake
)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build zstd programs")
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build zstd tests")
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Build shared library")
set(ZSTD_BUILD_STATIC ON CACHE BOOL "Build static library")
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "Disable zstd threading for WASM")
if(BUILD_WEB AND NOT zstd_POPULATED)
    FetchContent_MakeAvailable(zstd)
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
    ${DEPS_ROOT}
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc
)

# Source files for WASM mirror entry point
set(WEB_SRC_MIRROR
    ${CMAKE_CURRENT_SOURCE_DIR}/mirror.c
)

# Platform abstraction layer for WASM
set(PLATFORM_SRC
    ${LIB_ROOT}/platform/wasm/init.c
    ${LIB_ROOT}/platform/wasm/console.c
    ${LIB_ROOT}/platform/wasm/terminal.c
    ${LIB_ROOT}/platform/wasm/threading.c
    ${LIB_ROOT}/platform/wasm/system.c
    ${LIB_ROOT}/platform/wasm/time.c
    ${LIB_ROOT}/platform/wasm/string.c
    ${LIB_ROOT}/platform/wasm/lifecycle.c
    ${LIB_ROOT}/platform/wasm/log.c
    ${LIB_ROOT}/platform/wasm/stubs/util.c
    ${LIB_ROOT}/platform/wasm/stubs/actions.c
    ${LIB_ROOT}/platform/wasm/stubs/terminal.c
    ${LIB_ROOT}/platform/wasm/stubs/manpage.c
    ${LIB_ROOT}/platform/wasm/stubs/filesystem.c
    ${LIB_ROOT}/platform/wasm/stubs/video.c
    ${LIB_ROOT}/platform/wasm/stubs/mode_defaults.c
    ${LIB_ROOT}/platform/wasm/stubs/crypto.c
    ${LIB_ROOT}/platform/wasm/stubs/backtrace.c
    ${LIB_ROOT}/platform/wasm/stubs/atomic.c
)

# Third-party dependencies (source-included)
set(TOML_SRC
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src/tomlc17.c
)

set(UTF8PROC_SRC
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc/utf8proc.c
)

# Library source files common to both mirror and client modes
set(LIB_SRC_COMMON
    # Core
    ${LIB_ROOT}/common.c
    ${LIB_ROOT}/asciichat_errno.c
    ${LIB_ROOT}/buffer_pool.c

    # Options system
    ${LIB_ROOT}/options/options.c
    ${LIB_ROOT}/options/common.c
    ${LIB_ROOT}/options/enums.c
    ${LIB_ROOT}/options/strings.c
    ${LIB_ROOT}/options/rcu.c
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

    # Discovery (for session string validation)
    ${LIB_ROOT}/discovery/strings.c
    ${LIB_ROOT}/discovery/adjectives.c
    ${LIB_ROOT}/discovery/nouns.c

    # Note: platform/system.c is NOT included here - it's included by the WASM-specific
    # wasm/system.c file, which follows the same pattern as posix/system.c and windows/system.c

    # Logging
    # ${LIB_ROOT}/log/log.c - EXCLUDED for WASM (depends on log_template_parse and other functions)
    # ${LIB_ROOT}/log/format.c - EXCLUDED for WASM (depends on lifecycle)
    # ${LIB_ROOT}/log/colorize.c - EXCLUDED for WASM
    # ${LIB_ROOT}/log/grep.c - EXCLUDED for WASM
    # ${LIB_ROOT}/log/interactive_grep.c - EXCLUDED: not needed for web client

    # Debug/memory tracking (sync/mutex/atomic debugging excluded for WASM)
    ${LIB_ROOT}/debug/memory.c
    ${LIB_ROOT}/atomic.c

    # Terminal abstraction (using WASM-specific version from PLATFORM_SRC)
    # ${LIB_ROOT}/platform/terminal.c - EXCLUDED: using wasm/terminal.c instead
)

# Mirror-specific sources (not needed by client mode)
set(LIB_SRC_MIRROR_ONLY
    ${LIB_ROOT}/video/rgba/image.c          # Image file I/O (JPEG/PNG) - mirror only
    ${LIB_ROOT}/video/ascii/rle.c            # RLE codec for server ASCII frames - mirror only
    ${LIB_ROOT}/video/anim/digital_rain.c   # Matrix rain effect - mirror only
    ${LIB_ROOT}/util/aspect_ratio.c    # Video scaling - mirror only
)

# Client-specific sources (crypto + network)
# Note: Only minimal crypto handshake for web client (no SSH keys, known_hosts, etc.)
set(LIB_SRC_CLIENT_CRYPTO
    ${LIB_ROOT}/crypto/handshake/common.c
    ${LIB_ROOT}/crypto/handshake/client.c
)

set(LIB_SRC_CLIENT_NETWORK
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
    ${TOML_SRC}
    ${UTF8PROC_SRC}
)

# Apply EMSCRIPTEN_BUILD only to WASM targets (not globally)
target_compile_definitions(wasm-common PRIVATE
    EMSCRIPTEN_BUILD=1
)

# Enable Emscripten's pthread support
target_compile_options(wasm-common PRIVATE -pthread)
target_link_options(wasm-common PRIVATE -pthread -Wno-error=pthreads-mem-growth)

# Link dependencies for shared library
target_link_libraries(wasm-common PRIVATE
    sodium
    pcre2-8
)

# Add include directories for wasm-common to find sodium and pcre2 headers
target_include_directories(wasm-common PRIVATE
    ${CMAKE_BINARY_DIR}/_deps/sodium-src/libsodium/src/libsodium/include
    ${CMAKE_BINARY_DIR}/_deps/pcre2-src
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

# Apply EMSCRIPTEN_BUILD only to WASM targets (not globally)
target_compile_definitions(mirror-web PRIVATE EMSCRIPTEN_BUILD=1)

# Enable Emscripten's pthread support and SIMD for video processing
target_compile_options(mirror-web PRIVATE -pthread -msimd128 -mavx2)
target_link_options(mirror-web PRIVATE -pthread -Wno-error=pthreads-mem-growth)

# Link dependencies (built for WASM via FetchContent)
target_link_libraries(mirror-web PRIVATE
    sodium
    pcre2-8
)

# Apply sanitizers
apply_sanitizers_to_target(mirror-web)

# Source files for WASM client entry point
set(WEB_SRC_CLIENT
    ${CMAKE_CURRENT_SOURCE_DIR}/client.c
)

# Create WASM client module
add_executable(client-web
    ${WEB_SRC_CLIENT}
    ${LIB_SRC_CLIENT_CRYPTO}
    ${LIB_SRC_CLIENT_NETWORK}
    $<TARGET_OBJECTS:wasm-common>
)

# Apply EMSCRIPTEN_BUILD only to WASM targets (not globally)
target_compile_definitions(client-web PRIVATE EMSCRIPTEN_BUILD=1)

# Enable Emscripten's pthread support
target_compile_options(client-web PRIVATE -pthread)
target_link_options(client-web PRIVATE -pthread -Wno-error=pthreads-mem-growth)

# Link dependencies for client mode
target_link_libraries(client-web PRIVATE
    sodium
    pcre2-8
    opus
    libzstd_static
)

# Add include directories for client mode dependencies
target_include_directories(client-web PRIVATE
    ${zstd_SOURCE_DIR}/lib
    ${opus_SOURCE_DIR}/include
)

# Apply sanitizers
apply_sanitizers_to_target(client-web)

# Exported functions for mirror module (all EMSCRIPTEN_KEEPALIVE functions from mirror.c and common/options.c)
set(EXPORTED_FUNCTIONS_MIRROR
    _mirror_init_with_args
    _mirror_cleanup
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
    _set_target_fps
    _get_target_fps
    _mirror_convert_frame
    _mirror_free_string
    _get_help_text
    _malloc
    _free
)

# Exported functions for client module (all EMSCRIPTEN_KEEPALIVE functions from client.c and common/options.c)
set(EXPORTED_FUNCTIONS_CLIENT
    _client_init_with_args
    _client_cleanup
    _client_generate_keypair
    _client_get_public_key_hex
    _client_handle_key_exchange_init
    _client_handle_auth_challenge
    _client_handle_handshake_complete
    _client_encrypt_packet
    _client_decrypt_packet
    _client_parse_packet
    _client_serialize_packet
    _client_send_video_frame
    _client_get_connection_state
    _client_opus_encoder_init
    _client_opus_decoder_init
    _client_opus_encode
    _client_opus_decode
    _client_opus_encoder_cleanup
    _client_opus_decoder_cleanup
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
    _set_target_fps
    _get_target_fps
    _client_free_string
    _get_help_text
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
    -sWASM_BIGINT=1
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
    -sMAXIMUM_MEMORY=128MB
    -sWASM_BIGINT=1
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
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../../../web/web/src/wasm/dist
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/mirror.js
            ${CMAKE_CURRENT_BINARY_DIR}/mirror.wasm
            ${CMAKE_SOURCE_DIR}/../../../web/web/src/wasm/dist/
        COMMENT "Copying mirror WASM files to web/src/wasm/dist/"
    )

    add_custom_command(TARGET client-web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../../../web/web/src/wasm/dist
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/client.wasm
            ${CMAKE_SOURCE_DIR}/../../../web/web/src/wasm/dist/
        COMMENT "Copying client WASM files to web/src/wasm/dist/"
    )

# Combined WASM target (builds both mirror and client)
    add_custom_target(web
        DEPENDS mirror-web client-web
        COMMENT "All WASM modules built"
    )
endif()

