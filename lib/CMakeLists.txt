# AppKit supplies native desktop clipboard access; it is unavailable on iOS.
if(APPLE AND NOT PLATFORM_IOS)
    set(ASCIICHAT_CLIPBOARD_FRAMEWORK "-framework AppKit")
endif()

# lib/ - Core application infrastructure
# Sources from: log/, logging/, options/, discovery/, debug/, ui/

# Store lib directory since this file may be included() from parent scope
set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Libplatform.cmake)
set(CORE_SRCS
    ${LIB_DIR}/event.c
    ${LIB_DIR}/util/utf8.c
    ${LIB_DIR}/media/input.c
    ${LIB_DIR}/media/playlist.c
    ${LIB_DIR}/common.c
    ${LIB_DIR}/otel.c
    ${LIB_DIR}/asciichat_errno.c
    ${CMAKE_SOURCE_DIR}/src/common/http_renderer.c
    ${LIB_DIR}/app_callbacks.c
    ${LIB_DIR}/stats/stats.c
    ${LIB_DIR}/stats/descriptors.c
    ${LIB_DIR}/stats/runtime.c
    # log/
    ${LIB_DIR}/log/log.c
    ${LIB_DIR}/log/format.c
    ${LIB_DIR}/log/named.c
    ${LIB_DIR}/log/colorize.c
    ${LIB_DIR}/log/mmap.c
    ${LIB_DIR}/log/grep.c
    ${LIB_DIR}/log/search.c
    ${LIB_DIR}/log/json.c
    ${LIB_DIR}/log/websocket.c
    ${LIB_DIR}/log/io.c
    ${LIB_DIR}/log/terminal.c
    ${LIB_DIR}/log/asan.c
    # ui/
    ${LIB_DIR}/tui/session.c
    ${LIB_DIR}/tui/session_status.c
    ${LIB_DIR}/tui/session_input.c
    ${LIB_DIR}/tui/session_help.c
    ${LIB_DIR}/tui/session_splash.c
    ${LIB_DIR}/tui/controller.c
    ${LIB_DIR}/tui/render_progress.c
    ${LIB_DIR}/tui/input.c
    ${LIB_DIR}/tui/sync.c
    ${LIB_DIR}/tui/prompt.c
    ${LIB_DIR}/tui/error_popup.c
    ${LIB_DIR}/tui/notice.c
    ${LIB_DIR}/tui/too_small.c
    ${LIB_DIR}/ui/frame_buffer.c
    ${LIB_DIR}/ui/popup.c
    ${LIB_DIR}/ui/fps_counter.c
    ${LIB_DIR}/tui/terminal_screen.c
    ${LIB_DIR}/tui/keyboard_help.c
    ${LIB_DIR}/tui/splash.c
    ${LIB_DIR}/tui/invitation.c
    ${LIB_DIR}/tui/status.c
    ${LIB_DIR}/tui/update_banner.c
    ${LIB_DIR}/tui/mdns.c
    ${LIB_DIR}/ui/input.c
    ${LIB_DIR}/ui/output.c
    ${LIB_DIR}/ui/layout.c
    ${LIB_DIR}/ui/progress.c
    ${LIB_DIR}/ui/text.c
    ${LIB_DIR}/ui/viewport.c
    ${LIB_DIR}/ui/scrollbar.c
    ${LIB_DIR}/ui/table.c
    ${LIB_DIR}/ui/help_menu.c
    ${LIB_DIR}/ui/buttons.c
    ${LIB_DIR}/ui/selectable_list.c
    ${LIB_DIR}/tui/stats.c
    ${LIB_DIR}/tui/shortcuts.c
    ${LIB_DIR}/tui/errno_stacks.c
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
    ${LIB_DIR}/options/config.c
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
    # version
    ${LIB_DIR}/options/manpage/embedded.c
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
    ${LIB_DIR}/discovery/key_set.c
    ${LIB_DIR}/discovery/identity.c
    ${LIB_DIR}/discovery/strings.c
    ${LIB_DIR}/discovery/adjectives.c
    ${LIB_DIR}/discovery/nouns.c
    # src/discovery/ (client code that's part of lib)
    ${CMAKE_SOURCE_DIR}/src/discovery/nat.c
    ${CMAKE_SOURCE_DIR}/src/discovery/negotiate.c
    ${CMAKE_SOURCE_DIR}/src/discovery/session.c
    ${CMAKE_SOURCE_DIR}/src/discovery/webrtc.c
    # src/common/session/ (executable code, refactored from lib/session)
    ${CMAKE_SOURCE_DIR}/src/common/session/protocol.c
    ${CMAKE_SOURCE_DIR}/src/common/session/frame_validation.c
    ${CMAKE_SOURCE_DIR}/src/common/session/connection.c
    ${CMAKE_SOURCE_DIR}/src/common/session/establish.c
    ${CMAKE_SOURCE_DIR}/src/common/session/client_like.c
    ${CMAKE_SOURCE_DIR}/src/common/session/consensus.c
    ${CMAKE_SOURCE_DIR}/src/common/session/consensus_integration.c
    ${CMAKE_SOURCE_DIR}/src/common/session/capture.c
    ${CMAKE_SOURCE_DIR}/src/common/session/display.c
    ${CMAKE_SOURCE_DIR}/src/common/session/pipeline.c
    ${CMAKE_SOURCE_DIR}/src/common/session/render.c
    ${CMAKE_SOURCE_DIR}/src/common/session/settings.c
    ${CMAKE_SOURCE_DIR}/src/common/session/audio.c
    ${CMAKE_SOURCE_DIR}/src/common/session/av_state.c
    ${CMAKE_SOURCE_DIR}/src/common/session/participant.c
    ${CMAKE_SOURCE_DIR}/src/common/session/host.c
    ${CMAKE_SOURCE_DIR}/src/common/session/keyboard_handler.c
    ${CMAKE_SOURCE_DIR}/src/common/session/session_log_buffer.c
    ${CMAKE_SOURCE_DIR}/src/common/session/server_like.c
    ${CMAKE_SOURCE_DIR}/src/common/session/quic_listeners.c
    # Terminal FD reader/writer
    ${CMAKE_SOURCE_DIR}/lib/terminal/fd/reader.c
    ${CMAKE_SOURCE_DIR}/lib/terminal/fd/writer.c
    # Third-party: tomlc17
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17/src/tomlc17.c
)

if(WIN32)
    # The upstream TOML parser uses portable C stdio rather than MSVC's secure CRT extensions.
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17/src/tomlc17.c"
        PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS
    )
endif()

# Named registry, backtrace, and sync are provided by libplatform.
list(APPEND CORE_SRCS
    ${LIB_DIR}/debug/stats.c
    ${LIB_DIR}/debug/errno.c
        ${CMAKE_SOURCE_DIR}/lib/options/registry/debug.c
)

# Additional debug sources only in non-Release builds
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND CORE_SRCS
    )
endif()

# WASM logging only for Emscripten builds
if(EMSCRIPTEN)
    list(APPEND CORE_SRCS
        ${LIB_DIR}/log/wasm.c
    )
endif()

# Apple unified logging (os_log) for macOS/iOS
if(PLATFORM_DARWIN)
    list(APPEND CORE_SRCS
        ${LIB_DIR}/log/apple.c
    )
endif()

# Include subdirectories that define module source variables
add_subdirectory(${LIB_DIR}/util ${CMAKE_BINARY_DIR}/lib/util)
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

# Check for warning flags that may not exist in older Clang versions
# -Wno-unterminated-string-initialization was added in Clang 20
check_c_compiler_flag("-Wno-unterminated-string-initialization" HAVE_WNO_UNTERMINATED_STRING_INIT)

# Always use OBJECT libraries (all build types)
# OBJECT libraries eliminate duplicate compilation: modules compile once,
# shared library and tests both link to the same object files
# This simplifies the build configuration and ensures consistent behavior
set(BUILDING_OBJECT_LIBS TRUE)

