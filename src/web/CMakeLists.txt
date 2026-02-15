# WebAssembly Mirror Mode Build
# Full library integration for ASCII rendering in browser

cmake_minimum_required(VERSION 3.18)
project(ascii-chat-mirror-wasm)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Required for Emscripten
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define flags for WASM build
add_definitions(
    -DEMSCRIPTEN_BUILD=1
)

# Add compiler flags for WASM SIMD
# -mavx2 enables AVX2 intrinsics emulation on top of WASM SIMD128
# Note: pthreads disabled for mirror mode (no threading needed, causes malloc hangs)
add_compile_options(-msimd128 -mavx2)

# Fetch and build dependencies for WASM
include(FetchContent)

# libsodium
FetchContent_Declare(Sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG e5b985ad0dd235d8c4307ea3a385b45e76c74c6a
)
set(SODIUM_DISABLE_TESTS ON)
FetchContent_MakeAvailable(Sodium)

# PCRE2
FetchContent_Declare(pcre2
    GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
    GIT_TAG pcre2-10.44
)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "Build PCRE2 tests")
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "Build pcre2grep")
FetchContent_MakeAvailable(pcre2)

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
FetchContent_MakeAvailable(Opus)

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
FetchContent_MakeAvailable(zstd)

# TOML (using tomlc17 from deps)
# No FetchContent needed - using vendored tomlc17

# Path to main ascii-chat library
set(LIB_ROOT ${CMAKE_SOURCE_DIR}/../../lib)
set(INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/../../include)
set(DEPS_ROOT ${CMAKE_SOURCE_DIR}/../../deps)

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

# Source files for WASM entry point
set(MIRROR_SOURCES
    ${CMAKE_SOURCE_DIR}/mirror.c
)

# Platform abstraction layer for WASM
set(PLATFORM_SOURCES
    ${LIB_ROOT}/platform/wasm/init.c
    ${LIB_ROOT}/platform/wasm/console.c
    ${LIB_ROOT}/platform/wasm/terminal.c
    ${LIB_ROOT}/platform/wasm/threading.c
    ${LIB_ROOT}/platform/wasm/system.c
    ${LIB_ROOT}/platform/wasm/time.c
    ${LIB_ROOT}/platform/wasm/string.c
    ${LIB_ROOT}/platform/wasm/stubs/util.c
    ${LIB_ROOT}/platform/wasm/stubs/actions.c
    ${LIB_ROOT}/platform/wasm/stubs/terminal.c
    ${LIB_ROOT}/platform/wasm/stubs/manpage.c
    ${LIB_ROOT}/platform/wasm/stubs/filesystem.c
    ${LIB_ROOT}/platform/wasm/stubs/video.c
    ${LIB_ROOT}/platform/wasm/stubs/log.c
    ${LIB_ROOT}/platform/wasm/stubs/mode_defaults.c
    ${LIB_ROOT}/platform/wasm/stubs/crypto.c
)

# Third-party dependencies (source-included)
set(TOML_SOURCES
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src/tomlc17.c
)

set(UTF8PROC_SOURCES
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc/utf8proc.c
)

# Library source files (mirror mode)
set(LIB_SOURCES_MIRROR
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

    # Video processing
    ${LIB_ROOT}/video/ascii.c
    ${LIB_ROOT}/video/ansi.c
    ${LIB_ROOT}/video/ansi_fast.c
    ${LIB_ROOT}/video/color_filter.c
    ${LIB_ROOT}/video/video_frame.c
    ${LIB_ROOT}/video/palette.c
    ${LIB_ROOT}/video/image.c
    ${LIB_ROOT}/video/output_buffer.c
    ${LIB_ROOT}/video/rle.c
    ${LIB_ROOT}/video/digital_rain.c
    ${LIB_ROOT}/video/simd/common.c
    ${LIB_ROOT}/video/simd/ascii_simd.c
    ${LIB_ROOT}/video/simd/ascii_simd_color.c
    ${LIB_ROOT}/video/simd/avx2.c

    # Utilities (minimal set needed by video/options)
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
    ${LIB_ROOT}/util/aspect_ratio.c

    # Discovery (for session string validation)
    ${LIB_ROOT}/discovery/strings.c
    ${LIB_ROOT}/discovery/adjectives.c
    ${LIB_ROOT}/discovery/nouns.c

    # Platform system functions
    ${LIB_ROOT}/platform/system.c

    # Logging
    ${LIB_ROOT}/log/logging.c
    ${LIB_ROOT}/log/colorize.c
    ${LIB_ROOT}/log/grep.c
    ${LIB_ROOT}/log/interactive_grep.c

    # Debug/memory tracking
    ${LIB_ROOT}/debug/memory.c
    ${LIB_ROOT}/debug/lock.c

    # Terminal abstraction (using WASM-specific version from PLATFORM_SOURCES)
    # ${LIB_ROOT}/platform/terminal.c - EXCLUDED: using wasm/terminal.c instead
)

