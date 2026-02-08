# =============================================================================
# WebAssembly Build Targets (Wrappers)
# =============================================================================
# Provides WASM build targets that invoke the separate web/web.ascii-chat.com/wasm/
# build system optimized for Emscripten.
#
# Available targets:
#   mirror-wasm  - Build mirror mode WASM module only
#   client-wasm  - Build client mode WASM module only
#   wasm         - Build both mirror and client WASM modules
#
# Example:
#   cmake --build build --target wasm
#
# The actual WASM build is in web/web.ascii-chat.com/wasm/ with its own
# CMakeLists.txt optimized for Emscripten.
# =============================================================================

# Mirror mode WASM target
add_custom_target(mirror-wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building mirror WASM module..."
    COMMAND ${CMAKE_COMMAND} -E env
        PATH=/usr/lib/emscripten:$ENV{PATH}
        bash -c "cd ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm && emcmake cmake -B build && cmake --build build --target mirror-web"
    COMMAND ${CMAKE_COMMAND} -E echo "Merging compile_commands.json..."
    COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/utils/merge-compile-commands.sh
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm/build/compile_commands.json
        ${CMAKE_BINARY_DIR}/compile_commands.json
    COMMENT "Building WASM mirror module and merging compile_commands.json"
    VERBATIM
)

# Client mode WASM target
add_custom_target(client-wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building client WASM module..."
    COMMAND ${CMAKE_COMMAND} -E env
        PATH=/usr/lib/emscripten:$ENV{PATH}
        bash -c "cd ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm && emcmake cmake -B build && cmake --build build --target client-web"
    COMMAND ${CMAKE_COMMAND} -E echo "Merging compile_commands.json..."
    COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/utils/merge-compile-commands.sh
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm/build/compile_commands.json
        ${CMAKE_BINARY_DIR}/compile_commands.json
    COMMENT "Building WASM client module and merging compile_commands.json"
    VERBATIM
)

# Combined WASM target (builds both mirror and client)
add_custom_target(wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building all WASM modules..."
    COMMAND ${CMAKE_COMMAND} -E env
        PATH=/usr/lib/emscripten:$ENV{PATH}
        bash -c "cd ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm && emcmake cmake -B build && cmake --build build --target mirror-web && cmake --build build --target client-web"
    COMMAND ${CMAKE_COMMAND} -E echo "Merging compile_commands.json..."
    COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/utils/merge-compile-commands.sh
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm/build/compile_commands.json
        ${CMAKE_BINARY_DIR}/compile_commands.json
    COMMENT "Building all WASM modules and merging compile_commands.json"
    VERBATIM
)

message(STATUS "WASM targets configured:")
message(STATUS "  mirror-wasm - Build mirror mode WASM only")
message(STATUS "  client-wasm - Build client mode WASM only")
message(STATUS "  wasm        - Build both mirror and client WASM")
message(STATUS "  Requires: emscripten toolchain in PATH or /usr/lib/emscripten/")