# In CMakeLists.txt - for macOS
if(APPLE AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    # Use standard install paths, let Homebrew handle relocation
    # Skip for Release builds to avoid embedding developer paths in the rpath
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

# Helper macro to create a module with common settings
macro(create_ascii_chat_module MODULE_NAME MODULE_SRCS)
    # Always use OBJECT libraries to eliminate duplication
    # (shared library and tests both link to the same object files, compiled once)
    add_library(${MODULE_NAME} OBJECT ${MODULE_SRCS})

    if(WIN32 AND ASCIICHAT_ENABLE_UNITY_BUILDS)
        set_target_properties(${MODULE_NAME} PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 4)
        # Keep vendored code separate so export filtering retains its source paths.
        foreach(_module_source IN ITEMS ${MODULE_SRCS})
            if(_module_source MATCHES "[/\\\\]deps[/\\\\]")
                set_source_files_properties(${_module_source} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
            endif()
        endforeach()
    endif()

    # Musl builds expose libsodium headers through the generated dependency
    # prefix. Every module includes common headers, which in turn include
    # sodium.h, so the external project must finish installing its headers
    # before any object-library source is compiled.
    if(USE_MUSL AND DEFINED LIBSODIUM_BUILD_TARGET)
        add_dependencies(${MODULE_NAME} ${LIBSODIUM_BUILD_TARGET})
    endif()

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

    # Unity compilation mixes C and C++ sources in the audio module.  That
    # defeats musl's isolated C++ standard-library/header setup and can pull
    # host glibc headers into the translation unit, so keep musl builds
    # source-isolated.
    if(ASCIICHAT_ENABLE_UNITY_BUILDS AND NOT USE_MUSL)
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

    # Version dependency
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

    if(USE_MIMALLOC AND TARGET asciichat-mimalloc)
        target_link_libraries(${MODULE_NAME} PRIVATE asciichat-mimalloc)
    endif()

    # Musl flag
    if(USE_MUSL)
        target_compile_definitions(${MODULE_NAME} PRIVATE USE_MUSL=1)
    endif()

    # Apply sanitizers to module
    apply_sanitizers_to_target(${MODULE_NAME})
endmacro()

# -----------------------------------------------------------------------------
# Module 1: Utilities (no dependencies)
# -----------------------------------------------------------------------------
list(FILTER UTIL_SRCS EXCLUDE REGEX "/utf8proc/utf8proc[.]c$")
create_ascii_chat_module(ascii-chat-util "${UTIL_SRCS}")
target_link_libraries(ascii-chat-util PRIVATE asciichat-utf8proc asciichat-icu asciichat-libflags)

# PCRE2: Define PCRE2_CODE_UNIT_WIDTH=8 for 8-bit API (UTF-8 support)
# CRITICAL: Must be defined before including pcre2.h in any source file
# PUBLIC because include/ascii-chat/util/pcre2.h includes pcre2.h
target_compile_definitions(ascii-chat-util PUBLIC PCRE2_CODE_UNIT_WIDTH=8)

# Add PCRE2 include directory (must be before linking)
# PUBLIC because pcre2.h is included from public header include/ascii-chat/util/pcre2.h
target_link_libraries(ascii-chat-util PUBLIC asciichat-pcre2)

# -----------------------------------------------------------------------------
# Module 2: Data Structures (depends on: util)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-data-structures "${DATA_STRUCTURES_SRCS}")
target_link_libraries(ascii-chat-data-structures PRIVATE ascii-chat-util)

# -----------------------------------------------------------------------------
# Module 3: Platform Abstraction (depends on: util, data-structures)
# -----------------------------------------------------------------------------

# Runtime is created before the dependency interface below because the
# platform dependency bundle is now attached directly to the runtime module.
set(_core_runtime_regex "[/\\\\]lib[/\\\\](event|common|otel|asciichat_errno|app_callbacks|version)[.]c$|[/\\\\]lib[/\\\\]util[/\\\\]utf8[.]c$|[/\\\\]src[/\\\\]common[/\\\\]http_renderer[.]c$|[/\\\\]lib[/\\\\]stats[/\\\\].*[.]c$|[/\\\\]lib[/\\\\]media[/\\\\](input|playlist)[.]c$|[/\\\\]lib[/\\\\]debug[/\\\\](stats|errno)[.]c$")
set(_runtime_sources "${CORE_SRCS}")
list(FILTER _runtime_sources INCLUDE REGEX "${_core_runtime_regex}")
list(APPEND _runtime_sources ${LIB_DIR}/video/ascii/palette.c)
create_ascii_chat_module(ascii-chat-runtime "${_runtime_sources}")

target_link_libraries(ascii-chat-runtime INTERFACE ${ASCIICHAT_LIBPLATFORM_TARGET})
if(NOT EMSCRIPTEN)
    target_link_libraries(ascii-chat-runtime INTERFACE asciichat-libuv)
endif()
target_link_libraries(ascii-chat-runtime INTERFACE
    ascii-chat-util
    ascii-chat-data-structures
)

# Link render-file dependencies to platform module (for terminal renderers)
target_link_libraries(ascii-chat-runtime INTERFACE asciichat-platform asciichat-utf8proc)

# Add kernel headers for musl builds (needed for V4L2)
if(USE_MUSL AND DEFINED MUSL_DEPS_DIR AND EXISTS "${MUSL_DEPS_DIR}/musl-deps/kernel-headers")
    target_include_directories(ascii-chat-runtime INTERFACE
        "${MUSL_DEPS_DIR}/musl-deps/kernel-headers"
    )
endif()

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(ascii-chat-runtime INTERFACE
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
        psapi    # For EnumProcessModules (backtrace symbol resolution)
    )
else()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-runtime INTERFACE
            "-framework CoreFoundation"
            ${ASCIICHAT_CLIPBOARD_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
            ${CORESERVICES_FRAMEWORK}
            ${IOKIT_FRAMEWORK}  # Required for IOPMLib.h (keepawake functionality)
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-runtime INTERFACE ${CMAKE_THREAD_LIBS_INIT})
        target_link_libraries(ascii-chat-runtime INTERFACE asciichat-systemd)
        target_compile_definitions(ascii-chat-runtime INTERFACE HAVE_LIBSYSTEMD=1)
    endif()
endif()

# -----------------------------------------------------------------------------
# Module 3: Cryptography (depends on: util, platform)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-crypto "${CRYPTO_SRCS}")
# For OBJECT libs, link external deps only (no module deps)
target_link_libraries(ascii-chat-crypto PRIVATE asciichat-libsodium)

# PCRE2: Configure for crypto module (for SSH parser regex)
# Define PCRE2_CODE_UNIT_WIDTH=8 for 8-bit API (UTF-8 support)
target_compile_definitions(ascii-chat-crypto PRIVATE PCRE2_CODE_UNIT_WIDTH=8)

# Add PCRE2 include directory
target_link_libraries(ascii-chat-crypto PRIVATE asciichat-pcre2)

# Add BearSSL (required crypto library)
target_link_libraries(ascii-chat-crypto PRIVATE asciichat-bearssl)

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
    PROPERTIES COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src -I${CMAKE_SOURCE_DIR}/include/compat"
)

# -----------------------------------------------------------------------------
# Module 4: SIMD (depends on: util, core, video)
# Note: Circular dependency with video (simd needs video for benchmark code,
#       video needs simd for SIMD functions). This is resolved at link time
#       since both libraries are linked together in executables.
# -----------------------------------------------------------------------------
# Keep per-pixel rendering and frame validation responsive in Debug builds.
# Debug information and sanitizer instrumentation remain enabled.
set_property(SOURCE
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/render/common.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/render/luminance.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/render/glyph.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/stabilize.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/output_buffer.c"
    "${CMAKE_SOURCE_DIR}/src/common/session/frame_validation.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/ascii.c"
    "${CMAKE_SOURCE_DIR}/lib/video/rgba/image.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/simd/cells.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/sse2/luminance.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/sse2/color.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/ssse3/color.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/avx2/luminance.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/avx2/color.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/neon/luminance.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/neon/color.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/sve/luminance.c"
    "${CMAKE_SOURCE_DIR}/lib/video/ascii/sve/color.c"
    APPEND PROPERTY COMPILE_OPTIONS
    "$<$<AND:$<CONFIG:Debug>,$<NOT:$<C_COMPILER_ID:MSVC>>>:-O2;-finline>"
)
# The core module's Debug precompiled header uses -O0 and cannot be reused by
# this optimized source with Clang.
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/common/session/frame_validation.c"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
create_ascii_chat_module(ascii-chat-simd "${SIMD_SRCS}")

# Note: SIMD and Video have circular dependency (simd benchmarks use video, video uses simd).
# This is resolved at link time when both libraries are linked together in executables.
# We don't link module dependencies here due to OBJECT library pattern.

# -----------------------------------------------------------------------------
# Module 5: Video Processing (depends on: util, platform, core, simd)
# -----------------------------------------------------------------------------
# FFmpeg headers are configured globally so library and application targets use
# the same version as the linked libraries.

create_ascii_chat_module(ascii-chat-video "${VIDEO_SRCS}")
# The defer transformer parses video sources from the compilation database
# before external FFmpeg headers are installed; keep this target's include
# search path explicit instead of relying only on the project interface target.
target_link_libraries(ascii-chat-video PRIVATE asciichat-ffmpeg)
if(USE_MUSL)
    set(_musl_ffmpeg_include_dir "${ASCIICHAT_DEPS_CACHE_DIR}/build/installed/ffmpeg/include")
    target_include_directories(ascii-chat-video BEFORE PRIVATE "${_musl_ffmpeg_include_dir}")
    # Defer copies source properties to transformed sources; target-only include
    # directories are not present when defer parses the original compilation DB.
    set_source_files_properties(${VIDEO_SRCS} PROPERTIES
        INCLUDE_DIRECTORIES "${_musl_ffmpeg_include_dir}")
endif()
include(${CMAKE_SOURCE_DIR}/cmake/deps/FaceDetection.cmake)
ascii_chat_add_face_detector(ascii-chat-video)

# Link external dependencies (FFmpeg, freetype, fontconfig)
# Module dependencies (util, platform, core, simd) are resolved through OBJECT library merging
target_link_libraries(ascii-chat-video PRIVATE asciichat-fftw-float)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # V4L2 capture uses swscale and needs generated FFmpeg headers before compiling.
    target_link_libraries(ascii-chat-runtime INTERFACE asciichat-ffmpeg)
endif()
if(VTERM_MUSL_TARGET)
    add_dependencies(ascii-chat-video ${VTERM_MUSL_TARGET})
endif()

# libvterm for terminal rendering in render-file
target_link_libraries(ascii-chat-video PRIVATE asciichat-vterm)

# FreeType2 for font rendering in render-file
target_link_libraries(ascii-chat-video PRIVATE asciichat-freetype)

# Fontconfig for font selection in render-file
if(TARGET asciichat-fontconfig)
    target_link_libraries(ascii-chat-video PRIVATE asciichat-fontconfig)
endif()

# Mark generated font files as generated (for OBJECT libraries, must be before module creation)
set_source_files_properties(
    "${CMAKE_BINARY_DIR}/generated/data/fonts/matrix_resurrected.c"
    "${CMAKE_BINARY_DIR}/generated/data/fonts/default.c"
    PROPERTIES LANGUAGE C GENERATED TRUE
)

# Ensure font files are generated before building (required for --clean-first to work)
add_dependencies(ascii-chat-video generate_fonts)

# PCRE2: Configure for media module (for YouTube video ID extraction)
# Define PCRE2_CODE_UNIT_WIDTH=8 for 8-bit API (UTF-8 support)
target_compile_definitions(ascii-chat-video PRIVATE PCRE2_CODE_UNIT_WIDTH=8)

# Add PCRE2 include directory
target_link_libraries(ascii-chat-video PRIVATE asciichat-pcre2)

# -----------------------------------------------------------------------------
# Module 6: Audio Processing (depends on: util, platform, data-structures)
# Add WebRTC AEC3 include directories BEFORE creating the module
if(TARGET asciichat-webrtc-aec3)
    get_target_property(WEBRTC_INCLUDES asciichat-webrtc-aec3 INTERFACE_INCLUDE_DIRECTORIES)
    if(WEBRTC_INCLUDES)
        # Set globally so they apply during compilation
        include_directories(${WEBRTC_INCLUDES})
    endif()
endif()

# WebRTC AEC3's Windows time source uses timeGetTime from winmm.  Keep this
# system-library requirement attached to the external WebRTC base target so
# every consumer, including its DLL, receives the import library.
if(WIN32 AND TARGET base)
    target_link_libraries(base PUBLIC winmm)
endif()

# Now create the module with WebRTC includes in scope
create_ascii_chat_module(ascii-chat-audio "${AUDIO_SRCS}")
target_link_libraries(ascii-chat-audio PRIVATE asciichat-rnnoise)
add_dependencies(ascii-chat-audio asciichat-rnnoise-build)

# For OBJECT libs, link external deps only
target_link_libraries(ascii-chat-audio PRIVATE
    asciichat-portaudio asciichat-opus asciichat-webrtc-aec3)

# Link platform-specific audio libraries
# macOS: CoreAudio, AudioUnit, AudioToolbox (required for static portaudio)
# Linux: ALSA + JACK (vcpkg's PortAudio includes JACK support, so we must link it)
# Windows: WASAPI/DirectSound (handled by platform module)
# Note: vcpkg PortAudio builds include JACK support, so we link libjack if available
if(APPLE)
    target_link_libraries(ascii-chat-audio PRIVATE
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${CORESERVICES_FRAMEWORK}
    )
elseif(PLATFORM_LINUX AND JACK_LIB AND NOT USE_MUSL)
    # Link JACK on Linux if available (required by vcpkg's PortAudio build)
    # Skip for musl: musl PortAudio is built with ALSA only, system libjack.so is glibc-linked
    target_link_libraries(ascii-chat-audio PRIVATE ${JACK_LIB})
endif()

# Add build dependency for PortAudio from-source builds (non-musl Linux/macOS)
if(DEFINED PORTAUDIO_BUILD_TARGET AND TARGET ${PORTAUDIO_BUILD_TARGET})
    add_dependencies(ascii-chat-audio ${PORTAUDIO_BUILD_TARGET})
endif()

# -----------------------------------------------------------------------------
# Application infrastructure modules
#
# CORE_SRCS used to be compiled as one catch-all OBJECT library.  Keep the
# source inventory in one place for now, but partition it by ownership so that
# changes to options, UI, sessions, or discovery no longer invalidate all of
# application infrastructure.
# -----------------------------------------------------------------------------
set(_CORE_MODULES runtime logging options terminal ui session discovery)
set(_CORE_MODULE_TARGETS)

set(_core_runtime_regex "[/\\\\]lib[/\\\\](event|common|otel|asciichat_errno|app_callbacks|version)[.]c$|[/\\\\]lib[/\\\\]util[/\\\\]utf8[.]c$|[/\\\\]src[/\\\\]common[/\\\\]http_renderer[.]c$|[/\\\\]lib[/\\\\]stats[/\\\\].*[.]c$|[/\\\\]lib[/\\\\]media[/\\\\](input|playlist)[.]c$|[/\\\\]lib[/\\\\]debug[/\\\\](stats|errno)[.]c$")
set(_core_logging_regex "[/\\\\]log[/\\\\].*[.]c$")
set(_core_options_regex "[/\\\\]options[/\\\\].*[.]c$")
set(_core_terminal_regex "[/\\\\]terminal[/\\\\].*[.]c$|tomlc17[/\\\\]src[/\\\\]tomlc17[.]c$")
set(_core_ui_regex "[/\\\\](tui|ui)[/\\\\].*[.]c$")
set(_core_session_regex "[/\\\\]src[/\\\\]common[/\\\\]session[/\\\\].*[.]c$")
set(_core_discovery_regex "[/\\\\](lib[/\\\\]discovery|src[/\\\\]discovery)[/\\\\].*[.]c$")

foreach(_core_module IN LISTS _CORE_MODULES)
    set(_module_sources "${CORE_SRCS}")
    list(FILTER _module_sources INCLUDE REGEX "${_core_${_core_module}_regex}")
    set(_module_target "ascii-chat-${_core_module}")
    if(NOT TARGET ${_module_target})
        create_ascii_chat_module(${_module_target} "${_module_sources}")
    endif()
    list(APPEND _CORE_MODULE_TARGETS ${_module_target})
endforeach()

# Shared include/dependency requirements for the formerly monolithic sources.
foreach(_core_target IN LISTS _CORE_MODULE_TARGETS)
    target_include_directories(${_core_target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/lib
    )
    target_compile_definitions(${_core_target} PRIVATE PCRE2_CODE_UNIT_WIDTH=8)
    target_link_libraries(${_core_target} PRIVATE
        asciichat-sqlite3
        asciichat-utf8proc
        asciichat-yyjson
        asciichat-pcre2
        asciichat-libflags
        asciichat-tomlc17
    )
    if(NOT WIN32)
        target_link_libraries(${_core_target} PRIVATE m)
    endif()
endforeach()

# The UI partition contains lib/tui/update_banner.c, which includes <uv.h>
# directly. Apply the selected libuv target's include paths while compiling it.
if(NOT EMSCRIPTEN)
    target_link_libraries(ascii-chat-ui PRIVATE asciichat-libuv)
endif()

target_link_libraries(ascii-chat-logging PRIVATE
    OpenSSL::SSL
    asciichat-libwebsockets)

if(LIBWEBSOCKETS_BUILD_TARGET)
    add_dependencies(ascii-chat-discovery ${LIBWEBSOCKETS_BUILD_TARGET})
endif()
if(USE_MUSL)
    add_dependencies(ascii-chat-runtime asciichat-libexecinfo-musl)
endif()

# yyjson - Fast JSON library for structured logging and JSON parsing

# Add yyjson include directory
target_link_libraries(ascii-chat-video PRIVATE asciichat-yyjson)

# Link yyjson library
if(USE_MUSL)
    if(DEFINED YYJSON_BUILD_TARGET)
        add_dependencies(ascii-chat-runtime ${YYJSON_BUILD_TARGET})
        add_dependencies(ascii-chat-video ${YYJSON_BUILD_TARGET})
    endif()
endif()

# Link yyjson (required)
target_link_libraries(ascii-chat-logging PRIVATE asciichat-yyjson)

# -----------------------------------------------------------------------------
# Module 9: Panic Instrumentation Runtime (depends on infrastructure modules)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-panic "${TOOLING_PANIC_SRCS}")
target_include_directories(ascii-chat-panic PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-panic PRIVATE
        PRIVATE
            ascii-chat-util
            ascii-chat-runtime
            ascii-chat-runtime
            ascii-chat-logging
            ascii-chat-options
            ascii-chat-terminal
            ascii-chat-ui
            ascii-chat-session
            ascii-chat-discovery
    )
endif()
if(NOT WIN32 AND TARGET Threads::Threads)
    target_link_libraries(ascii-chat-panic PUBLIC Threads::Threads)
endif()

# -----------------------------------------------------------------------------
# Module 10: Network (depends on: util, platform, crypto)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-network "${NETWORK_SRCS}")
target_link_libraries(ascii-chat-network PRIVATE asciichat-libuv)
target_link_libraries(ascii-chat-network PRIVATE asciichat-mdns)
if(TARGET asciichat-miniupnpc)
    target_include_directories(ascii-chat-network PRIVATE
        "$<TARGET_PROPERTY:asciichat-miniupnpc,INTERFACE_INCLUDE_DIRECTORIES>")
    if(EXISTS "/usr/include/miniupnpc/miniupnpc.h")
        target_include_directories(ascii-chat-network PRIVATE "/usr/include/miniupnpc")
    endif()
endif()
# Prefer the package target so manifest/vcpkg builds carry the correct
# include and transitive-link settings on every platform.  Keep the direct
# search as a fallback for distro/Homebrew installs that do not ship a CMake
# package file.
find_package(nghttp2 CONFIG QUIET)
if(TARGET nghttp2_static)
    set(ASCIICHAT_NGHTTP2_TARGET nghttp2_static)
    get_target_property(_asciichat_nghttp2_includes nghttp2_static INTERFACE_INCLUDE_DIRECTORIES)
    if(_asciichat_nghttp2_includes)
        target_include_directories(ascii-chat-network PRIVATE ${_asciichat_nghttp2_includes})
    endif()
elseif(TARGET nghttp2)
    set(ASCIICHAT_NGHTTP2_TARGET nghttp2)
elseif(USE_MUSL)
    # gotel builds this target for the active toolchain; a host installation
    # can otherwise introduce glibc headers and libraries into a musl build.
    if(NOT TARGET nghttp2_static)
        message(FATAL_ERROR "musl builds require the toolchain-built nghttp2_static target")
    endif()
    set(ASCIICHAT_NGHTTP2_TARGET nghttp2_static)
    get_target_property(_asciichat_nghttp2_includes nghttp2_static INTERFACE_INCLUDE_DIRECTORIES)
    if(_asciichat_nghttp2_includes)
        target_include_directories(ascii-chat-network PRIVATE ${_asciichat_nghttp2_includes})
    endif()
elseif(TARGET nghttp2::nghttp2)
    set(ASCIICHAT_NGHTTP2_TARGET nghttp2::nghttp2)
elseif(TARGET nghttp2::nghttp2_static)
    set(ASCIICHAT_NGHTTP2_TARGET nghttp2::nghttp2_static)
else()
    find_path(ASCIICHAT_NGHTTP2_INCLUDE_DIR nghttp2/nghttp2.h NO_CMAKE_FIND_ROOT_PATH)
    find_library(ASCIICHAT_NGHTTP2_LIBRARY NAMES nghttp2 nghttp2_static NO_CMAKE_FIND_ROOT_PATH)
    if(UNIX AND (NOT ASCIICHAT_NGHTTP2_INCLUDE_DIR OR NOT ASCIICHAT_NGHTTP2_LIBRARY))
        # vcpkg's toolchain can disable the default system prefixes even when
        # the distro package is installed.  Retry the canonical Unix prefixes.
        find_path(ASCIICHAT_NGHTTP2_SYSTEM_INCLUDE_DIR nghttp2/nghttp2.h
            PATHS /usr/include /usr/local/include
            NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        find_library(ASCIICHAT_NGHTTP2_SYSTEM_LIBRARY NAMES nghttp2 nghttp2_static
            PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu
                /usr/lib/aarch64-linux-gnu /usr/lib/arm-linux-gnueabihf
                /usr/local/lib
            NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        if(NOT ASCIICHAT_NGHTTP2_INCLUDE_DIR)
            set(ASCIICHAT_NGHTTP2_INCLUDE_DIR ${ASCIICHAT_NGHTTP2_SYSTEM_INCLUDE_DIR})
        endif()
        if(NOT ASCIICHAT_NGHTTP2_LIBRARY)
            set(ASCIICHAT_NGHTTP2_LIBRARY ${ASCIICHAT_NGHTTP2_SYSTEM_LIBRARY})
        endif()
    endif()
    if(NOT ASCIICHAT_NGHTTP2_INCLUDE_DIR OR NOT ASCIICHAT_NGHTTP2_LIBRARY)
        message(FATAL_ERROR "nghttp2 development files are required for the shared HTTP/2 server")
    endif()
    set(ASCIICHAT_NGHTTP2_TARGET ${ASCIICHAT_NGHTTP2_LIBRARY})
    target_include_directories(ascii-chat-network PRIVATE ${ASCIICHAT_NGHTTP2_INCLUDE_DIR})
endif()
# nghttp2 is already propagated by the gotel transport dependency.  Adding
# the separately discovered package here creates two incompatible Windows
# libraries on the final link line.
if(TARGET asciichat-libuv)
    target_link_libraries(ascii-chat-network PRIVATE asciichat-libuv)
endif()
if(NOT EMSCRIPTEN)
    target_link_libraries(ascii-chat-network PRIVATE asciichat-quic)
endif()
# The HTTP/2 implementation is compiled into this module, so its discovered
# nghttp2 target must be part of the module's link interface.  In particular,
# this keeps system nghttp2 available when gotel is configured without its
# optional HTTP/2 transport.
target_link_libraries(ascii-chat-network PRIVATE ${ASCIICHAT_NGHTTP2_TARGET})
# clang-cl disables C++ exceptions by default, but this adapter catches
# exceptions from the libdatachannel C++ API.
if(WIN32)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(ascii-chat-network PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")
    else()
        target_compile_options(ascii-chat-network PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
    endif()
endif()
# Exclude main() function from mdns library
target_compile_definitions(ascii-chat-network PRIVATE MDNS_NO_MAIN)
target_link_libraries(ascii-chat-network PRIVATE mdns::mdns)
# yyjson for /health JSON endpoint
target_link_libraries(ascii-chat-network PRIVATE asciichat-yyjson)

# PCRE2: Add include directory and define PCRE2_CODE_UNIT_WIDTH=8
# Network module uses PCRE2 in sdp.c and ice.c
target_link_libraries(ascii-chat-network PRIVATE asciichat-pcre2 asciichat-libwebsockets)
if(LIBWEBSOCKETS_BUILD_TARGET)
    add_dependencies(ascii-chat-network ${LIBWEBSOCKETS_BUILD_TARGET})
endif()
target_compile_definitions(ascii-chat-network PRIVATE PCRE2_CODE_UNIT_WIDTH=8)
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-network PRIVATE
        ascii-chat-util
        ascii-chat-runtime
        ascii-chat-crypto
        ascii-chat-runtime
        ascii-chat-logging
        ascii-chat-options
        ascii-chat-terminal
        ascii-chat-ui
        ascii-chat-session
        ascii-chat-discovery
        asciichat-pcre2
        asciichat-zstd
        asciichat-sqlite3  # Rate limiting SQLite backend
        asciichat-libdatachannel  # WebRTC DataChannels for P2P transport
        OpenSSL::Crypto  # Required by libdatachannel for TLS/DTLS
    )
    # Link libwebsockets
    # miniupnpc is a required native dependency.
    target_link_libraries(ascii-chat-network PUBLIC asciichat-miniupnpc)
        # Also link libnatpmp on macOS
        if(NATPMP_LIBRARY)
            target_include_directories(ascii-chat-network PRIVATE ${NATPMP_INCLUDE_DIR})
            target_link_libraries(ascii-chat-network PRIVATE ${NATPMP_LIBRARY})
        endif()
else()
    # For OBJECT libs, link external deps only (OpenSSL needed by libdatachannel for TLS/DTLS)
    target_link_libraries(ascii-chat-network PUBLIC asciichat-pcre2 asciichat-zstd asciichat-sqlite3 asciichat-libdatachannel asciichat-miniupnpc OpenSSL::Crypto)
    # Link libwebsockets
    target_link_libraries(ascii-chat-network PRIVATE asciichat-libwebsockets asciichat-miniupnpc)
        # Also link libnatpmp on macOS
        if(NATPMP_LIBRARY)
            target_include_directories(ascii-chat-network PRIVATE ${NATPMP_INCLUDE_DIR})
            target_link_libraries(ascii-chat-network PRIVATE ${NATPMP_LIBRARY})
        endif()
endif()

# =============================================================================
# Unified Library Targets (not built by default)
# =============================================================================
# The executable links this interface to consume the same object files as the
# unified libraries. Keep one authoritative object set for static release
# linking on Windows, Linux, and musl.
if(NOT TARGET ascii-chat-runtime-objects)
    add_library(ascii-chat-runtime-objects INTERFACE)
    foreach(_runtime_module IN ITEMS
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-crypto
        ascii-chat-simd
        ascii-chat-video
        ascii-chat-audio
        ascii-chat-network
        ascii-chat-runtime
        ascii-chat-logging
        ascii-chat-options
        ascii-chat-terminal
        ascii-chat-ui
        ascii-chat-session
        ascii-chat-discovery
        ascii-chat-panic)
        target_sources(ascii-chat-runtime-objects INTERFACE
            $<TARGET_OBJECTS:${_runtime_module}>)
    endforeach()
endif()

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
# Skip shared library for musl builds (static PIE binary only)
# Also skip on Linux Release non-musl: static FFmpeg .a can't link into .so (PIC/TLS issues on arm64)
# Also skip on Windows Release: link object libraries directly into executable for fully static binary
if(BUILD_SHARED_LIBS AND NOT USE_MUSL)
add_library(ascii-chat-shared SHARED
        $<TARGET_OBJECTS:ascii-chat-util>
        $<TARGET_OBJECTS:ascii-chat-data-structures>
        $<TARGET_OBJECTS:ascii-chat-crypto>
        $<TARGET_OBJECTS:ascii-chat-simd>
        $<TARGET_OBJECTS:ascii-chat-video>
        $<TARGET_OBJECTS:ascii-chat-audio>
        $<TARGET_OBJECTS:ascii-chat-network>
        $<TARGET_OBJECTS:ascii-chat-runtime>
        $<TARGET_OBJECTS:ascii-chat-logging>
        $<TARGET_OBJECTS:ascii-chat-options>
        $<TARGET_OBJECTS:ascii-chat-terminal>
        $<TARGET_OBJECTS:ascii-chat-ui>
        $<TARGET_OBJECTS:ascii-chat-session>
        $<TARGET_OBJECTS:ascii-chat-discovery>
        $<TARGET_OBJECTS:ascii-chat-panic>
    )
    # Consumers of the shared object also call platform APIs directly (tests,
    # tooling, and small probes), so keep libplatform transitively visible.
target_link_libraries(ascii-chat-shared PUBLIC ${ASCIICHAT_LIBPLATFORM_TARGET})
    if(NOT EMSCRIPTEN)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-quic)
    endif()
target_link_libraries(ascii-chat-shared PRIVATE asciichat-utf8proc asciichat-libflags asciichat-rnnoise)
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
        WINDOWS_EXPORT_ALL_SYMBOLS TRUE
        # Windows: VERSION/SOVERSION embed version info in PE header (no symlinks)
        # Library version is from lib/v* tags, separate from app version
        VERSION ${ASCIICHAT_LIB_VERSION}
        SOVERSION ${ASCIICHAT_LIB_VERSION_MAJOR}
        # macOS: Consumers resolve the library through their configured runtime paths.
        MACOSX_RPATH TRUE
        INSTALL_NAME_DIR "@rpath"
    )
    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_C_COMPILER MATCHES "clang-cl")
        # The GNU-style Clang driver targets the MSVC ABI here. Match CMake's
        # generated import-library path to the .lib path passed to lld-link.
        set_target_properties(ascii-chat-shared PROPERTIES
            IMPORT_PREFIX ""
            IMPORT_SUFFIX ".lib"
        )
    endif()
    if(WIN32 AND NOT CMAKE_C_COMPILER MATCHES "clang-cl")
        # GNU-style Clang embeds the DLL name in the import library. Keep the
        # runtime DLL name aligned with that embedded name on Windows.
        set_target_properties(ascii-chat-shared PROPERTIES PREFIX "")
    endif()

    # Set RPATH on shared library so it can find dependencies at runtime
    # This ensures transitive dependencies (like custom FFmpeg) are found
    if(APPLE OR UNIX)
        if(APPLE)
            set(_shared_origin "@loader_path")
        else()
            set(_shared_origin "\$ORIGIN")
        endif()
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            # Release builds: use only relative paths to avoid embedded developer paths
            # Copy FFmpeg libraries to lib/ directory alongside shared library
            file(GLOB _ffmpeg_libs_for_shared "${ASCIICHAT_DEPS_CACHE_DIR}/ffmpeg/lib/*.so*")
            foreach(_lib ${_ffmpeg_libs_for_shared})
                add_custom_command(TARGET ascii-chat-shared PRE_LINK
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_lib}" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/"
                )
            endforeach()
            # Use relative rpath to find FFmpeg libraries in the same directory
            if(APPLE)
                set(_shared_rpath_list "@loader_path")
            else()
                set(_shared_rpath_list "\$ORIGIN")
            endif()
            if(BUILD_SHARED_LIBS AND CMAKE_BUILD_RPATH)
                list(APPEND _shared_rpath_list ${CMAKE_BUILD_RPATH})
            endif()
            message(STATUS "Release build: Will copy FFmpeg libs to ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} with \$ORIGIN RPATH")
        else()
            # Non-Release builds: use absolute paths (will be removed by remove_paths.sh in Release only)
            set(_shared_rpath_list "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

            if(CMAKE_BUILD_RPATH)
                list(APPEND _shared_rpath_list ${CMAKE_BUILD_RPATH})
            endif()
            if(APPLE)
                list(APPEND _shared_rpath_list "${HOMEBREW_PREFIX}/lib")
            endif()
        endif()

        # Let CMake encode each runtime search path for the platform linker.
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            # Release: use only our explicit RPATH, don't add linked library paths automatically
            set_target_properties(ascii-chat-shared PROPERTIES
                BUILD_RPATH "${_shared_rpath_list}"
                INSTALL_RPATH "${_shared_rpath_list}"
                SKIP_BUILD_RPATH FALSE
                INSTALL_RPATH_USE_LINK_PATH FALSE
            )
        else()
            # Non-Release: allow automatic RPATH for development
            set_target_properties(ascii-chat-shared PROPERTIES
                BUILD_RPATH "${_shared_rpath_list}"
                INSTALL_RPATH "${_shared_origin};${CMAKE_BUILD_RPATH}"
                SKIP_BUILD_RPATH FALSE
                INSTALL_RPATH_USE_LINK_PATH TRUE
            )
        endif()
    endif()

    # Auto-generate .def file from OBJECT libraries
    # Use a wrapper script to collect object files and generate .def
    # Object files are in CMakeFiles/<target>.dir/ directories
    set(MODULE_TARGETS
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-crypto
        ascii-chat-simd
        ascii-chat-video
        ascii-chat-audio
        ascii-chat-network
        ascii-chat-runtime
        ascii-chat-logging
        ascii-chat-options
        ascii-chat-terminal
        ascii-chat-ui
        ascii-chat-session
        ascii-chat-discovery
        ascii-chat-panic
    )

    # Ensure all object library targets are built before linking the shared library
    # This prevents parallel build race conditions where the linker tries to use
    # object files that haven't finished compiling yet
    add_dependencies(ascii-chat-shared
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-runtime
        ascii-chat-crypto
        ascii-chat-simd
        ascii-chat-video
        ascii-chat-audio
        ascii-chat-network
        ascii-chat-runtime
        ascii-chat-logging
        ascii-chat-options
        ascii-chat-terminal
        ascii-chat-ui
        ascii-chat-session
        ascii-chat-discovery
        ascii-chat-panic
    )

    # Add include directories needed by the OBJECT libraries
    target_include_directories(ascii-chat-shared PRIVATE
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    )

    # Add yyjson include directory
    # Add yyjson include directory (required)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-yyjson)

    # Add PCRE2 include directory and library directory for the shared library
    # PUBLIC because public header include/ascii-chat/util/pcre2.h includes pcre2.h (required)
    target_link_libraries(ascii-chat-shared PUBLIC asciichat-pcre2)
    # PCRE2_CODE_UNIT_WIDTH=8 must be defined before including pcre2.h
    target_compile_definitions(ascii-chat-shared PUBLIC PCRE2_CODE_UNIT_WIDTH=8)

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
        if(ASCIICHAT_ENABLE_IPO)
            target_link_options(ascii-chat-shared PRIVATE
                -Wl,-mllvm,-relocation-model=pic
            )
        endif()
    endif()

    # Add dependencies from modules
    add_dependencies(ascii-chat-shared generate_version)
    if(DEFINED LIBSODIUM_BUILD_TARGET)
        add_dependencies(ascii-chat-shared ${LIBSODIUM_BUILD_TARGET})
    endif()

    # Link external dependencies (libsodium, zstd, opus, etc.)
    # These are needed because shared library is created from OBJECT files,
    # which don't automatically bring in their module dependencies
    target_link_libraries(ascii-chat-shared PRIVATE
        asciichat-icu
        ${ASCIICHAT_NGHTTP2_TARGET}
        asciichat-libsodium
        asciichat-zstd
        asciichat-opus
        asciichat-sqlite3
        asciichat-pcre2
        asciichat-yyjson
    )

    # Coverage instrumentation is propagated through the shared target's
    # compile and link options. Do not link the compiler's profile runtime
    # archive into the shared library: LLVM's runtime symbols must be owned by
    # the final executable, and lld rejects a DSO that references those
    # non-exported symbols when coverage and sanitizers are combined.
    if(ASCIICHAT_ENABLE_COVERAGE)
        target_link_options(ascii-chat-shared PRIVATE --coverage)
    endif()

    # Link PortAudio with platform-specific dependencies
    if(APPLE)
        target_link_libraries(ascii-chat-shared PRIVATE
            asciichat-portaudio
            "-framework CoreFoundation"
            ${ASCIICHAT_CLIPBOARD_FRAMEWORK}
            ${COREAUDIO_FRAMEWORK}
            ${AUDIOUNIT_FRAMEWORK}
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${CORESERVICES_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX AND JACK_LIB)
        target_link_libraries(ascii-chat-shared PRIVATE
            asciichat-portaudio
            ${JACK_LIB}
        )
    else()
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-portaudio)
    endif()

    # Link libsystemd (required for keepawake) on Linux only.
    if(PLATFORM_LINUX AND NOT USE_MUSL)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-systemd)
    endif()

    # Link WebRTC and other network libraries
    target_link_libraries(ascii-chat-shared PRIVATE
        asciichat-libdatachannel
        OpenSSL::SSL
        OpenSSL::Crypto
    )
    if(TARGET asciichat-miniupnpc)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-miniupnpc)
        if(NATPMP_LIBRARY)
            target_include_directories(ascii-chat-shared PRIVATE ${NATPMP_INCLUDE_DIR})
            target_link_libraries(ascii-chat-shared PRIVATE ${NATPMP_LIBRARY})
        endif()
    endif()
    # Link BearSSL (required)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-bearssl)
    # Link libwebsockets
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-libwebsockets)

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

    # Add system library dependencies (only for shared library, which is not built for musl)
    if(WIN32)
    set(CORE_LIBS asciichat-core-dependencies)
    target_link_libraries(ascii-chat-shared PRIVATE
        ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
        ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
        shell32  # CommandLineToArgvW
    crypt32  # For Windows crypto certificate functions
        Winmm    # For timeBeginPeriod/timeEndPeriod
        ${CORE_LIBS}
    )
    # Link BearSSL (required)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-bearssl)
    # Link miniupnpc if available (UPnP/NAT-PMP support)
    if(TARGET asciichat-miniupnpc)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-miniupnpc)
        if(NATPMP_LIBRARY)
            target_include_directories(ascii-chat-shared PRIVATE ${NATPMP_INCLUDE_DIR})
            target_link_libraries(ascii-chat-shared PRIVATE ${NATPMP_LIBRARY})
        endif()
    endif()
    # Windows DLL needs mimalloc built in (no LD_PRELOAD mechanism)
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-mimalloc)
    endif()
    # Delay-load datachannel.dll to avoid loading it for simple commands like --help/--version.
    # Note: We cannot delay-load MSVCP140D.dll because it breaks CRT initialization.
    target_link_options(ascii-chat-shared PRIVATE
        "LINKER:/DELAYLOAD:datachannel.dll"
    )
    target_link_libraries(ascii-chat-shared PRIVATE delayimp)
