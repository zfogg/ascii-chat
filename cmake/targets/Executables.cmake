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
# For Release: static library
# For USE_MUSL: always static (musl requires static linking)
if((CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev") AND NOT USE_MUSL)
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
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        endif()
        # Link WebRTC audio processing for echo cancellation
        if(TARGET webrtc_audio_processing)
            target_link_libraries(ascii-chat webrtc_audio_processing)
        endif()
        # Note: mimalloc comes from ascii-chat-shared (PUBLIC linkage), no need to link explicitly
    else()
        # Unix: Also need explicit dependencies when linking against shared library
        get_core_deps_libraries(CORE_LIBS)
        target_link_libraries(ascii-chat
            ${CORE_LIBS}
        )
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        endif()
        # Link WebRTC audio processing for echo cancellation
        if(TARGET webrtc_audio_processing)
            target_link_libraries(ascii-chat webrtc_audio_processing)
        endif()
        # Link mimalloc explicitly - the shared library links it PRIVATE so symbols don't propagate
        if(USE_MIMALLOC AND MIMALLOC_LIBRARIES)
            target_link_libraries(ascii-chat ${MIMALLOC_LIBRARIES})
        endif()
    endif()
else()
    # Release builds OR USE_MUSL builds: use static library
    # USE_MUSL needs static library because musl requires static linking
    add_dependencies(ascii-chat ascii-chat-static-build generate_version)
    target_link_libraries(ascii-chat ascii-chat-static)
    # Define BUILDING_STATIC_LIB for executable when using static library (Windows)
    # This prevents LNK4217 warnings about dllimport on locally defined symbols
    if(WIN32)
        target_compile_definitions(ascii-chat PRIVATE
            BUILDING_STATIC_LIB=1
            _WIN32_WINNT=0x0A00  # Windows 10
        )
    endif()
endif()

# Ensure build directory takes precedence in rpath for Debug/Dev builds
# This prevents conflicts with installed libraries in system directories
# Skip for Release builds - they use static linking and shouldn't embed developer paths
if((APPLE OR UNIX) AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    # Explicitly set BUILD_RPATH to put build/lib first, before any system paths
    # This ensures we use the freshly built library, not any installed version
    set_target_properties(ascii-chat PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY};${CMAKE_BUILD_RPATH}"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Release builds use static linking - no rpath needed
    set_target_properties(ascii-chat PROPERTIES
        SKIP_BUILD_RPATH TRUE
        INSTALL_RPATH ""
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# Include directories for executable (needed for version.h and other generated headers)
target_include_directories(ascii-chat PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

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
    add_dependencies(ascii-chat portaudio-musl alsa-lib-musl libsodium-musl zstd-musl libexecinfo-musl)

    # Link against musl-built static libraries from individual dependency directories
    target_link_directories(ascii-chat PRIVATE
        ${PORTAUDIO_PREFIX}/lib
        ${ALSA_PREFIX}/lib
        ${LIBSODIUM_PREFIX}/lib
        ${LIBEXECINFO_PREFIX}/lib
    )
    # Note: -rdynamic is incompatible with -static and not needed for musl + libexecinfo
    # Use lld linker for musl+LTO builds (handles LTO without gold plugin)
    target_link_options(ascii-chat PRIVATE -static -fuse-ld=lld)

    # Link all libraries (LTO + dead code elimination removes unused code)
    target_link_libraries(ascii-chat
        ${PORTAUDIO_PREFIX}/lib/libportaudio.a
        ${ALSA_PREFIX}/lib/libasound.a
        ${LIBSODIUM_PREFIX}/lib/libsodium.a
        ${LIBEXECINFO_PREFIX}/lib/libexecinfo.a
        -lm -lpthread
    )

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
