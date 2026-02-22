# src/ - Application mode entry points

set(APP_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    # Server mode
    ${CMAKE_CURRENT_SOURCE_DIR}/server/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/stream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/render.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/stats.c
    # Client mode
    ${CMAKE_CURRENT_SOURCE_DIR}/client/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/capture.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/keepalive.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/webrtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/connection_attempt.c
    # Mirror mode
    ${CMAKE_CURRENT_SOURCE_DIR}/mirror/main.c
    # Discovery-service mode
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery-service/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery-service/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery-service/signaling.c
    # Discovery mode
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/main.c
)

set(APP_SRCS ${APP_SRCS} PARENT_SCOPE)
# =============================================================================
# Executables Module
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/utils/CoreDependencies.cmake)
# This module creates the main executable target
#
# Prerequisites:
#   - All libraries must be created (via Libraries.cmake)
#   - Platform detection complete
#   - USE_MIMALLOC, USE_MUSL known
#
# Outputs:
#   - ascii-chat executable target
# =============================================================================

# Unified binary with both server and client modes
add_executable(ascii-chat ${APP_SRCS})

if(ASCIICHAT_ENABLE_UNITY_BUILDS)
    set_target_properties(ascii-chat PROPERTIES UNITY_BUILD ON)
endif()