else()
    # Non-musl builds use normal dependencies
    # Note: Core dependencies are PkgConfig::* IMPORTED targets that include library paths automatically
    set(CORE_LIBS asciichat-core-dependencies)

    # Check for JACK (required if PortAudio was built with JACK support)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JACK jack)

    target_link_libraries(ascii-chat-shared PRIVATE
        ${CORE_LIBS} m asciichat-sqlite3
        asciichat-platform
    )
    # Link miniupnpc if available (UPnP/NAT-PMP support)
    if(TARGET asciichat-miniupnpc)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-miniupnpc)
        if(NATPMP_LIBRARY)
            target_link_libraries(ascii-chat-shared PRIVATE ${NATPMP_LIBRARY})
        endif()
    endif()
    # Link BearSSL (required)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-bearssl)
    # Audio codec and processing
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-opus)
    # Link mimalloc into shared library (required for SAFE_MALLOC macros)
    # Use force_load/whole-archive to export all mimalloc symbols from the shared library
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-mimalloc)
    endif()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-shared PRIVATE
            ${FOUNDATION_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK}
            "-framework CoreFoundation"
            ${ASCIICHAT_CLIPBOARD_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
            ${COREAUDIO_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK}
            ${CORESERVICES_FRAMEWORK} ${IOKIT_FRAMEWORK}
            "-framework SystemConfiguration"
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
    endif()
endif()

# Link external module dependencies to shared library
# These are needed for:
# - Debug builds: linking OBJECT libraries
# - Release builds: compilation and linking of source files that #include their headers
# The linker automatically deduplicates any multiple references
if(TARGET asciichat-webrtc-aec3)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-webrtc-aec3)
endif()

