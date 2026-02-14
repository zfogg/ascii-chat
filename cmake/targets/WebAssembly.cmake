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

set(WASM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/web/web.ascii-chat.com/wasm")
set(WASM_BUILD_DIR "${WASM_SOURCE_DIR}/build")

# Find emcmake - check common locations on Windows and Unix
if(WIN32)
    # Windows: Check EMSDK env var, scoop install location, or PATH
    if(DEFINED ENV{EMSDK})
        set(EMCMAKE_SEARCH_PATHS
            "$ENV{EMSDK}/upstream/emscripten"
            "$ENV{EMSDK}"
        )
    else()
        # Common scoop install location
        set(EMCMAKE_SEARCH_PATHS
            "$ENV{USERPROFILE}/scoop/apps/emscripten/current/upstream/emscripten"
            "$ENV{LOCALAPPDATA}/emsdk/upstream/emscripten"
            "C:/emsdk/upstream/emscripten"
        )
    endif()
    find_program(EMCMAKE_EXECUTABLE
        NAMES emcmake.bat emcmake
        HINTS ${EMCMAKE_SEARCH_PATHS}
        PATHS ${EMCMAKE_SEARCH_PATHS}
    )
else()
    # Unix: Check EMSDK env var or common paths
    if(DEFINED ENV{EMSDK})
        set(EMCMAKE_SEARCH_PATHS
            "$ENV{EMSDK}/upstream/emscripten"
        )
    else()
        set(EMCMAKE_SEARCH_PATHS
            "/usr/lib/emscripten"
            "/usr/local/lib/emscripten"
            "$ENV{HOME}/emsdk/upstream/emscripten"
        )
    endif()
    find_program(EMCMAKE_EXECUTABLE
        NAMES emcmake
        HINTS ${EMCMAKE_SEARCH_PATHS}
        PATHS ${EMCMAKE_SEARCH_PATHS}
    )
endif()

if(EMCMAKE_EXECUTABLE)
    message(STATUS "Found emcmake: ${EMCMAKE_EXECUTABLE}")
    get_filename_component(EMSCRIPTEN_DIR "${EMCMAKE_EXECUTABLE}" DIRECTORY)
else()
    message(STATUS "emcmake not found - WASM targets will search PATH at build time")
    if(WIN32)
        set(EMCMAKE_EXECUTABLE "emcmake.bat")
    else()
        set(EMCMAKE_EXECUTABLE "emcmake")
    endif()
endif()

# Create a CMake script to run the WASM build (cross-platform)
set(WASM_BUILD_SCRIPT "${CMAKE_BINARY_DIR}/wasm_build.cmake")
file(WRITE ${WASM_BUILD_SCRIPT} "
# Cross-platform WASM build script
# Arguments: TARGET (mirror-web, client-web, or all)

cmake_minimum_required(VERSION 3.18)

set(WASM_SOURCE_DIR \"${WASM_SOURCE_DIR}\")
set(WASM_BUILD_DIR \"${WASM_BUILD_DIR}\")
set(EMCMAKE \"${EMCMAKE_EXECUTABLE}\")

if(NOT TARGET_NAME)
    set(TARGET_NAME \"all\")
endif()

message(STATUS \"Building WASM target: \${TARGET_NAME}\")

# Configure with emcmake if not already configured
if(NOT EXISTS \"\${WASM_BUILD_DIR}/CMakeCache.txt\")
    message(STATUS \"Configuring WASM build with emcmake...\")
    execute_process(
        COMMAND \${EMCMAKE} \${CMAKE_COMMAND} -B \"\${WASM_BUILD_DIR}\"
        WORKING_DIRECTORY \"\${WASM_SOURCE_DIR}\"
        RESULT_VARIABLE CONFIG_RESULT
    )
    if(NOT CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR \"WASM configure failed with code \${CONFIG_RESULT}\")
    endif()
endif()

# Build the target(s)
if(TARGET_NAME STREQUAL \"all\")
    message(STATUS \"Building mirror-web...\")
    execute_process(
        COMMAND \${CMAKE_COMMAND} --build \"\${WASM_BUILD_DIR}\" --target mirror-web
        RESULT_VARIABLE BUILD_RESULT
    )
    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR \"WASM mirror-web build failed\")
    endif()

    message(STATUS \"Building client-web...\")
    execute_process(
        COMMAND \${CMAKE_COMMAND} --build \"\${WASM_BUILD_DIR}\" --target client-web
        RESULT_VARIABLE BUILD_RESULT
    )
    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR \"WASM client-web build failed\")
    endif()
else()
    message(STATUS \"Building \${TARGET_NAME}...\")
    execute_process(
        COMMAND \${CMAKE_COMMAND} --build \"\${WASM_BUILD_DIR}\" --target \${TARGET_NAME}
        RESULT_VARIABLE BUILD_RESULT
    )
    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR \"WASM \${TARGET_NAME} build failed\")
    endif()
endif()

message(STATUS \"WASM build complete\")
")

# Mirror mode WASM target
add_custom_target(mirror-wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building mirror WASM module..."
    COMMAND ${CMAKE_COMMAND} -DTARGET_NAME=mirror-web -P "${WASM_BUILD_SCRIPT}"
    COMMENT "Building WASM mirror module"
    VERBATIM
)

# Client mode WASM target
add_custom_target(client-wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building client WASM module..."
    COMMAND ${CMAKE_COMMAND} -DTARGET_NAME=client-web -P "${WASM_BUILD_SCRIPT}"
    COMMENT "Building WASM client module"
    VERBATIM
)

# Combined WASM target (builds both mirror and client)
add_custom_target(wasm
    COMMAND ${CMAKE_COMMAND} -E echo "Building all WASM modules..."
    COMMAND ${CMAKE_COMMAND} -DTARGET_NAME=all -P "${WASM_BUILD_SCRIPT}"
    COMMENT "Building all WASM modules"
    VERBATIM
)

message(STATUS "WASM targets configured:")
message(STATUS "  mirror-wasm - Build mirror mode WASM only")
message(STATUS "  client-wasm - Build client mode WASM only")
message(STATUS "  wasm        - Build both mirror and client WASM")
if(EMCMAKE_EXECUTABLE)
    message(STATUS "  Using emcmake: ${EMCMAKE_EXECUTABLE}")
else()
    message(STATUS "  Requires: emscripten toolchain in PATH")
endif()
