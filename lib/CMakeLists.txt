# lib/ - Core application infrastructure
# Sources from: log/, logging/, options/, discovery/, debug/, ui/, session/

# Store lib directory since this file may be included() from parent scope
set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

set(CORE_SRCS
    ${LIB_DIR}/common.c
    ${LIB_DIR}/asciichat_errno.c
    ${LIB_DIR}/embedded_resources.c
    # log/
    ${LIB_DIR}/log/logging.c
    ${LIB_DIR}/log/format.c
    ${LIB_DIR}/log/colorize.c
    ${LIB_DIR}/log/mmap.c
    ${LIB_DIR}/log/grep.c
    ${LIB_DIR}/log/interactive_grep.c
    ${LIB_DIR}/log/file_parser.c
    ${LIB_DIR}/log/json.c
    # platform/terminal.c - unified color detection
    ${LIB_DIR}/platform/terminal.c
    # ui/
    ${LIB_DIR}/ui/terminal_screen.c
    ${LIB_DIR}/ui/help_screen.c
    ${LIB_DIR}/ui/splash.c
    ${LIB_DIR}/ui/server_status.c
    ${LIB_DIR}/ui/discovery_status.c
    # options/
    ${LIB_DIR}/options/colorscheme.c
    ${LIB_DIR}/options/options.c
    ${LIB_DIR}/options/common.c
    ${LIB_DIR}/options/enums.c
    ${LIB_DIR}/options/strings.c
    ${LIB_DIR}/options/levenshtein.c
    ${LIB_DIR}/options/rcu.c
    ${LIB_DIR}/options/layout.c
    ${LIB_DIR}/options/manpage.c
    ${LIB_DIR}/options/help_api.c
    # options/builder/
    ${LIB_DIR}/options/builder/builder.c
    ${LIB_DIR}/options/builder/handlers.c
    ${LIB_DIR}/options/builder/help.c
    # options/config/
    ${LIB_DIR}/options/config/config.c
    ${LIB_DIR}/options/config/schema.c
    ${LIB_DIR}/options/config/presets.c
    # options/registry/
    ${LIB_DIR}/options/registry/metadata.c
    ${LIB_DIR}/options/registry/general.c
    ${LIB_DIR}/options/registry/logging.c
    ${LIB_DIR}/options/registry/configuration.c
    ${LIB_DIR}/options/registry/terminal.c
    ${LIB_DIR}/options/registry/webcam.c
    ${LIB_DIR}/options/registry/display.c
    ${LIB_DIR}/options/registry/network.c
    ${LIB_DIR}/options/registry/security.c
    ${LIB_DIR}/options/registry/media.c
    ${LIB_DIR}/options/registry/audio.c
    ${LIB_DIR}/options/registry/database.c
    ${LIB_DIR}/options/registry/registry.c
    ${LIB_DIR}/options/registry/core.c
    ${LIB_DIR}/options/registry/public_api.c
    ${LIB_DIR}/options/registry/mode_defaults.c
    # options/parsing/
    ${LIB_DIR}/options/parsing/parsers.c
    ${LIB_DIR}/options/parsing/validation.c
    ${LIB_DIR}/options/parsing/actions.c
    # options/manpage/
    ${LIB_DIR}/options/manpage/resources.c
    ${LIB_DIR}/options/manpage/parser.c
    ${LIB_DIR}/options/manpage/formatter.c
    ${LIB_DIR}/options/manpage/merger.c
    ${LIB_DIR}/options/manpage/content/options.c
    ${LIB_DIR}/options/manpage/content/environment.c
    ${LIB_DIR}/options/manpage/content/usage.c
    ${LIB_DIR}/options/manpage/content/examples.c
    ${LIB_DIR}/options/manpage/content/positional.c
    # options/completions/
    ${LIB_DIR}/options/completions/completions.c
    ${LIB_DIR}/options/completions/bash.c
    ${LIB_DIR}/options/completions/fish.c
    ${LIB_DIR}/options/completions/zsh.c
    ${LIB_DIR}/options/completions/powershell.c
    # version
    ${LIB_DIR}/version.c
    # discovery/ (library)
    ${LIB_DIR}/discovery/session.c
    ${LIB_DIR}/discovery/database.c
    ${LIB_DIR}/discovery/identity.c
    ${LIB_DIR}/discovery/strings.c
    ${LIB_DIR}/discovery/adjectives.c
    ${LIB_DIR}/discovery/nouns.c
    # src/discovery/ (client code that's part of lib)
    ${CMAKE_SOURCE_DIR}/src/discovery/nat.c
    ${CMAKE_SOURCE_DIR}/src/discovery/negotiate.c
    ${CMAKE_SOURCE_DIR}/src/discovery/session.c
    ${CMAKE_SOURCE_DIR}/src/discovery/webrtc.c
    # Third-party: tomlc17
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17/src/tomlc17.c
)

# Debug sources only in non-Release builds
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND CORE_SRCS
        ${LIB_DIR}/debug/lock.c
        ${LIB_DIR}/debug/memory.c
    )
endif()

# Session library
set(SESSION_SRCS
    ${LIB_DIR}/session/consensus.c
    ${LIB_DIR}/session/consensus_integration.c
    ${LIB_DIR}/session/capture.c
    ${LIB_DIR}/session/display.c
    ${LIB_DIR}/session/render.c
    ${LIB_DIR}/session/settings.c
    ${LIB_DIR}/session/audio.c
    ${LIB_DIR}/session/participant.c
    ${LIB_DIR}/session/host.c
    ${LIB_DIR}/session/keyboard_handler.c
    ${LIB_DIR}/session/session_log_buffer.c
)

# Include subdirectories that define module source variables
add_subdirectory(${LIB_DIR}/util ${CMAKE_BINARY_DIR}/lib/util)
add_subdirectory(${LIB_DIR}/platform ${CMAKE_BINARY_DIR}/lib/platform)
add_subdirectory(${LIB_DIR}/crypto ${CMAKE_BINARY_DIR}/lib/crypto)
add_subdirectory(${LIB_DIR}/video ${CMAKE_BINARY_DIR}/lib/video)
add_subdirectory(${LIB_DIR}/audio ${CMAKE_BINARY_DIR}/lib/audio)
add_subdirectory(${LIB_DIR}/network ${CMAKE_BINARY_DIR}/lib/network)
add_subdirectory(${LIB_DIR}/tooling ${CMAKE_BINARY_DIR}/lib/tooling)

# Now include the Libraries configuration (moved from cmake/targets/Libraries.cmake)
# =============================================================================
# Libraries Module
# =============================================================================
# This module creates all library targets and their dependencies
#
# Prerequisites:
#   - All *_SRCS variables must be set (via SourceFiles.cmake)
#   - Platform detection complete
#   - Dependencies found (via Dependencies.cmake)
#   - USE_MIMALLOC, USE_MUSL known
#
# Outputs:
#   - All ascii-chat-* library targets
#   - ascii-chat-lib interface library
#   - ascii-chat-static and ascii-chat-shared unified libraries
# =============================================================================

include(CheckCCompilerFlag)
include(${CMAKE_SOURCE_DIR}/cmake/utils/CoreDependencies.cmake)

# Check for warning flags that may not exist in older Clang versions
# -Wno-unterminated-string-initialization was added in Clang 20
check_c_compiler_flag("-Wno-unterminated-string-initialization" HAVE_WNO_UNTERMINATED_STRING_INIT)

# Always use OBJECT libraries (all build types)
# OBJECT libraries eliminate duplicate compilation: modules compile once,
# shared library and tests both link to the same object files
# This simplifies the build configuration and ensures consistent behavior
set(BUILDING_OBJECT_LIBS TRUE)

# In CMakeLists.txt - for macOS
if(APPLE)
    # Use standard install paths, let Homebrew handle relocation
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