# Link libdatachannel for WebRTC P2P connections
if(TARGET asciichat-libdatachannel)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-libdatachannel)
endif()

# Link FFmpeg for media file streaming
if(TARGET asciichat-ffmpeg)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-ffmpeg)
endif()

# Link FreeType2 and Fontconfig for font rendering
# Use PUBLIC to ensure these are transitively linked when linking static library
target_link_libraries(ascii-chat-shared PRIVATE asciichat-freetype)
if(TARGET asciichat-fontconfig)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-fontconfig)
endif()

# Link vterm for terminal emulation
target_link_libraries(ascii-chat-shared PRIVATE asciichat-vterm)

# Link yyjson for JSON support
target_link_libraries(ascii-chat-shared PRIVATE asciichat-yyjson)

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

# Apply sanitizers to shared library
apply_sanitizers_to_target(ascii-chat-shared)

# =============================================================================
# macOS dSYM Generation for Shared Library (Debug/Dev builds only)
# =============================================================================
# Generate dSYM bundle for the shared library to enable backtraces with line numbers
# for library code (crypto, network, etc.)
if(APPLE AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
    find_program(DSYMUTIL_EXECUTABLE dsymutil)
    if(DSYMUTIL_EXECUTABLE)
        add_custom_command(TARGET ascii-chat-shared POST_BUILD
            COMMAND ${DSYMUTIL_EXECUTABLE} $<TARGET_FILE:ascii-chat-shared> -o $<TARGET_FILE:ascii-chat-shared>.dSYM
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
# Prefer the tool CMake selected for the active compiler toolchain. This avoids
# mixing llvm-nm from a different or incomplete LLVM installation.
if(CMAKE_NM)
    set(ASCIICHAT_SYMBOL_NM_EXECUTABLE "${CMAKE_NM}")
else()
    set(ASCIICHAT_SYMBOL_NM_EXECUTABLE "${ASCIICHAT_LLVM_NM_EXECUTABLE}")
endif()

if(ASCIICHAT_SYMBOL_NM_EXECUTABLE)
    # LLVM's nm reports the exported symbols from the Windows import library,
    # while a PE DLL itself is not an object-symbol table. Keep the generated
    # export definition and validate the import library with the selected
    # compiler family's tool suite.
    if(WIN32)
        set(SHARED_LIB_TO_VALIDATE "$<TARGET_LINKER_FILE:ascii-chat-shared>")
    else()
        set(SHARED_LIB_TO_VALIDATE "$<TARGET_FILE:ascii-chat-shared>")
    endif()

    # Build the validation command with optional internal symbols
    set(_VALIDATE_CMD
        ${CMAKE_COMMAND}
        -DSYMBOL_TOOL=${ASCIICHAT_SYMBOL_NM_EXECUTABLE}
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

endif()  # End BUILD_SHARED_LIBS shared library configuration

# =============================================================================
# Required Symbol Definitions (musl builds skip shared lib but still need these for static lib)
# =============================================================================
if(NOT ASCIICHAT_REQUIRED_SYMBOLS)
    set(ASCIICHAT_REQUIRED_SYMBOLS
        "log_msg"
        "send_packet"
        "receive_packet"
        "crypto_init"
    )
    string(REPLACE ";" "," ASCIICHAT_SYMBOLS_CSV "${ASCIICHAT_REQUIRED_SYMBOLS}")
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
    ascii-chat-crypto
    ascii-chat-simd
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-network
    ascii-chat-runtime
    ascii-chat-logging
    ascii-chat-options
    ascii-chat-terminal
    ascii-chat-ui
    ascii-chat-session
    ascii-chat-discovery
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
if(CMAKE_CONFIGURATION_TYPES)
    set(OBJECT_LIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/static_lib_objects-$<CONFIG>.txt")
else()
    set(OBJECT_LIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/static_lib_objects.txt")
endif()
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
    # Depend on the object files themselves as well as their object-library
    # targets so rebuilt objects refresh the combined archive.
    DEPENDS ${STATIC_LIB_MODULES} ${OBJECT_EXPR_LIST} ${OBJECT_LIST_FILE}
        ${CMAKE_SOURCE_DIR}/cmake/scripts/create_static_archive.cmake
    COMMENT "Creating static archive with unique object names (eliminates duplicates)"
    VERBATIM
)

# Create an IMPORTED library that references the custom archive
add_library(ascii-chat-static-lib STATIC IMPORTED GLOBAL)
set_target_properties(ascii-chat-static-lib PROPERTIES
    IMPORTED_LOCATION ${STATIC_LIB_OUTPUT}
)

# Create a custom target to actually build the archive
# For musl builds, ensure all musl dependency targets are built first
set(MUSL_DEPS "")
if(USE_MUSL)
    if(TARGET asciichat-ffmpeg-musl)
        list(APPEND MUSL_DEPS asciichat-ffmpeg-musl)
    endif()
    if(TARGET openssl-musl)
        list(APPEND MUSL_DEPS openssl-musl)
    endif()
endif()

add_custom_target(build-ascii-chat-static-lib
    DEPENDS ${STATIC_LIB_OUTPUT} ${MUSL_DEPS}
)

# Make the imported library depend on the build target
add_dependencies(ascii-chat-static-lib build-ascii-chat-static-lib)

# Create an INTERFACE library for dependency propagation
# (IMPORTED libraries can't have INTERFACE_LINK_LIBRARIES in older CMake)
add_library(ascii-chat-static INTERFACE)
target_link_libraries(ascii-chat-static INTERFACE asciichat-libplatform asciichat-utf8proc asciichat-icu asciichat-rnnoise)
if(NOT EMSCRIPTEN)
    target_link_libraries(ascii-chat-static INTERFACE asciichat-quic)
endif()

# Link bundled dependencies (not available via package managers)
# BearSSL and WebRTC AEC3 are embedded in the static library
# BearSSL is required
target_link_libraries(ascii-chat-static INTERFACE asciichat-bearssl)
if(NOT EMSCRIPTEN)
    if(TARGET ascii-chat-shared)
        target_link_libraries(ascii-chat-shared PRIVATE asciichat-libuv)
    endif()
    target_link_libraries(ascii-chat-static INTERFACE asciichat-libuv)
    if(NOT ASCIICHAT_NGHTTP2_TARGET)
        message(FATAL_ERROR "ascii-chat-static requires an nghttp2 provider")
    endif()
    target_link_libraries(ascii-chat-static INTERFACE ${ASCIICHAT_NGHTTP2_TARGET})
endif()
target_link_libraries(ascii-chat-static INTERFACE asciichat-webrtc-aec3)

# External dependencies - users must install these via package manager
# These are linked but NOT embedded in the static library
target_link_libraries(ascii-chat-static INTERFACE
    asciichat-pcre2
    asciichat-libsodium
    asciichat-zstd
    asciichat-portaudio
    asciichat-opus
    asciichat-sqlite3
    asciichat-webrtc-aec3
    asciichat-freetype
    asciichat-vterm
    asciichat-libdatachannel
    asciichat-libwebsockets
    OpenSSL::SSL
    OpenSSL::Crypto
)
if(TARGET asciichat-fontconfig)
    target_link_libraries(ascii-chat-static INTERFACE asciichat-fontconfig)
endif()

# Link mimalloc
if(USE_MIMALLOC)
    if(TARGET asciichat-mimalloc)
        target_link_libraries(ascii-chat-static INTERFACE asciichat-mimalloc)
    endif()
endif()

# Link FFmpeg for media file streaming
if(TARGET asciichat-ffmpeg)
    target_link_libraries(ascii-chat-static INTERFACE asciichat-ffmpeg)
endif()

# Link yyjson for structured logging (required)
target_link_libraries(ascii-chat-static INTERFACE asciichat-yyjson)

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(ascii-chat-static INTERFACE
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
    target_link_libraries(ascii-chat-static INTERFACE
        ${FOUNDATION_FRAMEWORK}
        "-framework CoreFoundation"
            ${ASCIICHAT_CLIPBOARD_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${CORESERVICES_FRAMEWORK}
        "-framework IOKit"
        "-framework SystemConfiguration"
        ${ASCIICHAT_STATIC_LIBCXX_LIBS}
    )
else()
    # Linux
    target_link_libraries(ascii-chat-static INTERFACE
        ${CMAKE_THREAD_LIBS_INIT}
        m
        dl
    )

    # Link C++ standard library
    if(USE_MUSL)
        # Musl builds use Alpine's musl-compatible libc++
        if(ALPINE_LIBCXX_STATIC AND ALPINE_LIBCXXABI_STATIC)
            target_link_libraries(ascii-chat-static INTERFACE
                ${ALPINE_LIBCXX_STATIC}
                ${ALPINE_LIBCXXABI_STATIC}
            )
            if(ALPINE_LIBUNWIND_STATIC)
                target_link_libraries(ascii-chat-static INTERFACE ${ALPINE_LIBUNWIND_STATIC})
            endif()
        endif()
    else()
        # Non-musl builds: link the C++ standard library
        target_link_libraries(ascii-chat-static INTERFACE c++)
        # The combined archive includes the platform keepawake implementation
        # on Linux; systemd is not available on Windows or Darwin.
        if(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-static INTERFACE asciichat-systemd)
        endif()
    endif()
endif()

# Link libdatachannel for WebRTC P2P connections (static library)
# The libdatachannel INTERFACE target's dependencies propagate to ascii-chat-static-lib
if(TARGET asciichat-libdatachannel)
    target_link_libraries(ascii-chat-static INTERFACE asciichat-libdatachannel)
endif()

# Link OpenSSL for TURN credentials (required by network module)
# This is linked to ascii-chat-network module, but must also be exposed via the
# INTERFACE library for final executables since libasciichat.a includes turn_credentials.c
if(TARGET OpenSSL::Crypto)
    target_link_libraries(ascii-chat-static INTERFACE OpenSSL::Crypto)
endif()

# Link required miniupnpc for UPnP/NAT-PMP port mapping
# This is linked to ascii-chat-network module, but must also be exposed via the
# INTERFACE library for final executables since libasciichat.a includes upnp.c
if(TARGET asciichat-miniupnpc)
    target_link_libraries(ascii-chat-static INTERFACE asciichat-miniupnpc)
    # Also link libnatpmp on macOS (separate library for NAT-PMP protocol)
    if(NATPMP_LIBRARY)
        target_include_directories(ascii-chat-static INTERFACE ${NATPMP_INCLUDE_DIR})
        target_link_libraries(ascii-chat-static INTERFACE ${NATPMP_LIBRARY})
    endif()
endif()

# =============================================================================
# Symbol Validation for Combined Static Library
# =============================================================================
# Validates that key ascii-chat symbols exist in the combined library
# Uses ASCIICHAT_SYMBOLS_CSV defined earlier (shared between static and shared lib validation)
if(ASCIICHAT_SYMBOL_NM_EXECUTABLE)
    # Add validation step after library is created
    add_custom_command(
        OUTPUT ${STATIC_LIB_OUTPUT}.validated
        COMMAND ${CMAKE_COMMAND}
            -DSYMBOL_TOOL=${ASCIICHAT_SYMBOL_NM_EXECUTABLE}
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
        DEPENDS ${STATIC_LIB_OUTPUT}.validated
        VERBATIM
    )
else()
    message(STATUS "Symbol validation: ${BoldYellow}disabled${ColorReset} (selected symbol tool not found)")
    # User-friendly 'static-lib' target - builds the static library (no validation)
    add_custom_target(static-lib
        DEPENDS ${STATIC_LIB_OUTPUT}
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
    if(TARGET ascii-chat-shared)
        # Use the existing shared target but make it build by default.
        set_target_properties(ascii-chat-shared PROPERTIES EXCLUDE_FROM_ALL FALSE)
        target_link_libraries(ascii-chat-static INTERFACE ascii-chat-shared)
        set(ASCII_CHAT_UNIFIED_BUILD_TARGET ascii-chat-shared)
    else()
        # Static-only configurations (for example security analysis with
        # BUILD_SHARED_LIBS=OFF) still need the unified compatibility target.
        target_link_libraries(ascii-chat-static INTERFACE ascii-chat-static-lib)
        set(ASCII_CHAT_UNIFIED_BUILD_TARGET build-ascii-chat-static-lib)
    endif()
else()
    # Release builds or USE_MUSL: ascii-chat-static wraps the combined static library
    target_link_libraries(ascii-chat-static INTERFACE ascii-chat-static-lib)

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
# - core needs symbols from network (buffer domains) and crypto (known_hosts)
# - network needs symbols from core
# Solution: List consumers before providers, with circulars listed twice
add_library(ascii-chat-lib INTERFACE)
target_link_libraries(ascii-chat-lib INTERFACE
    ascii-chat-simd
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-runtime
    ascii-chat-logging
    ascii-chat-options
    ascii-chat-terminal
    ascii-chat-ui
    ascii-chat-session
    ascii-chat-discovery
    ascii-chat-network
    ascii-chat-crypto
    ascii-chat-network
    ascii-chat-runtime
    ascii-chat-data-structures
    ascii-chat-util
)

# =============================================================================
# Static Library Archive Creation (custom command handles basename collisions)
# =============================================================================
# See cmake/scripts/create_static_archive.cmake for implementation details

# FFTW symbols are used unconditionally by the native audio module.
target_link_libraries(ascii-chat-audio PRIVATE asciichat-fftw)
target_link_libraries(ascii-chat-audio PRIVATE asciichat-fftw-float)
if(ASCIICHAT_FFTW_INCLUDE_DIR)
    target_include_directories(ascii-chat-audio PRIVATE "${ASCIICHAT_FFTW_INCLUDE_DIR}")
endif()
if(FFTW_INCLUDE_DIRS)
    target_include_directories(ascii-chat-audio PRIVATE ${FFTW_INCLUDE_DIRS})
endif()
if(TARGET ascii-chat-shared)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-fftw)
    target_link_libraries(ascii-chat-shared PRIVATE asciichat-fftw-float)
    if(TARGET FFTW3::fftw3f)
        target_link_libraries(ascii-chat-shared PRIVATE FFTW3::fftw3f)
    endif()
endif()
target_link_libraries(ascii-chat-static INTERFACE asciichat-fftw)
target_link_libraries(ascii-chat-static INTERFACE asciichat-fftw-float)
target_link_libraries(ascii-chat-runtime PRIVATE asciichat-fftw-float)

if(NOT EMSCRIPTEN)
    if(NOT TARGET gotel::gotel)
        message(FATAL_ERROR "gotel dependency was configured without a usable target")
    endif()
    set(ASCIICHAT_GOTEL_TARGET gotel::gotel)
    target_link_libraries(ascii-chat-runtime PRIVATE ${ASCIICHAT_GOTEL_TARGET})
    if(TARGET ascii-chat-shared)
        target_link_libraries(ascii-chat-shared PRIVATE ${ASCIICHAT_GOTEL_TARGET})
    endif()
    target_link_libraries(ascii-chat-static INTERFACE ${ASCIICHAT_GOTEL_TARGET})
endif()
target_link_libraries(ascii-chat-static INTERFACE ${ASCIICHAT_LIBFLAGS_TARGET})
