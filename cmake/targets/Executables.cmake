# =============================================================================
# Executables Module
# =============================================================================
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
add_executable(ascii-chat
    src/main.c
    # Server mode sources
    src/server/main.c
    src/server/client.c
    src/server/protocol.c
    src/server/crypto.c
    src/server/stream.c
    src/server/render.c
    src/server/stats.c
    # Client mode sources
    src/client/main.c
    src/client/server.c
    src/client/protocol.c
    src/client/crypto.c
    src/client/display.c
    src/client/capture.c
    src/client/audio.c
    src/client/keepalive.c
)

# Link against the combined library instead of individual libraries
# Ensure the combined library is built before linking
# For Debug/Dev/Coverage: shared library (DLL on Windows)
# For Release: static library
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Coverage")
    add_dependencies(ascii-chat ascii-chat-shared generate_version)
    target_link_libraries(ascii-chat ascii-chat-shared)

    # Explicitly add system libraries when using shared library
    # The executable needs to link against the same dependencies as the shared library
    if(WIN32)
        # Import library is in bin directory with the DLL
        target_link_directories(ascii-chat PRIVATE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
        target_link_libraries(ascii-chat
            ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
            ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
            crypt32
            Winmm  # For timeBeginPeriod/timeEndPeriod
            ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES}
        )
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        endif()
        if(USE_MIMALLOC)
            target_link_libraries(ascii-chat ${MIMALLOC_LIBRARIES})
        endif()
    else()
        # Unix: Also need explicit dependencies when linking against shared library
        target_link_libraries(ascii-chat
            ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES}
        )
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        endif()
        if(USE_MIMALLOC)
            target_link_libraries(ascii-chat ${MIMALLOC_LIBRARIES})
        endif()
    endif()
else()
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

# Include directories for executable (needed for version.h and other generated headers)
target_include_directories(ascii-chat PRIVATE ${CMAKE_BINARY_DIR}/generated)

# Add build timing for ascii-chat executable
# Record start time before linking (only when actually building)
add_custom_command(TARGET ascii-chat PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    COMMENT "Recording build start time for ascii-chat"
    VERBATIM
)

# Show timing and success message after build completes
# This runs whenever ascii-chat is actually rebuilt (POST_BUILD)
add_custom_command(TARGET ascii-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    COMMENT ""
    VERBATIM
)

# Custom target wrapper for ALL builds (when no explicit --target is used)
# This shows "up to date" message when ascii-chat doesn't need rebuilding
add_custom_target(show-ascii-chat-success ALL
    COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    DEPENDS ascii-chat
    VERBATIM
)

# macOS Info.plist embedding (for client mode webcam access)
if(PLATFORM_DARWIN AND EXISTS "${CMAKE_SOURCE_DIR}/Info.plist")
    set_target_properties(ascii-chat PROPERTIES
        LINK_FLAGS "-sectcreate __TEXT __info_plist ${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()

# Link mimalloc to the executable (needed for Release builds)
if(USE_MIMALLOC)
    target_link_libraries(ascii-chat mimalloc-static)
    # Add mimalloc include directory for executable sources (they include common.h which includes mimalloc.h)
    target_include_directories(ascii-chat PRIVATE ${FETCHCONTENT_BASE_DIR}/mimalloc-src/include)
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

# Disable PIE for Debug/Dev builds so addr2line can resolve backtrace addresses
# Only on Unix/Linux/macOS - Windows doesn't support -no-pie flag
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
    target_link_options(ascii-chat PRIVATE "LINKER:-no-pie")
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

    # Link-time optimization (LTO) for Release executable only
    # Not applied to shared libraries (would strip symbols needed by external users)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        target_compile_options(ascii-chat PRIVATE -flto)
        target_link_options(ascii-chat PRIVATE -flto)
    endif()
endif()

if(NOT WIN32)
    if(APPLE)
        target_link_options(ascii-chat PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(ascii-chat PRIVATE -Wl,--gc-sections)
    endif()
endif()

# =============================================================================
# Global Build Timer - End Marker
# =============================================================================
# Show total build time after the final target is built
# ascii-chat is always built last, so attach the timer there
add_custom_command(TARGET ascii-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=build-total -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    COMMENT ""
    VERBATIM
)

# Custom target wrapper for ALL builds (when no explicit --target)
# This only runs for default builds and prevents duplicate messages
add_custom_target(build-timer-end ALL
    DEPENDS show-ascii-chat-success
    VERBATIM
)

# If shared library was also built, wait for it too
if(TARGET show-ascii-chat-shared-success)
    add_dependencies(build-timer-end show-ascii-chat-shared-success)
endif()
