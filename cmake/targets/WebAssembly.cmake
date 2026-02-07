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

# Create custom target that invokes the WASM build
add_custom_target(mirror-wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building WASM module..."
    COMMAND ${CMAKE_COMMAND} -E env
        PATH=/usr/lib/emscripten:$ENV{PATH}
        bash -c "cd ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm && emcmake cmake -B build && cmake --build build"
    COMMENT "Building WASM mirror module (emscripten required)"
    VERBATIM
)

message(STATUS "WASM target configured: mirror-wasm")
message(STATUS "  Build with: cmake --build build --target mirror-wasm")
message(STATUS "  Requires: emscripten toolchain in PATH or /usr/lib/emscripten/")