if(ASCIICHAT_ENABLE_IPO)
    set_property(TARGET ascii-chat PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Link against the combined library instead of individual libraries
# Ensure the combined library is built before linking
# For Debug/Dev: shared library (DLL on Windows) - except musl which needs static
# For Release: static library (unless ASCIICHAT_SHARED_DEPS is ON)
# For USE_MUSL: always static (musl requires static linking)
# Determine if we should use shared library linking
set(_use_shared_lib FALSE)
if(NOT USE_MUSL)
    # Always use shared library (built from OBJECT libraries for all build types)
    set(_use_shared_lib TRUE)
endif()

if(_use_shared_lib)
    add_dependencies(ascii-chat ascii-chat-shared generate_version)
    target_link_libraries(ascii-chat ascii-chat-shared)

    # Explicitly add system libraries when using shared library
    # The executable needs to link against the same dependencies as the shared library
    if(WIN32)
        # Import library is in bin directory with the DLL
        target_link_directories(ascii-chat PRIVATE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
        get_core_deps_libraries(CORE_LIBS)
        target_link_libraries(ascii-chat
            ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
            ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
            crypt32
            Winmm  # For timeBeginPeriod/timeEndPeriod
            ${CORE_LIBS}
        )
        # Link BearSSL (required)
        target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        # Note: On Windows, we do NOT link webrtc_audio_processing directly to the executable.
        # The shared library (asciichat.dll) already links it, and direct C++ library linkage
        # would pull in MSVCP140D.dll as a direct dependency of the exe, causing ASan bad-free
        # errors during process exit (iostream objects allocated before ASan hooks are active).

        # Link mimalloc explicitly - the shared library links it PRIVATE so symbols don't propagate
        if(USE_MIMALLOC AND MIMALLOC_LIBRARIES)
            target_link_libraries(ascii-chat ${MIMALLOC_LIBRARIES})
        endif()

        # Delay-load datachannel.dll to defer loading when it's not needed.
        # Note: We cannot delay-load asciichat.dll because the executable accesses
        # global variables (g_argc, g_argv) defined in the DLL, and delay-loading
        # only works for function calls, not variable access.
        target_link_options(ascii-chat PRIVATE
            "LINKER:/DELAYLOAD:datachannel.dll"
        )
        target_link_libraries(ascii-chat delayimp)
    else()
        # Unix: Also need explicit dependencies when linking against shared library
        get_core_deps_libraries(CORE_LIBS)
        target_link_libraries(ascii-chat
            ${CORE_LIBS}
        )
        # Link BearSSL (required)
        target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        # Link WebRTC audio processing for echo cancellation
        if(TARGET webrtc_audio_processing)
            target_link_libraries(ascii-chat webrtc_audio_processing)
        endif()
        # Link mimalloc explicitly - the shared library links it PRIVATE so symbols don't propagate
        if(USE_MIMALLOC AND MIMALLOC_LIBRARIES)
            target_link_libraries(ascii-chat ${MIMALLOC_LIBRARIES})
        endif()

        # Suppress linker warnings about duplicate debug symbols in libdatachannel
        # (libdatachannel embeds debug symbols with invalid timestamps - linker warning only, not an error)
        if(APPLE)
            target_link_options(ascii-chat PRIVATE "-Wl,-w")
        endif()

        # Ensure C++ standard library is linked
        # Use CXX language for linking to automatically link C++ runtime
        set_property(TARGET ascii-chat PROPERTY LINKER_LANGUAGE CXX)
    endif()
endif()
# Ensure build directory takes precedence in rpath for Debug/Dev builds
# This prevents conflicts with installed libraries in system directories
# Skip for Release builds - they use static linking and shouldn't embed developer paths
# EXCEPTION: ASCIICHAT_SHARED_DEPS builds need rpath to find Homebrew LLVM's libc++ and libunwind
if((APPLE OR UNIX) AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    # Put our build/lib directory FIRST in the RPATH so it's searched before system paths
    # This ensures ghostty library is found before any system conflicts
    # Use CMAKE_BUILD_RPATH but prepend our path to it
    set(_rpath_list "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
    if(CMAKE_BUILD_RPATH)
        list(APPEND _rpath_list ${CMAKE_BUILD_RPATH})
    endif()
    if(APPLE)
        list(APPEND _rpath_list "${HOMEBREW_PREFIX}/lib")
    endif()

    set_target_properties(ascii-chat PROPERTIES
        BUILD_RPATH "${_rpath_list}"
        SKIP_BUILD_RPATH FALSE
    )
elseif(CMAKE_BUILD_TYPE STREQUAL "Release" AND ASCIICHAT_SHARED_DEPS)
    # Release builds with shared deps (Homebrew) need rpath for dynamic linking
    # Include:
    #   - @loader_path/../lib for libasciichat.dylib (relative to binary)
    #   - CMAKE_BUILD_RPATH for LLVM library paths (libc++, libunwind)
    #   - ${HOMEBREW_PREFIX}/lib for Homebrew-installed libraries on macOS
    if(APPLE)
        set(_install_rpath "@loader_path/../lib;${CMAKE_BUILD_RPATH};${HOMEBREW_PREFIX}/lib")
    else()
        set(_install_rpath "$ORIGIN/../lib;${CMAKE_BUILD_RPATH}")
    endif()
    set_target_properties(ascii-chat PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY};${CMAKE_BUILD_RPATH}"
        INSTALL_RPATH "${_install_rpath}"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
    message(STATUS "ascii-chat using rpath for SHARED_DEPS build: ${_install_rpath}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Release builds now use shared library (after OBJECT library simplification)
    # Use relative rpath to avoid embedding developer paths in the binary
    # This satisfies the ValidatePaths.cmake security check
    # Add ghostty library directory for releases (ghostty is static in release, no rpath needed)
    if(APPLE)
        set(_release_rpath "@loader_path/../lib;/usr/local/lib;${HOMEBREW_PREFIX}/lib")
    else()
        set(_release_rpath "$ORIGIN/../lib:/usr/local/lib")
    endif()
    set_target_properties(ascii-chat PROPERTIES
        # Skip automatic rpath during build - we set it explicitly
        SKIP_BUILD_RPATH FALSE
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${_release_rpath}"
        # Don't add link directories to rpath - use only what we specify
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# Include directories for executable (needed for version.h and other generated headers)
target_include_directories(ascii-chat PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)
# Add src/common for session headers (allows #include "session/display.h" style)
target_include_directories(ascii-chat PRIVATE ${CMAKE_SOURCE_DIR}/src/common)

# Add mimalloc include directory for executable (needed for SAFE_MALLOC macros in src/)
if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
    target_include_directories(ascii-chat PRIVATE ${MIMALLOC_INCLUDE_DIRS})
endif()

# Add build timing for ascii-chat executable
add_custom_command(TARGET ascii-chat PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Starting ascii-chat timer"
    VERBATIM
)
add_custom_command(TARGET ascii-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Finishing ascii-chat timer"
    VERBATIM
)

# Custom target wrapper for ALL builds (when no explicit --target is used)
# This shows "up to date" message when ascii-chat doesn't need rebuilding
add_custom_target(show-ascii-chat-success ALL
    COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMAND_ECHO NONE
    DEPENDS ascii-chat
    VERBATIM
)

# macOS Info.plist embedding (for client mode webcam access)
if(PLATFORM_DARWIN AND EXISTS "${CMAKE_SOURCE_DIR}/Info.plist")
    set_target_properties(ascii-chat PROPERTIES
        LINK_FLAGS "-sectcreate __TEXT __info_plist ${CMAKE_SOURCE_DIR}/Info.plist"
    )
    # Note: WebRTC library is cleaned by CMake to remove build tool objects with duplicate
    # main() symbols. Any remaining undefined symbols (like CACurrentMediaTime from video
    # capture code) are acceptable since that code won't be called in audio-only builds.
endif()

# Add musl dependency if building with musl
if(USE_MUSL)
    # Add dependencies on all musl libraries (they'll build automatically)
    # Note: Do NOT add ascii-chat-shared as a dependency - we'll link OBJECT libraries directly
    add_dependencies(ascii-chat portaudio-musl alsa-lib-musl libsodium-musl zstd-musl libexecinfo-musl opus-musl pcre2-musl ffmpeg-musl libwebsockets-musl)

    # Link against musl-built static libraries from individual dependency directories
    target_link_directories(ascii-chat PRIVATE
        ${PORTAUDIO_PREFIX}/lib
        ${ALSA_PREFIX}/lib
        ${LIBSODIUM_PREFIX}/lib
        ${LIBEXECINFO_PREFIX}/lib
        ${PCRE2_PREFIX}/lib
    )
    # Note: -rdynamic is incompatible with -static and not needed for musl + libexecinfo
    # Use lld linker for musl+LTO builds (handles LTO without gold plugin)
    target_link_options(ascii-chat PRIVATE -static -fuse-ld=lld)

    # Link internal modules directly (OBJECT libraries avoid shared library dependency)
    # These are the same modules that make up ascii-chat-shared
    target_link_libraries(ascii-chat PRIVATE
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-platform
        ascii-chat-crypto
        ascii-chat-simd
        ascii-chat-video
        ascii-chat-audio
        ascii-chat-network
        ascii-chat-core
        ascii-chat-session
        ascii-chat-panic
    )

    # Link external dependencies (LTO + dead code elimination removes unused code)
    target_link_libraries(ascii-chat PRIVATE
        ${PORTAUDIO_PREFIX}/lib/libportaudio.a
        ${ALSA_PREFIX}/lib/libasound.a
        ${LIBSODIUM_PREFIX}/lib/libsodium.a
        ${LIBEXECINFO_PREFIX}/lib/libexecinfo.a
        ${PCRE2_PREFIX}/lib/libpcre2-8.a
        ${ZLIB_LIBRARY}
        -lm -lpthread
    )

    # Link FFmpeg for media file streaming (musl-built static libraries - required)
    target_link_libraries(ascii-chat PRIVATE ${FFMPEG_LINK_LIBRARIES})

    # Link libwebsockets for WebSocket transport (musl-built static library - required)
    target_link_libraries(ascii-chat PRIVATE ${LIBWEBSOCKETS_LIBRARIES})

    # libdatachannel and its dependencies (must be linked explicitly for static builds)
    # The libdatachannel INTERFACE target's dependencies don't propagate through
    # the combined static library, so we link them directly here
    if(TARGET libdatachannel)
        set(LIBDATACHANNEL_MUSL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libdatachannel-build")
        target_link_libraries(ascii-chat PRIVATE
            "${LIBDATACHANNEL_MUSL_BUILD_DIR}/lib/libdatachannel.a"
            "${LIBDATACHANNEL_MUSL_BUILD_DIR}/lib/libjuice.a"
            "${LIBDATACHANNEL_MUSL_BUILD_DIR}/lib/libusrsctp.a"
        )
        # OpenSSL for libdatachannel TURN credentials (musl-built)
        if(OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
            target_link_libraries(ascii-chat PRIVATE
                ${OPENSSL_SSL_LIBRARY}
                ${OPENSSL_CRYPTO_LIBRARY}
            )
        endif()
    endif()

    # Link Alpine libc++ for static executable (no -fPIC needed for static)
    # Function defined in Musl.cmake, contains musl-compatible libc++
    link_alpine_libcxx(ascii-chat)

    # Disable RPATH changes for static musl binaries
    # CPack fails when trying to modify RPATH on static-PIE binaries
    set_target_properties(ascii-chat PROPERTIES
        SKIP_BUILD_RPATH TRUE
        INSTALL_RPATH ""
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# Preserve custom .ascii_chat_version section during linking
# NOTE: --keep-section is not supported by GNU ld (only gold/lld linkers)
# Commenting out for compatibility - version info may be stripped by --gc-sections
#if(NOT WIN32)
#    # Unix/macOS: Use linker flags to keep custom section
#    target_link_options(ascii-chat PRIVATE -Wl,--keep-section=.ascii_chat_version)
#endif()

# Check if PIE is supported before using PIE-related flags
include(CheckPIESupported)
check_pie_supported(OUTPUT_VARIABLE PIE_OUTPUT LANGUAGES C)

# PIE (Position Independent Executable) for security hardening
# Only enable for Release/RelWithDebInfo - Debug builds disable PIE for addr2line compatibility
# Note: -pie is added here per-target, not globally, to avoid affecting shared libraries
if(NOT WIN32 AND CMAKE_C_LINK_PIE_SUPPORTED)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        if(APPLE)
            target_link_options(ascii-chat PRIVATE -Wl,-pie)
        elseif(PLATFORM_LINUX)
            target_link_options(ascii-chat PRIVATE "LINKER:-pie")
        endif()
    endif()
endif()

# Enable dead code elimination for optimal binary size
target_compile_options(ascii-chat PRIVATE -ffunction-sections -fdata-sections)

# Release-specific optimizations for executable
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Hide symbols for smaller binary and better performance
    # Shared libraries use -fvisibility=default to export symbols
    if(NOT WIN32)
        target_compile_options(ascii-chat PRIVATE -fvisibility=hidden)
    endif()
endif()

if(USE_MIMALLOC)
    message(STATUS "ascii-chat linking mimalloc: ${ASCIICHAT_MIMALLOC_LINK_LIB}")
    message(STATUS "ascii-chat MIMALLOC_LIBRARIES raw: ${MIMALLOC_LIBRARIES}")
    if(TARGET mimalloc-static)
        add_dependencies(ascii-chat mimalloc-static)
    endif()
    if(TARGET mimalloc-shared)
        add_dependencies(ascii-chat mimalloc-shared)
    endif()
endif()

if(ASCIICHAT_LLVM_STATIC_LIBUNWIND)
    target_link_libraries(ascii-chat ${ASCIICHAT_LLVM_STATIC_LIBUNWIND})
endif()

if(NOT WIN32)
    if(APPLE)
        target_link_options(ascii-chat PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(ascii-chat PRIVATE -Wl,--gc-sections)
    endif()
endif()

# =============================================================================
# macOS dSYM Generation (Debug/Dev builds only)
# =============================================================================
# Generate dSYM bundle for debug symbol resolution with atos
# This enables backtraces with function names and line numbers
if(APPLE AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
    find_program(DSYMUTIL_EXECUTABLE dsymutil)
    if(DSYMUTIL_EXECUTABLE)
        add_custom_command(TARGET ascii-chat POST_BUILD
            COMMAND timeout 3 ${DSYMUTIL_EXECUTABLE} $<TARGET_FILE:ascii-chat> -o $<TARGET_FILE:ascii-chat>.dSYM
            COMMENT "Generating dSYM for ascii-chat (enables atos backtraces with line numbers)"
            VERBATIM
        )
        message(STATUS "dSYM generation: ${BoldGreen}enabled${ColorReset} (Debug/Dev builds)")
    else()
        message(STATUS "dSYM generation: ${BoldYellow}disabled${ColorReset} (dsymutil not found)")
    endif()
endif()

# =============================================================================
# macOS Code Signing (function definition only - actual signing happens in PostBuild.cmake after stripping)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/platform/CodeSigning.cmake)

# =============================================================================
# Global Build Timer - End Marker
# =============================================================================
# Show total build time after the final target is built
# ascii-chat is always built last, so attach the timer there
add_custom_command(TARGET ascii-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=build-total -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Finishing total build timer"
    VERBATIM
)

# Custom target wrapper for ALL builds (when no explicit --target)
# This only runs for default builds and prevents duplicate messages
add_custom_target(build-timer-end ALL
    DEPENDS show-ascii-chat-success
    VERBATIM
)

# =============================================================================
# Build-All Target - Shows Total Time After All Common Targets Built
# =============================================================================
# Usage: cmake --build build --target build-all
# This builds common targets (executable and libraries),
# then shows the total build time including all .o compilation

# Determine dependencies based on build configuration
set(BUILD_ALL_DEPS ascii-chat ascii-chat-shared)
if(NOT BUILDING_OBJECT_LIBS)
    list(APPEND BUILD_ALL_DEPS static-lib)
endif()

add_custom_target(build-all
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=build-total -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMAND_ECHO NONE
    DEPENDS ${BUILD_ALL_DEPS}
    COMMENT "Finishing build-all timer"
    VERBATIM
)

# =============================================================================
# Release Binary Validation (Linux ELF only)
# =============================================================================
# Validates Release binaries for security hardening, debug info removal,
# static linking, and correct architecture using llvm-readelf/objdump
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND UNIX AND NOT APPLE)
    if(ASCIICHAT_LLVM_READELF_EXECUTABLE)
        # 1. Security hardening check (RELRO, PIE)
        if(NOT ASCIICHAT_SKIP_HARDENING_VALIDATION)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DMODE=hardening
                    -DBINARY=$<TARGET_FILE:ascii-chat>
                    -DLLVM_READELF=${ASCIICHAT_LLVM_READELF_EXECUTABLE}
                    -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateBinary.cmake
                COMMENT "Validating security hardening"
                VERBATIM
            )
        endif()

        # 2. No debug info check
        # Note: Moved to PostBuild.cmake to run AFTER stripping

        # 3. Static linking check (USE_MUSL builds should be fully static)
        if(USE_MUSL)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DMODE=static
                    -DBINARY=$<TARGET_FILE:ascii-chat>
                    -DLLVM_READELF=${ASCIICHAT_LLVM_READELF_EXECUTABLE}
                    -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateBinary.cmake
                COMMENT "Validating static linking"
                VERBATIM
            )
        endif()

        # 4. Architecture check
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            set(EXPECTED_ARCH "x86_64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(EXPECTED_ARCH "aarch64")
        else()
            set(EXPECTED_ARCH "")
        endif()

        if(EXPECTED_ARCH)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DMODE=architecture
                    -DBINARY=$<TARGET_FILE:ascii-chat>
                    -DLLVM_READELF=${ASCIICHAT_LLVM_READELF_EXECUTABLE}
                    -DEXPECTED_ARCH=${EXPECTED_ARCH}
                    -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateBinary.cmake
                COMMENT "Validating architecture (${EXPECTED_ARCH})"
                VERBATIM
            )
        endif()

        message(STATUS "Release binary validation: ${BoldGreen}enabled${ColorReset} (hardening, no_debug, static, architecture)")
    else()
        message(STATUS "Release binary validation: ${BoldYellow}disabled${ColorReset} (llvm-readelf not found)")
    endif()
endif()

# =============================================================================
# Post-Build Processing (must be included after ascii-chat target is created)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/utils/PostBuild.cmake)
# Copy DLL dependencies to bin directory on Windows (for non-static builds)
if(WIN32)
    copy_windows_dlls(ascii-chat)
endif()
# Check static linking for Release builds
check_static_linking(ascii-chat)

# =============================================================================
# WebAssembly Build Targets
# =============================================================================
# WASM is built as a standalone project via src/web/CMakeLists.txt
# We pass the Emscripten toolchain file directly instead of wrapping through emcmake.

set(WASM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/web")
set(WASM_BUILD_DIR "${CMAKE_BINARY_DIR}/web")
set(WASM_DEPS_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/web")

# Find Emscripten toolchain file
if(DEFINED ENV{EMSDK})
    set(EMSCRIPTEN_SEARCH_PATHS "$ENV{EMSDK}/upstream/emscripten")
else()
    set(EMSCRIPTEN_SEARCH_PATHS
        "/usr/lib/emscripten"
        "/usr/local/lib/emscripten"
        "$ENV{HOME}/emsdk/upstream/emscripten"
    )
    if(HOMEBREW_PREFIX)
        list(APPEND EMSCRIPTEN_SEARCH_PATHS "${HOMEBREW_PREFIX}/share/emscripten")
    endif()
endif()

find_file(EMSCRIPTEN_TOOLCHAIN_FILE
    NAMES Emscripten.cmake
    PATHS ${EMSCRIPTEN_SEARCH_PATHS}
    PATH_SUFFIXES cmake/Modules/Platform
)

if(NOT EMSCRIPTEN_TOOLCHAIN_FILE)
    message(STATUS "Emscripten toolchain not found - WASM targets will not work")
endif()

set(WASM_CMAKE_ARGS
    -B "${WASM_BUILD_DIR}"
    "${WASM_SOURCE_DIR}"
    -DCMAKE_TOOLCHAIN_FILE=${EMSCRIPTEN_TOOLCHAIN_FILE}
    -DASCIICHAT_WEB_DEPS_DIR=${WASM_DEPS_DIR}
)

# Configure WASM build directory during CMake configuration (not during build)
if(EMSCRIPTEN_TOOLCHAIN_FILE)
    execute_process(
        COMMAND ${CMAKE_COMMAND} ${WASM_CMAKE_ARGS}
        WORKING_DIRECTORY "${WASM_SOURCE_DIR}"
        RESULT_VARIABLE WASM_CONFIG_RESULT
    )
    if(NOT WASM_CONFIG_RESULT EQUAL 0)
        message(WARNING "WASM configuration failed: ${WASM_CONFIG_RESULT}")
    endif()
endif()

# Mirror mode WASM target
if(NOT TARGET mirror-web)
    add_custom_target(mirror-web
        COMMAND ${CMAKE_COMMAND} ${WASM_CMAKE_ARGS}
        COMMAND ${CMAKE_COMMAND} --build "${WASM_BUILD_DIR}" --target mirror-web
        WORKING_DIRECTORY "${WASM_SOURCE_DIR}"
        COMMENT "Building WASM mirror module"
        VERBATIM
    )
endif()

# Client mode WASM target
if(NOT TARGET client-web)
    add_custom_target(client-web
        COMMAND ${CMAKE_COMMAND} ${WASM_CMAKE_ARGS}
        COMMAND ${CMAKE_COMMAND} --build "${WASM_BUILD_DIR}" --target client-web
        WORKING_DIRECTORY "${WASM_SOURCE_DIR}"
        COMMENT "Building WASM client module"
        VERBATIM
    )
endif()

# Combined WASM target (builds both mirror and client)
if(NOT TARGET web)
    add_custom_target(web
        COMMAND ${CMAKE_COMMAND} ${WASM_CMAKE_ARGS}
        COMMAND ${CMAKE_COMMAND} --build "${WASM_BUILD_DIR}" --target mirror-web
        COMMAND ${CMAKE_COMMAND} --build "${WASM_BUILD_DIR}" --target client-web
        WORKING_DIRECTORY "${WASM_SOURCE_DIR}"
        COMMENT "Building all WASM modules"
        VERBATIM
    )

    set(WASM_TARGETS_DEFINED TRUE CACHE BOOL "WASM targets have been defined" FORCE)
endif()

message(STATUS "WASM targets configured:")
message(STATUS "  mirror-web - Build mirror mode WASM only")
message(STATUS "  client-web - Build client mode WASM only")
message(STATUS "  web        - Build all WASM modules")
if(EMSCRIPTEN_TOOLCHAIN_FILE)
    message(STATUS "  Emscripten toolchain: ${EMSCRIPTEN_TOOLCHAIN_FILE}")
endif()
