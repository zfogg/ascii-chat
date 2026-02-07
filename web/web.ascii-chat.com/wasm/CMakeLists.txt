# WebAssembly Mirror Mode Build
# Full library integration for ASCII rendering in browser

cmake_minimum_required(VERSION 3.18)
project(ascii-chat-mirror-wasm)

# Required for Emscripten
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define flags for WASM build
add_definitions(
    -DEMSCRIPTEN_BUILD=1
)

# Add compiler flags for pthreads support (atomics + bulk-memory)
add_compile_options(-pthread -matomics -mbulk-memory)

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

# TOML (using tomlc17 from deps)
# No FetchContent needed - using vendored tomlc17

# Path to main ascii-chat library
set(LIB_ROOT ${CMAKE_SOURCE_DIR}/../../../lib)
set(INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/../../../include)
set(DEPS_ROOT ${CMAKE_SOURCE_DIR}/../../../deps)

# Include directories for library headers
include_directories(
    ${INCLUDE_ROOT}
    ${LIB_ROOT}
    ${DEPS_ROOT}
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc
)

# Source files for WASM entry point
set(MIRROR_SOURCES
    ${CMAKE_SOURCE_DIR}/mirror_wasm.c
)

# Platform abstraction layer for WASM
set(PLATFORM_SOURCES
    ${LIB_ROOT}/platform/wasm/init.c
    ${LIB_ROOT}/platform/wasm/terminal.c
    ${LIB_ROOT}/platform/wasm/threading.c
    ${LIB_ROOT}/platform/wasm/environment.c
    ${LIB_ROOT}/platform/wasm/time.c
    ${LIB_ROOT}/platform/wasm/string.c
    ${LIB_ROOT}/platform/wasm/util.c
    ${LIB_ROOT}/platform/wasm/actions.c
    ${LIB_ROOT}/platform/wasm/manpage_stubs.c
    ${LIB_ROOT}/platform/wasm/file.c
    ${LIB_ROOT}/platform/wasm/video_stubs.c
    ${LIB_ROOT}/platform/wasm/log_stubs.c
)

# Third-party dependencies (source-included)
set(TOML_SOURCES
    ${DEPS_ROOT}/ascii-chat-deps/tomlc17/src/tomlc17.c
)

set(UTF8PROC_SOURCES
    ${DEPS_ROOT}/ascii-chat-deps/utf8proc/utf8proc.c
)

# Library source files (mirror mode + crypto for future client mode)
set(LIB_SOURCES
    # Core
    ${LIB_ROOT}/common.c
    ${LIB_ROOT}/asciichat_errno.c

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

    # Crypto (needed for logging headers + future client mode)
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
    ${LIB_ROOT}/video/simd/common.c
    ${LIB_ROOT}/video/simd/ascii_simd.c

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
    ${LIB_ROOT}/log/filter.c

    # Debug/memory tracking
    ${LIB_ROOT}/debug/memory.c
    ${LIB_ROOT}/debug/lock.c

    # Terminal abstraction
    ${LIB_ROOT}/platform/terminal.c
)

# Create WASM module
add_executable(mirror
    ${MIRROR_SOURCES}
    ${PLATFORM_SOURCES}
    ${LIB_SOURCES}
    ${TOML_SOURCES}
    ${UTF8PROC_SOURCES}
)

# Link dependencies (built for WASM via FetchContent)
target_link_libraries(mirror PRIVATE
    sodium
    pcre2-8
)

# Exported functions (all EMSCRIPTEN_KEEPALIVE functions from mirror_wasm.c)
set(EXPORTED_FUNCTIONS
    _mirror_init
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
    _mirror_convert_frame
    _mirror_free_string
    _malloc
    _free
)

# Convert list to JSON array format for Emscripten
string(REPLACE ";" "\",\"" EXPORTED_FUNCTIONS_JSON "${EXPORTED_FUNCTIONS}")
set(EXPORTED_FUNCTIONS_JSON "[\"${EXPORTED_FUNCTIONS_JSON}\"]")

set_target_properties(mirror PROPERTIES
    LINK_FLAGS "\
        -s WASM=1 \
        -s MODULARIZE=1 \
        -s EXPORT_ES6=1 \
        -s EXPORT_NAME='MirrorModule' \
        -s EXPORTED_FUNCTIONS='${EXPORTED_FUNCTIONS_JSON}' \
        -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"UTF8ToString\",\"getValue\",\"setValue\",\"HEAPU8\"]' \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s INITIAL_MEMORY=32MB \
        -s MAXIMUM_MEMORY=256MB \
        -s WASM_BIGINT=1 \
        -s USE_PTHREADS=1 \
        -pthread \
        -msimd128 \
        -O3 \
        --no-entry"
)

# Copy outputs to web/src/wasm/dist/ (so Vite can import them)
add_custom_command(TARGET mirror POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../src/wasm/dist
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/mirror.js
        ${CMAKE_BINARY_DIR}/mirror.wasm
        ${CMAKE_SOURCE_DIR}/../src/wasm/dist/
    COMMENT "Copying WASM files to web/src/wasm/dist/"
)