# Helper macro to create a module with common settings
macro(create_ascii_chat_module MODULE_NAME MODULE_SRCS)
    # Always use OBJECT libraries to eliminate duplication
    # (shared library and tests both link to the same object files, compiled once)
    add_library(${MODULE_NAME} OBJECT ${MODULE_SRCS})
    # OBJECT libraries need PIC for linking into shared libraries
    set_target_properties(${MODULE_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Always explicitly add -fPIC for non-Windows to ensure it's applied
    # (POSITION_INDEPENDENT_CODE property alone doesn't always work with musl toolchain)
    if(NOT WIN32)
        target_compile_options(${MODULE_NAME} PRIVATE -fPIC)
    endif()
    # Use global-dynamic TLS model since OBJECT libs are shared between the main
    # binary and the shared library. initial-exec generates TPOFF32 relocations that
    # are invalid in shared libraries. With LTO, the linker performs TLS relaxation
    # (global-dynamic → initial-exec) in the executable link automatically.
    # For musl with shared library, explicitly use global-dynamic to avoid TPOFF32 errors
    if(NOT WIN32)
        target_compile_options(${MODULE_NAME} PRIVATE -ftls-model=global-dynamic)
    endif()
    # Mark all symbols for export when building DLL from OBJECT libraries
    target_compile_definitions(${MODULE_NAME} PRIVATE
        _WIN32_WINNT=0x0A00  # Windows 10
        BUILDING_ASCIICHAT_DLL=1
    )

    if(ASCIICHAT_ENABLE_UNITY_BUILDS)
        set_target_properties(${MODULE_NAME} PROPERTIES UNITY_BUILD ON)
    endif()

    # For musl builds with shared library, disable LTO to avoid TLS relocation issues
    # (known LLVM limitation - issue #55909)
    if(ASCIICHAT_ENABLE_IPO)
        if(USE_MUSL)
            set_property(TARGET ${MODULE_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
        else()
            set_property(TARGET ${MODULE_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    endif()

    # Version dependency (build-timer-start removed to prevent unnecessary rebuilds)
    add_dependencies(${MODULE_NAME} generate_version)

    # Include paths
    target_include_directories(${MODULE_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>  # Public headers during build
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>  # Generated headers during build
        $<INSTALL_INTERFACE:include>  # Installed headers (relative to prefix)
    )

    # Build directory for llvm-symbolizer --debug-file-directory (debug builds only)
    # Only include BUILD_DIR in debug builds to avoid embedding build paths in release binaries
    # Note: Release builds will not have BUILD_DIR defined, so llvm-symbolizer will work without it
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
        target_compile_definitions(${MODULE_NAME} PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
    endif()

    # MI_DEBUG for mimalloc
    if(DEFINED MIMALLOC_DEBUG_LEVEL)
        target_compile_definitions(${MODULE_NAME} PRIVATE MI_DEBUG=${MIMALLOC_DEBUG_LEVEL})
    endif()

    # Mimalloc include directory (use MIMALLOC_INCLUDE_DIRS for system vs FetchContent)
    if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
        target_include_directories(${MODULE_NAME} PRIVATE ${MIMALLOC_INCLUDE_DIRS})
    endif()

    # Musl flag
    if(USE_MUSL)
        target_compile_definitions(${MODULE_NAME} PRIVATE USE_MUSL=1)
    endif()
endmacro()

# -----------------------------------------------------------------------------
# Module 1: Utilities (no dependencies)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-util "${UTIL_SRCS}")

# utf8proc: Define UTF8PROC_STATIC to avoid dllexport/dllimport issues on Windows
# We build utf8proc as part of our library, not as a separate DLL
target_compile_definitions(ascii-chat-util PRIVATE UTF8PROC_STATIC)

# PCRE2: Define PCRE2_CODE_UNIT_WIDTH=8 for 8-bit API (UTF-8 support)
# CRITICAL: Must be defined before including pcre2.h in any source file
# PUBLIC because include/ascii-chat/util/pcre2.h includes pcre2.h
target_compile_definitions(ascii-chat-util PUBLIC PCRE2_CODE_UNIT_WIDTH=8)

# Add PCRE2 include directory (must be before linking)
# PUBLIC because pcre2.h is included from public header include/ascii-chat/util/pcre2.h
if(PCRE2_INCLUDE_DIRS)
    target_include_directories(ascii-chat-util PUBLIC ${PCRE2_INCLUDE_DIRS})
endif()

# Link PCRE2 library
# Two modes: (1) pkg-config sets LIBRARY_DIRS + LIBRARIES, (2) static lib sets full path in LIBRARIES
if(PCRE2_LIBRARIES)
    # If LIBRARY_DIRS is set (pkg-config mode), add link directory first
    if(PCRE2_LIBRARY_DIRS)
        target_link_directories(ascii-chat-util PRIVATE ${PCRE2_LIBRARY_DIRS})
    endif()
    # Link the library (either library name or full path)
    target_link_libraries(ascii-chat-util ${PCRE2_LIBRARIES} pthread)
else()
    message(FATAL_ERROR "PCRE2 library not found. PCRE2_LIBRARIES=${PCRE2_LIBRARIES}")
endif()

# Add dependency on PCRE2 build target if building from source (musl)
if(DEFINED PCRE2_BUILD_TARGET)
    add_dependencies(ascii-chat-util ${PCRE2_BUILD_TARGET})
endif()

# -----------------------------------------------------------------------------
# Module 2: Data Structures (depends on: util)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-data-structures "${DATA_STRUCTURES_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-data-structures ascii-chat-util)
endif()

# -----------------------------------------------------------------------------
# Module 3: Platform Abstraction (depends on: util, data-structures)
# -----------------------------------------------------------------------------

create_ascii_chat_module(ascii-chat-platform "${PLATFORM_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-platform
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-core
    )
endif()

# Add kernel headers for musl builds (needed for V4L2)
if(USE_MUSL AND DEFINED MUSL_DEPS_DIR AND EXISTS "${MUSL_DEPS_DIR}/musl-deps/kernel-headers")
    target_include_directories(ascii-chat-platform PRIVATE
        "${MUSL_DEPS_DIR}/musl-deps/kernel-headers"
    )
endif()

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(ascii-chat-platform
        ${WS2_32_LIB}
        ${USER32_LIB}
        ${ADVAPI32_LIB}
        ${DBGHELP_LIB}
        ${MF_LIB}
        ${MFPLAT_LIB}
        ${MFREADWRITE_LIB}
        ${MFUUID_LIB}
        ${OLE32_LIB}
        crypt32  # For Windows crypto certificate functions
    )
else()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-platform
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
            ${CORESERVICES_FRAMEWORK}
            ${IOKIT_FRAMEWORK}  # Required for IOPMLib.h (keepawake functionality)
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-platform ${CMAKE_THREAD_LIBS_INIT})
        # Link libsystemd on Linux for keepawake functionality
        # Skip for musl builds: system libsystemd is glibc-linked and incompatible with static musl linking
        if(NOT USE_MUSL)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(LIBSYSTEMD REQUIRED libsystemd)
            target_include_directories(ascii-chat-platform PRIVATE ${LIBSYSTEMD_INCLUDE_DIRS})
            target_link_libraries(ascii-chat-platform ${LIBSYSTEMD_LIBRARIES})
            target_compile_definitions(ascii-chat-platform PRIVATE HAVE_LIBSYSTEMD=1)
            message(STATUS "libsystemd found: keepawake support enabled")
        else()
            message(STATUS "libsystemd skipped: musl static builds use glibc-independent keepawake")
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# Module 3: Cryptography (depends on: util, platform)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-crypto "${CRYPTO_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-crypto
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-network
        ${LIBSODIUM_LIBRARIES}
    )
else()
    # For OBJECT libs, link external deps only (no module deps)
    target_link_libraries(ascii-chat-crypto ${LIBSODIUM_LIBRARIES})
endif()

# Add libsodium include directory (for GCC builds from source)
if(LIBSODIUM_INCLUDE_DIRS)
    target_include_directories(ascii-chat-crypto PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
endif()

# Add dependency on libsodium build target if building from source
if(DEFINED LIBSODIUM_BUILD_TARGET)
    add_dependencies(ascii-chat-crypto ${LIBSODIUM_BUILD_TARGET})
endif()

# PCRE2: Configure for crypto module (for SSH parser regex)
# Define PCRE2_CODE_UNIT_WIDTH=8 for 8-bit API (UTF-8 support)
target_compile_definitions(ascii-chat-crypto PRIVATE PCRE2_CODE_UNIT_WIDTH=8)

# Add PCRE2 include directory
if(PCRE2_INCLUDE_DIRS)
    target_include_directories(ascii-chat-crypto PRIVATE ${PCRE2_INCLUDE_DIRS})
endif()

# Link PCRE2 library
if(PCRE2_LIBRARY_DIRS AND PCRE2_LIBRARIES)
    if(BUILDING_OBJECT_LIBS)
        # For OBJECT libs, add PCRE2 to link libraries
        target_link_libraries(ascii-chat-crypto ${PCRE2_LIBRARIES})
    endif()
    target_link_directories(ascii-chat-crypto PRIVATE ${PCRE2_LIBRARY_DIRS})
else()
    message(WARNING "PCRE2 not found for crypto module (SSH parser regex will not work)")
endif()

# Add dependency on PCRE2 build target if building from source
if(DEFINED PCRE2_BUILD_TARGET)
    add_dependencies(ascii-chat-crypto ${PCRE2_BUILD_TARGET})
endif()

# Add BearSSL if available
if(BEARSSL_FOUND)
    target_link_libraries(ascii-chat-crypto ${BEARSSL_LIBRARIES})
endif()

# Configure libsodium-bcrypt-pbkdf source files
# These files need src/ directory in include path to find their own internal headers
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Disable false-positive warnings for bcrypt_pbkdf.c (third-party code)
    set(BCRYPT_PBKDF_WARNINGS "-Wno-sizeof-array-div")
    if(HAVE_WNO_UNTERMINATED_STRING_INIT)
        list(APPEND BCRYPT_PBKDF_WARNINGS "-Wno-unterminated-string-initialization")
    endif()
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
        PROPERTIES COMPILE_OPTIONS "${BCRYPT_PBKDF_WARNINGS}"
    )
endif()

# Add src/ directory for all libsodium-bcrypt-pbkdf source files to find internal headers
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    PROPERTIES COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src"
)

# -----------------------------------------------------------------------------
# Module 4: SIMD (depends on: util, core, video)
# Note: Circular dependency with video (simd needs video for benchmark code,
#       video needs simd for SIMD functions). This is resolved at link time
#       since both libraries are linked together in executables.
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-simd "${SIMD_SRCS}")

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-simd PRIVATE
        ascii-chat-util
        ascii-chat-core
        ascii-chat-video
    )
endif()

# -----------------------------------------------------------------------------
# Module 5: Video Processing (depends on: util, platform, core, simd)
# -----------------------------------------------------------------------------
# Add FFmpeg include directories BEFORE creating the module (for media file streaming)
# Use SYSTEM to suppress warnings from third-party headers (e.g., FFmpeg's undefined bit shift behavior)
if(FFMPEG_FOUND)
    include_directories(SYSTEM ${FFMPEG_INCLUDE_DIRS})
endif()

create_ascii_chat_module(ascii-chat-video "${VIDEO_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-video
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-core
        ascii-chat-simd
    )
    # Link FFmpeg for media file streaming
    if(FFMPEG_FOUND)
        target_link_libraries(ascii-chat-video ${FFMPEG_LINK_LIBRARIES})
    endif()
endif()

# PCRE2: Configure for media module (for YouTube video ID extraction)
# Define PCRE2_CODE_UNIT_WIDTH=8 for 8-bit API (UTF-8 support)
target_compile_definitions(ascii-chat-video PRIVATE PCRE2_CODE_UNIT_WIDTH=8)

# Add PCRE2 include directory
if(PCRE2_INCLUDE_DIRS)
    target_include_directories(ascii-chat-video PRIVATE ${PCRE2_INCLUDE_DIRS})
endif()

# Link PCRE2 library
if(PCRE2_LIBRARY_DIRS AND PCRE2_LIBRARIES)
    if(BUILDING_OBJECT_LIBS)
        # For OBJECT libs, add PCRE2 to link libraries
        target_link_libraries(ascii-chat-video ${PCRE2_LIBRARIES})
    endif()
    target_link_directories(ascii-chat-video PRIVATE ${PCRE2_LIBRARY_DIRS})
else()
    message(WARNING "PCRE2 not found for media module (YouTube video ID extraction will not work)")
endif()

# Add dependency on PCRE2 build target if building from source
if(DEFINED PCRE2_BUILD_TARGET)
    add_dependencies(ascii-chat-video ${PCRE2_BUILD_TARGET})
endif()

# -----------------------------------------------------------------------------
# Module 6: Audio Processing (depends on: util, platform, data-structures)
# Add WebRTC AEC3 include directories BEFORE creating the module
if(TARGET webrtc_audio_processing)
    get_target_property(WEBRTC_INCLUDES webrtc_audio_processing INTERFACE_INCLUDE_DIRECTORIES)
    if(WEBRTC_INCLUDES)
        # Set globally so they apply during compilation
        include_directories(${WEBRTC_INCLUDES})
    endif()
endif()

# Now create the module with WebRTC includes in scope
create_ascii_chat_module(ascii-chat-audio "${AUDIO_SRCS}")

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-audio
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-data-structures
        ${PORTAUDIO_LIBRARIES}
        ${OPUS_LIBRARIES}
        webrtc_audio_processing
    )
else()
    # For OBJECT libs, link external deps only
    target_link_libraries(ascii-chat-audio ${PORTAUDIO_LIBRARIES} ${OPUS_LIBRARIES} webrtc_audio_processing)
endif()

# Link platform-specific audio libraries
# macOS: CoreAudio, AudioUnit, AudioToolbox (required for static portaudio)
# Linux: ALSA + JACK (vcpkg's PortAudio includes JACK support, so we must link it)
# Windows: WASAPI/DirectSound (handled by platform module)
# Note: vcpkg PortAudio builds include JACK support, so we link libjack if available
if(APPLE)
    target_link_libraries(ascii-chat-audio
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${CORESERVICES_FRAMEWORK}
    )
elseif(PLATFORM_LINUX AND JACK_LIB AND NOT USE_MUSL)
    # Link JACK on Linux if available (required by vcpkg's PortAudio build)
    # Skip for musl: musl PortAudio is built with ALSA only, system libjack.so is glibc-linked
    target_link_libraries(ascii-chat-audio ${JACK_LIB})
endif()

# Add build dependency for PortAudio from-source builds (non-musl Linux/macOS)
if(DEFINED PORTAUDIO_BUILD_TARGET AND TARGET ${PORTAUDIO_BUILD_TARGET})
    add_dependencies(ascii-chat-audio ${PORTAUDIO_BUILD_TARGET})
endif()

# -----------------------------------------------------------------------------
# Module 8: Core Infrastructure (depends on: util, platform)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-core "${CORE_SRCS}")

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-core
        ascii-chat-util
        ascii-chat-platform
        ${SQLITE3_LIBRARIES}
    )
endif()

# PCRE2: Configure for core module (for session string format validation)
# Define PCRE2_CODE_UNIT_WIDTH=8 for 8-bit API (UTF-8 support)
target_compile_definitions(ascii-chat-core PRIVATE PCRE2_CODE_UNIT_WIDTH=8)

# Add PCRE2 include directory
if(PCRE2_INCLUDE_DIRS)
    target_include_directories(ascii-chat-core PRIVATE ${PCRE2_INCLUDE_DIRS})
endif()

# Link PCRE2 library
if(PCRE2_LIBRARY_DIRS AND PCRE2_LIBRARIES)
    if(BUILDING_OBJECT_LIBS)
        # For OBJECT libs, add PCRE2 to link libraries
        target_link_libraries(ascii-chat-core ${PCRE2_LIBRARIES})
    endif()
    target_link_directories(ascii-chat-core PRIVATE ${PCRE2_LIBRARY_DIRS})
else()
    message(WARNING "PCRE2 not found for core module (session string validation will not work)")
endif()

# Add dependency on PCRE2 build target if building from source
if(DEFINED PCRE2_BUILD_TARGET)
    add_dependencies(ascii-chat-core ${PCRE2_BUILD_TARGET})
endif()

# Math library (Unix only - Windows has math functions in C runtime)
if(NOT WIN32)
    target_link_libraries(ascii-chat-core m)
endif()

# Special musl handling for libexecinfo
if(USE_MUSL)
    target_link_libraries(ascii-chat-core ${LIBEXECINFO_PREFIX}/lib/libexecinfo.a)
    add_dependencies(ascii-chat-core libexecinfo-musl)
endif()

# mimalloc for core
if(USE_MIMALLOC)
    if(TARGET mimalloc-static)
        add_dependencies(ascii-chat-core mimalloc-static)
    endif()
    if(ASCIICHAT_MIMALLOC_LINK_LIB)
        target_link_libraries(ascii-chat-core ${ASCIICHAT_MIMALLOC_LINK_LIB})
    elseif(MIMALLOC_LIBRARIES)
        target_link_libraries(ascii-chat-core ${MIMALLOC_LIBRARIES})
    endif()
endif()

# yyjson - Fast JSON library for structured logging
# Prevents reader API from being declared in our translation units (only writer needed)
target_compile_definitions(ascii-chat-core PRIVATE YYJSON_DISABLE_READER=1)

# Add yyjson include directory
if(YYJSON_INCLUDE_DIRS)
    target_include_directories(ascii-chat-core PRIVATE ${YYJSON_INCLUDE_DIRS})
endif()

# Link yyjson library
if(USE_MUSL)
    if(DEFINED YYJSON_BUILD_TARGET)
        add_dependencies(ascii-chat-core ${YYJSON_BUILD_TARGET})
    endif()
endif()

# Link yyjson - use YYJSON_LIBRARIES which is set by Yyjson.cmake
if(YYJSON_LIBRARIES)
    target_link_libraries(ascii-chat-core ${YYJSON_LIBRARIES})
else()
    # Fallback: try to link against system yyjson
    target_link_libraries(ascii-chat-core yyjson)
endif()

# -----------------------------------------------------------------------------
# Module 9: Panic Instrumentation Runtime (depends on: util, platform, core)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-panic "${TOOLING_PANIC_SRCS}")
target_include_directories(ascii-chat-panic PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-panic
        PRIVATE
            ascii-chat-util
            ascii-chat-platform
            ascii-chat-core
    )
endif()
if(NOT WIN32 AND TARGET Threads::Threads)
    target_link_libraries(ascii-chat-panic PUBLIC Threads::Threads)
endif()

# Note: Module 9b (defer runtime) was removed - now using direct code insertion

# -----------------------------------------------------------------------------
# Module 10: Network (depends on: util, platform, crypto, core)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-network "${NETWORK_SRCS}")
# Exclude main() function from mdns library
target_compile_definitions(ascii-chat-network PRIVATE MDNS_NO_MAIN)

# PCRE2: Add include directory and define PCRE2_CODE_UNIT_WIDTH=8
# Network module uses PCRE2 in sdp.c and ice.c
if(PCRE2_INCLUDE_DIRS)
    target_include_directories(ascii-chat-network PRIVATE ${PCRE2_INCLUDE_DIRS})
endif()
if(LIBWEBSOCKETS_INCLUDE_DIRS)
    target_include_directories(ascii-chat-network PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
endif()
if(LIBWEBSOCKETS_BUILD_TARGET)
    add_dependencies(ascii-chat-network ${LIBWEBSOCKETS_BUILD_TARGET})
endif()
target_compile_definitions(ascii-chat-network PRIVATE PCRE2_CODE_UNIT_WIDTH=8)
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-network
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-crypto
        ascii-chat-core
        ${PCRE2_LIBRARIES}
        ${ZSTD_LIBRARIES}
        ${SQLITE3_LIBRARIES}  # Rate limiting SQLite backend
        libdatachannel  # WebRTC DataChannels for P2P transport
        OpenSSL::Crypto  # Required by libdatachannel for TLS/DTLS
    )
    # Link libwebsockets
    if(TARGET PkgConfig::LIBWEBSOCKETS)
        target_link_libraries(ascii-chat-network PkgConfig::LIBWEBSOCKETS)
    else()
        target_link_libraries(ascii-chat-network ${LIBWEBSOCKETS_LIBRARIES})
    endif()
    # Link miniupnpc if available (optional UPnP/NAT-PMP support)
    if(MINIUPNPC_FOUND)
        target_include_directories(ascii-chat-network PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
        target_link_libraries(ascii-chat-network ${MINIUPNPC_LIBRARIES})
        # Also link libnatpmp on macOS
        if(NATPMP_LIBRARY)
            target_link_libraries(ascii-chat-network ${NATPMP_LIBRARY})
        endif()
    endif()
else()
    # For OBJECT libs, link external deps only (OpenSSL needed by libdatachannel for TLS/DTLS)
    target_link_libraries(ascii-chat-network ${PCRE2_LIBRARIES} ${ZSTD_LIBRARIES} ${SQLITE3_LIBRARIES} libdatachannel OpenSSL::Crypto)
    # Link libwebsockets
    if(TARGET PkgConfig::LIBWEBSOCKETS)
        target_link_libraries(ascii-chat-network PkgConfig::LIBWEBSOCKETS)
    else()
        target_link_libraries(ascii-chat-network ${LIBWEBSOCKETS_LIBRARIES})
    endif()
    if(MINIUPNPC_FOUND)
        target_include_directories(ascii-chat-network PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
        target_link_libraries(ascii-chat-network ${MINIUPNPC_LIBRARIES})
        # Also link libnatpmp on macOS
        if(NATPMP_LIBRARY)
            target_link_libraries(ascii-chat-network ${NATPMP_LIBRARY})
        endif()
    endif()
endif()

# Core module was moved earlier in the dependency chain (Module 7)

# -----------------------------------------------------------------------------
# Module 11: Session Library (depends on: util, platform, core, video, audio)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-session "${SESSION_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-session
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-core
        ascii-chat-video
        ascii-chat-audio
    )
endif()

# =============================================================================
# Unified Library Targets (OPTIONAL - not built by default)
# =============================================================================
# These combine all modules into a single library for external projects.
# Build with: cmake --build build --target ascii-chat-static
#         or: cmake --build build --target ascii-chat-shared
# =============================================================================

# Shared unified library (libasciichat.so / libasciichat.dylib / asciichat.dll)
# Debug builds: Create from OBJECT libraries (eliminates duplication, tests link to same objects)
# Release builds: Create from sources (no duplication risk)
# Always use OBJECT libraries: Create shared library from OBJECT libraries
# This eliminates duplication: both the shared library and test executables
# link to the same compiled object files.
# Note: ascii-chat-panic is always included as it provides runtime logging functions
# that may be called by panic-instrumented code
add_library(ascii-chat-shared SHARED
        $<TARGET_OBJECTS:ascii-chat-util>
        $<TARGET_OBJECTS:ascii-chat-data-structures>
        $<TARGET_OBJECTS:ascii-chat-platform>
        $<TARGET_OBJECTS:ascii-chat-crypto>
        $<TARGET_OBJECTS:ascii-chat-simd>
        $<TARGET_OBJECTS:ascii-chat-video>
        $<TARGET_OBJECTS:ascii-chat-audio>
        $<TARGET_OBJECTS:ascii-chat-network>
        $<TARGET_OBJECTS:ascii-chat-core>
        $<TARGET_OBJECTS:ascii-chat-session>
        $<TARGET_OBJECTS:ascii-chat-panic>
    )
    if(ASCIICHAT_ENABLE_UNITY_BUILDS)
        set_target_properties(ascii-chat-shared PROPERTIES UNITY_BUILD ON)
    endif()

    # Disable LTO for musl builds BEFORE other properties - must come first
    # LTO generates TPOFF32 relocations for TLS variables that cannot be used with -shared
    # This is a known LLVM limitation (issue #55909) - LTO ignores TLS model flags
    if(USE_MUSL)
        set_target_properties(ascii-chat-shared PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION FALSE
        )
    endif()

    set_target_properties(ascii-chat-shared PROPERTIES
        OUTPUT_NAME "asciichat"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}  # DLL goes in bin/
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}  # .so goes in lib/
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}  # Import .lib goes in lib/
        WINDOWS_EXPORT_ALL_SYMBOLS FALSE  # Use generated .def file instead
        # Windows: VERSION/SOVERSION embed version info in PE header (no symlinks)
        # Library version is from lib/v* tags, separate from app version
        VERSION ${ASCIICHAT_LIB_VERSION}
        SOVERSION ${ASCIICHAT_LIB_VERSION_MAJOR}
    )

    # Windows: Explicitly set import library location for Windows
    if(WIN32)
        target_link_options(ascii-chat-shared PRIVATE "LINKER:/implib:${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/asciichat.lib")
    endif()

    # Auto-generate .def file from OBJECT libraries
    # Use a wrapper script to collect object files and generate .def
    # Object files are in CMakeFiles/<target>.dir/ directories
    set(MODULE_TARGETS
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

    # Generate the .def file using a custom command
    # Windows: Auto-generate .def file for symbol export
    if(WIN32)
        # Convert list to string for passing to script
        string(REPLACE ";" "," MODULE_TARGETS_STR "${MODULE_TARGETS}")

        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def
            COMMAND ${CMAKE_COMMAND}
                -DNM_TOOL=${CMAKE_NM}
                -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DMODULE_TARGETS=${MODULE_TARGETS_STR}
                -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def
                -DLIBRARY_NAME=asciichat
                -P ${CMAKE_SOURCE_DIR}/cmake/targets/GenerateDefFile.cmake
            DEPENDS ${MODULE_TARGETS}
            COMMENT "Generating Windows .def file from object files"
            VERBATIM
        )

        # Create a target for the .def file generation
        add_custom_target(generate-def-file DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def)

        # Make the DLL depend on the .def file generation
        add_dependencies(ascii-chat-shared generate-def-file)

        # Use the generated .def file for linking
        target_link_options(ascii-chat-shared PRIVATE
            "LINKER:/DEF:${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def"
        )
    endif()

    # Add include directories needed by the OBJECT libraries
    target_include_directories(ascii-chat-shared PRIVATE
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    )

    if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PRIVATE $<BUILD_INTERFACE:${MIMALLOC_INCLUDE_DIRS}>)
    endif()

    # Add yyjson include directory
    if(YYJSON_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PRIVATE ${YYJSON_INCLUDE_DIRS})
    endif()

    # Add PCRE2 include directory and library directory for the shared library
    # PUBLIC because public header include/ascii-chat/util/pcre2.h includes pcre2.h
    if(PCRE2_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PUBLIC ${PCRE2_INCLUDE_DIRS})
    endif()
    # PCRE2_CODE_UNIT_WIDTH=8 must be defined before including pcre2.h
    target_compile_definitions(ascii-chat-shared PUBLIC PCRE2_CODE_UNIT_WIDTH=8)
    if(PCRE2_LIBRARY_DIRS)
        target_link_directories(ascii-chat-shared PRIVATE ${PCRE2_LIBRARY_DIRS})
    endif()

    # Unix: Set proper visibility and TLS model for shared library
    # Shared libraries must use global-dynamic or local-dynamic TLS model (not initial-exec)
    # initial-exec is only valid for executables and generates incompatible TPOFF32 relocations
    if(NOT WIN32)
        target_compile_options(ascii-chat-shared PRIVATE
            -fvisibility=default
            -ftls-model=global-dynamic
            -fPIC
        )
        # Force TLS model in ThinLTO optimizer - pass via linker plugin
        target_link_options(ascii-chat-shared PRIVATE
            -Wl,-mllvm,-relocation-model=pic
        )
    endif()

    # Add dependencies from modules
    add_dependencies(ascii-chat-shared generate_version)
    if(DEFINED LIBSODIUM_BUILD_TARGET)
        add_dependencies(ascii-chat-shared ${LIBSODIUM_BUILD_TARGET})
    endif()
    # Add musl dependencies to ensure libraries are built before linking
    if(USE_MUSL)
        add_dependencies(ascii-chat-shared
            zstd-musl
            libsodium-musl
            opus-musl
            pcre2-musl
            portaudio-musl
            alsa-lib-musl
            libexecinfo-musl
            openssl-musl
            ffmpeg-musl
            libwebsockets-musl
        )
    endif()

    # Link external dependencies (libsodium, zstd, opus, etc.)
    # These are needed because shared library is created from OBJECT files,
    # which don't automatically bring in their module dependencies
    target_link_libraries(ascii-chat-shared PRIVATE
        ${LIBSODIUM_LIBRARIES}
        ${ZSTD_LIBRARIES}
        ${OPUS_LIBRARIES}
        ${SQLITE3_LIBRARIES}
        ${PCRE2_LIBRARIES}
        ${YYJSON_LIBRARIES}
    )

    # Link PortAudio with platform-specific dependencies
    if(APPLE)
        target_link_libraries(ascii-chat-shared PRIVATE
            ${PORTAUDIO_LIBRARIES}
            ${COREAUDIO_FRAMEWORK}
            ${AUDIOUNIT_FRAMEWORK}
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${CORESERVICES_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX AND JACK_LIB)
        target_link_libraries(ascii-chat-shared PRIVATE
            ${PORTAUDIO_LIBRARIES}
            ${JACK_LIB}
        )
    else()
        target_link_libraries(ascii-chat-shared PRIVATE ${PORTAUDIO_LIBRARIES})
    endif()

    # Link libsystemd (required for keepawake)
    if(NOT USE_MUSL)
        target_link_libraries(ascii-chat-shared PRIVATE ${LIBSYSTEMD_LIBRARIES})
    endif()

    # Link WebRTC and other network libraries
    target_link_libraries(ascii-chat-shared PRIVATE
        libdatachannel
        OpenSSL::Crypto
    )
    if(MINIUPNPC_FOUND)
        target_include_directories(ascii-chat-shared PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
        target_link_libraries(ascii-chat-shared PRIVATE ${MINIUPNPC_LIBRARIES})
        if(NATPMP_LIBRARY)
            target_link_libraries(ascii-chat-shared PRIVATE ${NATPMP_LIBRARY})
        endif()
    endif()
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    # Link libwebsockets
    if(TARGET PkgConfig::LIBWEBSOCKETS)
        target_link_libraries(ascii-chat-shared PRIVATE PkgConfig::LIBWEBSOCKETS)
    else()
        target_link_libraries(ascii-chat-shared PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
    endif()

    # Suppress linker warnings about duplicate debug symbols in libdatachannel
    # (libdatachannel embeds debug symbols with invalid timestamps - linker warning only, not an error)
    if(APPLE)
        target_link_options(ascii-chat-shared PRIVATE "-Wl,-w")
    elseif(WIN32)
        # Delay-load datachannel.dll to avoid loading it for simple commands like --help/--version.
        # Note: We cannot delay-load MSVCP140D.dll because it breaks CRT initialization.
        target_link_options(ascii-chat-shared PRIVATE
            "LINKER:/DELAYLOAD:datachannel.dll"
        )
        target_link_libraries(ascii-chat-shared PRIVATE delayimp)
    endif()

    # Note: System library dependencies will be added below

# Add system library dependencies
if(WIN32)
    get_core_deps_libraries(CORE_LIBS)
    target_link_libraries(ascii-chat-shared PRIVATE
        ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
        ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
        crypt32  # For Windows crypto certificate functions (matches ascii-chat-platform)
        Winmm    # For timeBeginPeriod/timeEndPeriod
        ${CORE_LIBS}
    )
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    # Link miniupnpc if available (UPnP/NAT-PMP support)
    if(MINIUPNPC_FOUND)
        target_include_directories(ascii-chat-shared PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
        target_link_libraries(ascii-chat-shared PRIVATE ${MINIUPNPC_LIBRARIES})
        if(NATPMP_LIBRARY)
            target_link_libraries(ascii-chat-shared PRIVATE ${NATPMP_LIBRARY})
        endif()
    endif()
    # Windows DLL needs mimalloc built in (no LD_PRELOAD mechanism)
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
    endif()
    # Delay-load datachannel.dll to avoid loading it for simple commands like --help/--version.
    # Note: We cannot delay-load MSVCP140D.dll because it breaks CRT initialization.
    target_link_options(ascii-chat-shared PRIVATE
        "LINKER:/DELAYLOAD:datachannel.dll"
    )
    target_link_libraries(ascii-chat-shared PRIVATE delayimp)
else()
    # For musl builds, shared library links against system glibc libraries
    # Musl-built static libraries use incompatible TLS model (local-dynamic) and cannot be embedded in .so
    # Shared library users will have glibc, so we use system packages via pkg-config
    if(USE_MUSL)
        # Use system glibc libraries for shared library (not musl static libs)
        # Users of libasciichat.so will have glibc available
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PORTAUDIO_SYS portaudio-2.0)
        pkg_check_modules(ZSTD_SYS libzstd)
        pkg_check_modules(LIBSODIUM_SYS libsodium)
        pkg_check_modules(OPUS_SYS opus)

        get_core_deps_sys_libraries(CORE_SYS_LIBS)
        get_core_deps_sys_include_dirs(CORE_SYS_INCS)

        target_link_libraries(ascii-chat-shared PRIVATE
            ${CORE_SYS_LIBS}
            m
            ${CMAKE_THREAD_LIBS_INIT}
        )
        target_include_directories(ascii-chat-shared PRIVATE
            ${CORE_SYS_INCS}
        )

        # Link BearSSL and mimalloc static libraries into shared library
        # These are compiled with -fPIC and correct TLS model for shared library compatibility
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
        endif()
        # Link miniupnpc if available (UPnP/NAT-PMP support)
        if(MINIUPNPC_FOUND)
            target_include_directories(ascii-chat-shared PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
            target_link_libraries(ascii-chat-shared PRIVATE ${MINIUPNPC_LIBRARIES})
            if(NATPMP_LIBRARY)
                target_link_libraries(ascii-chat-shared PRIVATE ${NATPMP_LIBRARY})
            endif()
        endif()
        # Link mimalloc with force_load/whole-archive to export all symbols
        if(USE_MIMALLOC)
            # For FetchContent builds, use generator expression to get library path at build time
            if(TARGET mimalloc-shared)
                add_dependencies(ascii-chat-shared mimalloc-shared)
                # Linux musl: --whole-archive exports all symbols from static library
                target_link_libraries(ascii-chat-shared PRIVATE
                    -Wl,--whole-archive $<TARGET_FILE:mimalloc-shared> -Wl,--no-whole-archive)
            elseif(ASCIICHAT_MIMALLOC_SHARED_LINK_LIB)
                # Fallback: use explicit path if set (cached builds)
                target_link_libraries(ascii-chat-shared PRIVATE
                    -Wl,--whole-archive ${ASCIICHAT_MIMALLOC_SHARED_LINK_LIB} -Wl,--no-whole-archive)
            elseif(MIMALLOC_SHARED_LIBRARIES)
                # Another fallback: use MIMALLOC_SHARED_LIBRARIES if set
                target_link_libraries(ascii-chat-shared PRIVATE
                    -Wl,--whole-archive ${MIMALLOC_SHARED_LIBRARIES} -Wl,--no-whole-archive)
            endif()
        endif()
    else()
        # Non-musl builds use normal dependencies
        # Note: Core dependencies are PkgConfig::* IMPORTED targets that include library paths automatically
        get_core_deps_libraries(CORE_LIBS)

        # Check for JACK (required if PortAudio was built with JACK support)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(JACK jack)

        target_link_libraries(ascii-chat-shared PRIVATE
            ${CORE_LIBS} m ${SQLITE3_LIBRARIES}
            ${JACK_LIBRARIES}
        )
        # Link miniupnpc if available (UPnP/NAT-PMP support)
        if(MINIUPNPC_FOUND)
            target_include_directories(ascii-chat-shared PRIVATE ${MINIUPNPC_INCLUDE_DIRS})
            target_link_libraries(ascii-chat-shared PRIVATE ${MINIUPNPC_LIBRARIES})
            if(NATPMP_LIBRARY)
                target_link_libraries(ascii-chat-shared PRIVATE ${NATPMP_LIBRARY})
            endif()
        endif()
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
        endif()
        # Audio codec and processing
        if(OPUS_LIBRARIES)
            target_link_libraries(ascii-chat-shared PRIVATE ${OPUS_LIBRARIES})
        endif()
        # Link mimalloc into shared library (required for SAFE_MALLOC macros)
        # Use force_load/whole-archive to export all mimalloc symbols from the shared library
        if(USE_MIMALLOC)
            if(MIMALLOC_IS_SHARED_LIB)
                # System mimalloc is a shared library - link normally (no --whole-archive)
                # Symbols stay in separate .so, not embedded in our library
                if(TARGET mimalloc-shared)
                    target_link_libraries(ascii-chat-shared PRIVATE mimalloc-shared)
                elseif(MIMALLOC_LIBRARIES)
                    target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
                endif()
            elseif(TARGET mimalloc-shared)
                # Static library - use force_load/whole-archive to include all objects
                add_dependencies(ascii-chat-shared mimalloc-shared)
                if(APPLE)
                    # macOS: -force_load includes all objects from static library
                    target_link_libraries(ascii-chat-shared PRIVATE -force_load $<TARGET_FILE:mimalloc-shared>)
                else()
                    # Linux: --whole-archive includes all objects from static library
                    target_link_libraries(ascii-chat-shared PRIVATE
                        -Wl,--whole-archive $<TARGET_FILE:mimalloc-shared> -Wl,--no-whole-archive)
                endif()
            elseif(ASCIICHAT_MIMALLOC_SHARED_LINK_LIB)
                # Fallback: use explicit path if set (cached builds)
                if(APPLE)
                    target_link_libraries(ascii-chat-shared PRIVATE -force_load ${ASCIICHAT_MIMALLOC_SHARED_LINK_LIB})
                else()
                    target_link_libraries(ascii-chat-shared PRIVATE
                        -Wl,--whole-archive ${ASCIICHAT_MIMALLOC_SHARED_LINK_LIB} -Wl,--no-whole-archive)
                endif()
            elseif(MIMALLOC_SHARED_LIBRARIES)
                # Another fallback: use MIMALLOC_SHARED_LIBRARIES if set
                if(APPLE)
                    target_link_libraries(ascii-chat-shared PRIVATE -force_load ${MIMALLOC_SHARED_LIBRARIES})
                else()
                    target_link_libraries(ascii-chat-shared PRIVATE
                        -Wl,--whole-archive ${MIMALLOC_SHARED_LIBRARIES} -Wl,--no-whole-archive)
                endif()
            endif()
        endif()
        if(PLATFORM_DARWIN)
            target_link_libraries(ascii-chat-shared PRIVATE
                ${FOUNDATION_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK}
                ${COREMEDIA_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
                ${COREAUDIO_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK}
                ${CORESERVICES_FRAMEWORK} ${IOKIT_FRAMEWORK}
            )
        elseif(PLATFORM_LINUX)
            target_link_libraries(ascii-chat-shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
        endif()
    endif()
endif()

# Link external module dependencies to shared library
# These are needed for:
# - Debug builds: linking OBJECT libraries
# - Release builds: compilation and linking of source files that #include their headers
# The linker automatically deduplicates any multiple references
if(TARGET webrtc_audio_processing)
    target_link_libraries(ascii-chat-shared PRIVATE webrtc_audio_processing)
endif()

# Link libdatachannel for WebRTC P2P connections
if(TARGET libdatachannel)
    target_link_libraries(ascii-chat-shared PRIVATE libdatachannel)
endif()

# Link FFmpeg for media file streaming
if(FFMPEG_FOUND)
    target_link_libraries(ascii-chat-shared PRIVATE ${FFMPEG_LINK_LIBRARIES})
endif()

# Suppress linker warnings about duplicate debug symbols in libdatachannel
# (libdatachannel embeds debug symbols with invalid timestamps - linker warning only, not an error)
if(APPLE)
    target_link_options(ascii-chat-shared PRIVATE "-Wl,-w")
endif()

    # Ensure C++ standard library is linked for C++ dependencies
    # Set LINKER_LANGUAGE to CXX so CMake uses the C++ compiler for linking
    # This automatically includes the C++ standard library
    if(NOT WIN32)
        set_property(TARGET ascii-chat-shared PROPERTY LINKER_LANGUAGE CXX)
    endif()

# Add build timing for ascii-chat-shared library
add_custom_command(TARGET ascii-chat-shared PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Starting ascii-chat-shared timer"
    VERBATIM
)
add_custom_command(TARGET ascii-chat-shared POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Finishing ascii-chat-shared timer"
    VERBATIM
)

# =============================================================================
# macOS dSYM Generation for Shared Library (Debug/Dev builds only)
# =============================================================================
# Generate dSYM bundle for the shared library to enable backtraces with line numbers
# for library code (crypto, network, etc.)
if(APPLE AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
    find_program(DSYMUTIL_EXECUTABLE dsymutil)
    if(DSYMUTIL_EXECUTABLE)
        add_custom_command(TARGET ascii-chat-shared POST_BUILD
            COMMAND timeout 3 ${DSYMUTIL_EXECUTABLE} $<TARGET_FILE:ascii-chat-shared> -o $<TARGET_FILE:ascii-chat-shared>.dSYM
            COMMENT "Generating dSYM for ascii-chat-shared (enables atos backtraces with line numbers)"
            VERBATIM
        )
    endif()
endif()

# =============================================================================
# Required Symbol Definitions (used by both shared and static library validation)
# =============================================================================
# Note: log_debug/log_info/log_error are macros that call log_msg
set(ASCIICHAT_REQUIRED_SYMBOLS
    "log_msg"
    "asciichat_thread_create"
    "asciichat_thread_join"
    "mutex_init"
    "mutex_lock"
    "mutex_unlock"
    "send_packet"
    "receive_packet"
    "crypto_init"
)

# Internal symbols - linked but not necessarily exported (checked with lowercase 't')
set(ASCIICHAT_INTERNAL_SYMBOLS "")

# Add mimalloc symbols as internal when USE_MIMALLOC is enabled AND using static mimalloc
# When linking against shared mimalloc (.so), the symbols are NOT embedded in our library -
# they're resolved at runtime from libmimalloc.so, so we can't validate them here.
# Only static mimalloc links the symbols directly into our library.
# Note: On Windows, we skip this check because the import library (.lib) only contains
# exported symbol stubs, not internal symbols - those are only in the actual .dll
if(USE_MIMALLOC AND NOT MIMALLOC_IS_SHARED_LIB AND NOT WIN32)
    list(APPEND ASCIICHAT_INTERNAL_SYMBOLS "mi_malloc" "mi_free")
endif()

# Convert to comma-separated for passing through command line (semicolons cause escaping issues)
string(REPLACE ";" "," ASCIICHAT_SYMBOLS_CSV "${ASCIICHAT_REQUIRED_SYMBOLS}")
if(ASCIICHAT_INTERNAL_SYMBOLS)
    string(REPLACE ";" "," ASCIICHAT_INTERNAL_SYMBOLS_CSV "${ASCIICHAT_INTERNAL_SYMBOLS}")
endif()

# Validate symbols in shared library after build
if(ASCIICHAT_LLVM_NM_EXECUTABLE)
    # On Windows, check the import library (.lib) since DLLs don't expose symbols to llvm-nm
    # On Unix, check the shared object directly
    if(WIN32)
        set(SHARED_LIB_TO_VALIDATE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/asciichat.lib")
    else()
        set(SHARED_LIB_TO_VALIDATE "$<TARGET_FILE:ascii-chat-shared>")
    endif()

    # Build the validation command with optional internal symbols
    set(_VALIDATE_CMD
        ${CMAKE_COMMAND}
        -DLLVM_NM=${ASCIICHAT_LLVM_NM_EXECUTABLE}
        -DLIBRARY=${SHARED_LIB_TO_VALIDATE}
        -DSYMBOLS=${ASCIICHAT_SYMBOLS_CSV}
    )
    if(ASCIICHAT_INTERNAL_SYMBOLS_CSV)
        list(APPEND _VALIDATE_CMD -DINTERNAL_SYMBOLS=${ASCIICHAT_INTERNAL_SYMBOLS_CSV})
    endif()
    list(APPEND _VALIDATE_CMD -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateSymbols.cmake)

    add_custom_command(TARGET ascii-chat-shared POST_BUILD
        COMMAND ${_VALIDATE_CMD}
        COMMENT "Validating ascii-chat symbols in shared library"
        VERBATIM
    )
endif()

# =============================================================================
# Combined Static Library (built from OBJECT libraries with custom archiver)
# =============================================================================
# Create libasciichat.a from same OBJECT files used for shared library
# No duplicate compilation - compile once, output both shared and static
# Includes: our code + BearSSL + WebRTC AEC3 (not available via package managers)
# Users must install: libsodium, zstd, portaudio, opus, mimalloc
#
# Uses custom archive script to eliminate basename collisions:
# - Standard ar only stores basenames (common.c.o, transport.c.o)
# - Multiple modules have files with same names → duplicates in archive
# - Custom script encodes full paths in basenames before archiving
# - Result: No duplicates, 60% smaller library (3.9MB → 1.5MB)
# =============================================================================

# List of OBJECT library targets to archive
set(STATIC_LIB_MODULES
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

# Custom command to create static library with unique object names
# Uses generator expressions to get object files at build time
set(STATIC_LIB_OUTPUT "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libasciichat.a")

# Build semicolon-separated list of object file paths (generator expressions)
set(OBJECT_EXPR_LIST "")
foreach(MODULE ${STATIC_LIB_MODULES})
    if(OBJECT_EXPR_LIST)
        set(OBJECT_EXPR_LIST "${OBJECT_EXPR_LIST};$<TARGET_OBJECTS:${MODULE}>")
    else()
        set(OBJECT_EXPR_LIST "$<TARGET_OBJECTS:${MODULE}>")
    endif()
endforeach()

# Use file(GENERATE) to expand generator expressions at build time
set(OBJECT_LIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/static_lib_objects.txt")
file(GENERATE
    OUTPUT ${OBJECT_LIST_FILE}
    CONTENT "${OBJECT_EXPR_LIST}"
)

add_custom_command(
    OUTPUT ${STATIC_LIB_OUTPUT}
    COMMAND ${CMAKE_COMMAND}
        -DAR=${CMAKE_AR}
        -DRANLIB=${CMAKE_RANLIB}
        -DOUTPUT=${STATIC_LIB_OUTPUT}
        -DOBJECT_LIST_FILE=${OBJECT_LIST_FILE}
        -DCMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/scripts/create_static_archive.cmake
    DEPENDS ${STATIC_LIB_MODULES} ${OBJECT_LIST_FILE}
    COMMENT "Creating static archive with unique object names (eliminates duplicates)"
    VERBATIM
)

# Create an IMPORTED library that references the custom archive
add_library(ascii-chat-static-lib STATIC IMPORTED GLOBAL)
set_target_properties(ascii-chat-static-lib PROPERTIES
    IMPORTED_LOCATION ${STATIC_LIB_OUTPUT}
)

# Create a custom target to actually build the archive
add_custom_target(build-ascii-chat-static-lib
    DEPENDS ${STATIC_LIB_OUTPUT}
)

# Make the imported library depend on the build target
add_dependencies(ascii-chat-static-lib build-ascii-chat-static-lib)

# Create an INTERFACE library for dependency propagation
# (IMPORTED libraries can't have INTERFACE_LINK_LIBRARIES in older CMake)
add_library(ascii-chat-static-lib-deps INTERFACE)

# Link bundled dependencies (not available via package managers)
# BearSSL and WebRTC AEC3 are embedded in the static library
if(BEARSSL_LIBRARIES)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE ${BEARSSL_LIBRARIES})
endif()
if(WEBRTC_AEC3_LIBRARIES)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE ${WEBRTC_AEC3_LIBRARIES})
endif()

# External dependencies - users must install these via package manager
# These are linked but NOT embedded in the static library
target_link_libraries(ascii-chat-static-lib-deps INTERFACE
    ${PCRE2_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${OPUS_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    libdatachannel
    OpenSSL::Crypto
)

# Link mimalloc
if(USE_MIMALLOC)
    if(TARGET mimalloc-static)
        target_link_libraries(ascii-chat-static-lib-deps INTERFACE mimalloc-static)
    elseif(MIMALLOC_LIBRARIES)
        target_link_libraries(ascii-chat-static-lib-deps INTERFACE ${MIMALLOC_LIBRARIES})
    endif()
endif()

# Link FFmpeg for media file streaming
if(FFMPEG_FOUND)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE ${FFMPEG_LINK_LIBRARIES})
endif()

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE
        ${WS2_32_LIB}
        ${USER32_LIB}
        ${ADVAPI32_LIB}
        ${DBGHELP_LIB}
        ${MF_LIB}
        ${MFPLAT_LIB}
        ${MFREADWRITE_LIB}
        ${MFUUID_LIB}
        ${OLE32_LIB}
        crypt32
    )
elseif(APPLE)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE
        ${FOUNDATION_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${CORESERVICES_FRAMEWORK}
        ${ASCIICHAT_STATIC_LIBCXX_LIBS}
    )
else()
    # Linux
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE
        ${CMAKE_THREAD_LIBS_INIT}
        m
        dl
    )

    # Link C++ standard library
    if(USE_MUSL)
        # Musl builds use Alpine's musl-compatible libc++
        if(ALPINE_LIBCXX_STATIC AND ALPINE_LIBCXXABI_STATIC)
            target_link_libraries(ascii-chat-static-lib-deps INTERFACE
                ${ALPINE_LIBCXX_STATIC}
                ${ALPINE_LIBCXXABI_STATIC}
            )
            if(ALPINE_LIBUNWIND_STATIC)
                target_link_libraries(ascii-chat-static-lib-deps INTERFACE ${ALPINE_LIBUNWIND_STATIC})
            endif()
        endif()
    else()
        # Non-musl builds: link the C++ standard library
        target_link_libraries(ascii-chat-static-lib-deps INTERFACE c++)
    endif()
endif()

# Link libdatachannel for WebRTC P2P connections (static library)
# The libdatachannel INTERFACE target's dependencies propagate to ascii-chat-static-lib
if(TARGET libdatachannel)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE libdatachannel)
endif()

# Link OpenSSL for TURN credentials (required by network module)
# This is linked to ascii-chat-network module, but must also be exposed via the
# INTERFACE library for final executables since libasciichat.a includes turn_credentials.c
if(TARGET OpenSSL::Crypto)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE OpenSSL::Crypto)
endif()

# Link miniupnpc for UPnP/NAT-PMP port mapping (optional)
# This is linked to ascii-chat-network module, but must also be exposed via the
# INTERFACE library for final executables since libasciichat.a includes upnp.c
if(MINIUPNPC_FOUND)
    target_link_libraries(ascii-chat-static-lib-deps INTERFACE ${MINIUPNPC_LIBRARIES})
    # Also link libnatpmp on macOS (separate library for NAT-PMP protocol)
    if(NATPMP_LIBRARY)
        target_link_libraries(ascii-chat-static-lib-deps INTERFACE ${NATPMP_LIBRARY})
    endif()
endif()

# Wrapper: links both the archive and its dependencies
add_library(ascii-chat-static-lib-with-deps INTERFACE)
target_link_libraries(ascii-chat-static-lib-with-deps INTERFACE
    ascii-chat-static-lib
    ascii-chat-static-lib-deps
)

# =============================================================================
# Symbol Validation for Combined Static Library
# =============================================================================
# Validates that key ascii-chat symbols exist in the combined library
# Uses ASCIICHAT_SYMBOLS_CSV defined earlier (shared between static and shared lib validation)
if(ASCIICHAT_LLVM_NM_EXECUTABLE)
    # Add validation step after library is created
    add_custom_command(
        OUTPUT ${STATIC_LIB_OUTPUT}.validated
        COMMAND ${CMAKE_COMMAND}
            -DLLVM_NM=${ASCIICHAT_LLVM_NM_EXECUTABLE}
            -DLIBRARY=${STATIC_LIB_OUTPUT}
            -DSYMBOLS=${ASCIICHAT_SYMBOLS_CSV}
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateSymbols.cmake
        COMMAND ${CMAKE_COMMAND} -E touch ${STATIC_LIB_OUTPUT}.validated
        DEPENDS ${STATIC_LIB_OUTPUT}
        COMMENT "Validating ascii-chat symbols in libasciichat.a"
        VERBATIM
    )

    # User-friendly 'static-lib' target - builds AND validates the static library
    add_custom_target(static-lib
        COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
        COMMAND_ECHO NONE
        DEPENDS ${STATIC_LIB_OUTPUT}.validated build-timer-start
        VERBATIM
    )
else()
    message(STATUS "Symbol validation: ${BoldYellow}disabled${ColorReset} (llvm-nm not found)")
    # User-friendly 'static-lib' target - builds the static library (no validation)
    add_custom_target(static-lib
        COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
        COMMAND_ECHO NONE
        DEPENDS ${STATIC_LIB_OUTPUT} build-timer-start
        VERBATIM
    )
endif()

# Build target for Release builds (referenced by Executables.cmake)
# Note: Now handled by build-ascii-chat-static-lib custom target (created above)

# =============================================================================
# Unified Library (ascii-chat-static for backward compatibility)
# =============================================================================
# Debug/Dev: Shared library (DLL on Windows) for faster linking during development
# Release: Static library for distribution
# USE_MUSL: Always static (musl requires static linking)
if((CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev") AND NOT USE_MUSL)
    # Use the existing ascii-chat-shared target but make it build by default
    # Remove EXCLUDE_FROM_ALL for Debug/Dev builds
    set_target_properties(ascii-chat-shared PROPERTIES EXCLUDE_FROM_ALL FALSE)

    # Create interface library that wraps the shared library
    # Link directly to the target (CMake handles the import library on Windows)
    add_library(ascii-chat-static INTERFACE)
    target_link_libraries(ascii-chat-static INTERFACE ascii-chat-shared)

    # Build target name for consistency
    set(ASCII_CHAT_UNIFIED_BUILD_TARGET ascii-chat-shared)
else()
    # Release builds or USE_MUSL: ascii-chat-static wraps the combined static library
    add_library(ascii-chat-static INTERFACE)
    target_link_libraries(ascii-chat-static INTERFACE ascii-chat-static-lib-with-deps)

    # Build target name for consistency
    set(ASCII_CHAT_UNIFIED_BUILD_TARGET build-ascii-chat-static-lib)
endif()

# Note: External dependencies (mimalloc, libsodium, portaudio, etc.) are linked
# to the individual module libraries. When the executable links against
# ascii-chat-static, it will get all the object files from the combined library,
# but external dependencies still need to be linked to the executable
# (this is handled in Executables.cmake).

# =============================================================================
# Library Alias for Tests
# =============================================================================
# Create an alias that links all modules for test compatibility
# This allows tests to link against ascii-chat-lib instead of individual modules
# Note: Libraries with circular dependencies must be listed in correct order:
# - core needs symbols from network (buffer_pool) and crypto (known_hosts)
# - network needs symbols from core
# Solution: List consumers before providers, with circulars listed twice
add_library(ascii-chat-lib INTERFACE)
target_link_libraries(ascii-chat-lib INTERFACE
    ascii-chat-session
    ascii-chat-simd
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-core
    ascii-chat-network
    ascii-chat-crypto
    ascii-chat-network
    ascii-chat-core
    ascii-chat-platform
    ascii-chat-data-structures
    ascii-chat-util
)

# =============================================================================
# Static Library Archive Creation (custom command handles basename collisions)
# =============================================================================
# See cmake/scripts/create_static_archive.cmake for implementation details

