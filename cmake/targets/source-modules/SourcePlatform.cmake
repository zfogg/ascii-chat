# =============================================================================
# Module 3: Platform Abstraction (stable - changes monthly)
# =============================================================================
# Cross-platform abstraction layer for threads, sockets, and system functions

set(PLATFORM_SRCS_COMMON
    lib/platform/abstraction.c
    lib/platform/socket.c
    lib/platform/thread.c
    # NOTE: lib/platform/system.c is included by windows/system.c and posix/system.c
)

if(WIN32)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        lib/platform/windows/thread.c
        lib/platform/windows/mutex.c
        lib/platform/windows/rwlock.c
        lib/platform/windows/cond.c
        lib/platform/windows/terminal.c
        lib/platform/windows/system.c
        lib/platform/windows/socket.c
        lib/platform/windows/string.c
        lib/platform/windows/question.c
        lib/platform/windows/mmap.c
        lib/platform/windows/symbols.c
        lib/platform/windows/getopt.c
        lib/platform/windows/pipe.c
        lib/platform/windows/memory.c
        lib/platform/windows/process.c
        lib/platform/windows/fs.c
        lib/video/webcam/windows/webcam_mediafoundation.c
    )
elseif(PLATFORM_POSIX)
    # POSIX platforms (Linux/macOS)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        lib/platform/posix/thread.c
        lib/platform/posix/mutex.c
        lib/platform/posix/rwlock.c
        lib/platform/posix/cond.c
        lib/platform/posix/terminal.c
        lib/platform/posix/system.c
        lib/platform/posix/socket.c
        lib/platform/posix/string.c
        lib/platform/posix/question.c
        lib/platform/posix/mmap.c
        lib/platform/posix/symbols.c
        lib/platform/posix/pipe.c
        lib/platform/posix/memory.c
        lib/platform/posix/process.c
        lib/platform/posix/fs.c
    )

    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_SRCS
            lib/video/webcam/macos/webcam_avfoundation.m
        )
    elseif(PLATFORM_LINUX)
        list(APPEND PLATFORM_SRCS
            lib/video/webcam/linux/webcam_v4l2.c
        )
    else()
        message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}. We don't have webcam code for this platform.")
    endif()
endif()
