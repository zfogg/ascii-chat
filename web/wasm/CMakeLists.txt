# WebAssembly Mirror Mode Build
# Minimal build for ASCII rendering in browser

cmake_minimum_required(VERSION 3.18)
project(ascii-chat-mirror-wasm)

# Required for Emscripten
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define flags for WASM build
add_definitions(
    -DEMSCRIPTEN_BUILD=1
)

# Source files for ASCII rendering (standalone, no dependencies)
set(MIRROR_SOURCES
    ${CMAKE_SOURCE_DIR}/mirror_standalone.c
)

# Create WASM module
add_executable(mirror ${MIRROR_SOURCES})

set_target_properties(mirror PROPERTIES
    LINK_FLAGS "\
        -s WASM=1 \
        -s MODULARIZE=1 \
        -s EXPORT_ES6=1 \
        -s EXPORT_NAME='MirrorModule' \
        -s EXPORTED_FUNCTIONS='[\"_convert_frame_to_ascii\",\"_free_ascii_buffer\",\"_malloc\",\"_free\"]' \
        -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"UTF8ToString\",\"getValue\",\"setValue\"]' \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s INITIAL_MEMORY=32MB \
        -s MAXIMUM_MEMORY=256MB \
        -s WASM_BIGINT=1 \
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
