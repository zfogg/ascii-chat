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

# Enable dead code elimination for optimal binary size
target_compile_options(ascii-chat PRIVATE -ffunction-sections -fdata-sections)
if(NOT WIN32)
    if(APPLE)
        target_link_options(ascii-chat PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(ascii-chat PRIVATE -Wl,--gc-sections)
    endif()
endif()

target_link_libraries(ascii-chat
    ascii-chat-core
    ascii-chat-network
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-crypto
    ascii-chat-simd
    ascii-chat-platform
    ascii-chat-data-structures
    ascii-chat-util
)

# Link mimalloc to the executable (needed for Release builds)
if(USE_MIMALLOC)
    target_link_libraries(ascii-chat mimalloc-static)
    # Add mimalloc include directory for executable sources (they include common.h which includes mimalloc.h)
    target_include_directories(ascii-chat PRIVATE ${FETCHCONTENT_BASE_DIR}/mimalloc-src/include)
endif()

# Preserve custom .ascii_chat_version section during linking
if(NOT WIN32)
    # Unix/macOS: Use linker flags to keep custom section
    target_link_options(ascii-chat PRIVATE -Wl,--keep-section=.ascii_chat_version)
endif()

# Print success message only after ascii-chat gets linked
add_custom_command(TARGET ascii-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "SUCCESS"
    COMMENT "Build completed"
    VERBATIM
)

# Disable PIE for Debug/Dev builds so addr2line can resolve backtrace addresses
# Only on Unix/Linux/macOS - Windows doesn't support -no-pie flag
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
    target_link_options(ascii-chat PRIVATE "LINKER:-no-pie")
endif()

# Add musl dependency if building with musl
if(USE_MUSL)
    # Add dependencies on all musl libraries (they'll build automatically)
    add_dependencies(ascii-chat portaudio-musl alsa-lib-musl libsodium-musl zstd-musl libexecinfo-musl)

    # Link against musl-built static libraries
    target_link_directories(ascii-chat PRIVATE ${MUSL_PREFIX}/lib)
    # Note: -rdynamic is incompatible with -static and not needed for musl + libexecinfo
    target_link_options(ascii-chat PRIVATE -static)

    # Link all libraries (LTO + dead code elimination removes unused code)
    target_link_libraries(ascii-chat
        ${MUSL_PREFIX}/lib/libportaudio.a
        ${MUSL_PREFIX}/lib/libasound.a
        ${MUSL_PREFIX}/lib/libsodium.a
        ${MUSL_PREFIX}/lib/libexecinfo.a
        -lm -lpthread
    )
endif()

# macOS Info.plist embedding (for client mode webcam access)
if(PLATFORM_DARWIN AND EXISTS "${CMAKE_SOURCE_DIR}/Info.plist")
    set_target_properties(ascii-chat PROPERTIES
        LINK_FLAGS "-sectcreate __TEXT __info_plist ${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()