# Additional sources for client mode (crypto + network)
# Note: Only minimal crypto handshake for web client (no SSH keys, known_hosts, etc.)
set(CRYPTO_SOURCES
    ${LIB_ROOT}/crypto/handshake/common.c
    ${LIB_ROOT}/crypto/handshake/client.c
)

set(NETWORK_SOURCES
    ${LIB_ROOT}/network/packet_parsing.c
    ${LIB_ROOT}/network/crc32.c
    ${LIB_ROOT}/network/compression.c
    ${LIB_ROOT}/network/errors.c
    ${LIB_ROOT}/network/frame_validator.c
    ${LIB_ROOT}/network/acip/send.c
)

# Create WASM mirror module
add_executable(mirror-web
    ${MIRROR_SOURCES}
    ${PLATFORM_SOURCES}
    ${LIB_SOURCES_MIRROR}
    ${TOML_SOURCES}
    ${UTF8PROC_SOURCES}
)

# Link dependencies (built for WASM via FetchContent)
target_link_libraries(mirror-web PRIVATE
    sodium
    pcre2-8
)

# Source files for client mode WASM entry point
set(CLIENT_SOURCES
    ${CMAKE_SOURCE_DIR}/client.c
)

# Create WASM client module
add_executable(client-web
    ${CLIENT_SOURCES}
    ${PLATFORM_SOURCES}
    ${LIB_SOURCES_MIRROR}
    ${CRYPTO_SOURCES}
    ${NETWORK_SOURCES}
    ${TOML_SOURCES}
    ${UTF8PROC_SOURCES}
)

# Link dependencies for client mode
target_link_libraries(client-web PRIVATE
    sodium
    pcre2-8
    opus
    libzstd_static
)

# Add include directories for client mode dependencies
target_include_directories(client-web PRIVATE
    ${CMAKE_BINARY_DIR}/_deps/zstd-src/lib
    ${CMAKE_BINARY_DIR}/_deps/opus-src/include
)

# Exported functions for mirror module (all EMSCRIPTEN_KEEPALIVE functions from mirror.c)
set(EXPORTED_FUNCTIONS_MIRROR
    _mirror_init_with_args
    _mirror_cleanup
    _mirror_set_width
    _mirror_set_height
    _mirror_get_width
    _mirror_get_height
    _mirror_set_render_mode
    _mirror_get_render_mode
    _mirror_set_color_mode
    _mirror_get_color_mode
    _mirror_set_color_filter
    _mirror_get_color_filter
    _mirror_set_palette
    _mirror_get_palette
    _mirror_set_palette_chars
    _mirror_get_palette_chars
    _mirror_set_matrix_rain
    _mirror_get_matrix_rain
    _mirror_set_webcam_flip
    _mirror_get_webcam_flip
    _mirror_convert_frame
    _mirror_free_string
    _malloc
    _free
)

# Exported functions for client module (all EMSCRIPTEN_KEEPALIVE functions from client.c)
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
    _client_free_string
    _malloc
    _free
)

# Convert lists to JSON array format for Emscripten
# Use comma-separated format without outer brackets (emcc will add them)
string(REPLACE ";" "," EXPORTED_FUNCTIONS_MIRROR_CSV "${EXPORTED_FUNCTIONS_MIRROR}")
string(REPLACE ";" "," EXPORTED_FUNCTIONS_CLIENT_CSV "${EXPORTED_FUNCTIONS_CLIENT}")

set(EXPORTED_RUNTIME_METHODS_CSV "ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,getValue,setValue,HEAPU8,print,printErr")

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
    -sMAXIMUM_MEMORY=256MB
    -sWASM_BIGINT=1
    -msimd128
    -O3
    --no-entry
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
    -sINITIAL_MEMORY=64MB
    -sMAXIMUM_MEMORY=512MB
    -sWASM_BIGINT=1
    -msimd128
    -O3
    --no-entry
)

# Set output names to match target names
set_target_properties(mirror-web PROPERTIES OUTPUT_NAME "mirror")
set_target_properties(client-web PROPERTIES OUTPUT_NAME "client")

# Copy outputs to web/src/wasm/dist/ (so Vite can import them)
add_custom_command(TARGET mirror-web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../../../web/web.ascii-chat.com/src/wasm/dist
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/mirror.js
        ${CMAKE_BINARY_DIR}/mirror.wasm
        ${CMAKE_SOURCE_DIR}/../../../web/web.ascii-chat.com/src/wasm/dist/
    COMMENT "Copying mirror WASM files to web/src/wasm/dist/"
)

add_custom_command(TARGET client-web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../../../web/web.ascii-chat.com/src/wasm/dist
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/client.js
        ${CMAKE_BINARY_DIR}/client.wasm
        ${CMAKE_SOURCE_DIR}/../../../web/web.ascii-chat.com/src/wasm/dist/
    COMMENT "Copying client WASM files to web/src/wasm/dist/"
)
