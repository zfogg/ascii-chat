# =============================================================================
# WebAssembly Build Target (Wrapper)
# =============================================================================
# Provides 'mirror-wasm' target that builds the WASM module via the separate
# web/web.ascii-chat.com/wasm/ build system.
#
# This allows building WASM from the main build:
#   cmake --build build --target mirror-wasm
#
# The actual WASM build is in web/web.ascii-chat.com/wasm/ with its own
# CMakeLists.txt optimized for Emscripten.
# =============================================================================

# Create custom target that invokes the WASM build and merges compile_commands.json
add_custom_target(mirror-wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building WASM module..."
    COMMAND ${CMAKE_COMMAND} -E env
        PATH=/usr/lib/emscripten:$ENV{PATH}
        bash -c "cd ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm && emcmake cmake -B build && cmake --build build"
    COMMAND ${CMAKE_COMMAND} -E echo "Merging compile_commands.json..."
    COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/utils/merge-compile-commands.sh
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm/build/compile_commands.json
        ${CMAKE_BINARY_DIR}/compile_commands.json
    COMMENT "Building WASM mirror module and merging compile_commands.json"
    VERBATIM
)

message(STATUS "WASM target configured: mirror-wasm")
message(STATUS "  Build with: cmake --build build --target mirror-wasm")
message(STATUS "  Requires: emscripten toolchain in PATH or /usr/lib/emscripten/")
