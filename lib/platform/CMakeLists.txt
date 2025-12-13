# =============================================================================
# Platform Abstraction Module (ascii-chat-platform)
# =============================================================================
# Cross-platform abstraction layer for threads, mutexes, sockets, terminal I/O
# Supports Linux, macOS, and Windows
# =============================================================================

# Common platform-independent sources
set(PLATFORM_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/abstraction.c
    # NOTE: system.c is included by platform-specific implementations
)

# =============================================================================
# Platform-Specific Sources
# =============================================================================

if(WIN32)
    list(APPEND PLATFORM_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/thread.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/mutex.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/rwlock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/cond.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/terminal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/system.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/socket.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/string.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/password.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/symbols.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/getopt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/pipe.c
    )
elseif(PLATFORM_POSIX)
    # POSIX platforms (Linux/macOS)
    list(APPEND PLATFORM_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/thread.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/mutex.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/rwlock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/cond.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/terminal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/system.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/socket.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/string.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/password.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/symbols.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/pipe.c
    )
endif()

create_ascii_chat_module(ascii-chat-platform "${PLATFORM_SRCS}")

# =============================================================================
# Musl Kernel Headers
# =============================================================================
# Add kernel headers for musl builds (needed for V4L2)
if(USE_MUSL AND DEFINED MUSL_DEPS_DIR AND EXISTS "${MUSL_DEPS_DIR}/musl-deps/kernel-headers")
    target_include_directories(ascii-chat-platform PRIVATE
        "${MUSL_DEPS_DIR}/musl-deps/kernel-headers"
    )
endif()

# =============================================================================
# Platform-Specific System Libraries
# =============================================================================

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
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-platform ${CMAKE_THREAD_LIBS_INIT})
    endif()
endif()

# Export sources to parent scope for unified library builds
set(PLATFORM_SRCS ${PLATFORM_SRCS} PARENT_SCOPE)
