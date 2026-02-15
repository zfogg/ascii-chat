# lib/platform - Platform abstraction layer

set(PLATFORM_SRCS_COMMON
    ${CMAKE_CURRENT_SOURCE_DIR}/abstraction.c
    ${CMAKE_CURRENT_SOURCE_DIR}/socket.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/filesystem.c
)

if(WIN32)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/thread.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/mutex.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/rwlock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/cond.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/terminal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/keyboard.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/system.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/keepawake.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/socket.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/string.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/util.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/question.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/mmap.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/symbols.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/getopt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/pipe.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/memory.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/process.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/filesystem.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/errno.c
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/agent.c
        ${CMAKE_SOURCE_DIR}/lib/video/webcam/windows/webcam_mediafoundation.c
    )
elseif(PLATFORM_POSIX)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/thread.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/mutex.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/rwlock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/cond.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/terminal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/keyboard.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/system.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/socket.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/string.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/util.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/question.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/mmap.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/symbols.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/pipe.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/memory.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/process.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/filesystem.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/errno.c
        ${CMAKE_CURRENT_SOURCE_DIR}/posix/agent.c
    )

    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_SRCS
            ${CMAKE_CURRENT_SOURCE_DIR}/macos/keepawake.c
            ${CMAKE_SOURCE_DIR}/lib/video/webcam/macos/webcam_avfoundation.m
        )
    elseif(PLATFORM_LINUX)
        list(APPEND PLATFORM_SRCS
            ${CMAKE_CURRENT_SOURCE_DIR}/linux/keepawake.c
            ${CMAKE_SOURCE_DIR}/lib/video/webcam/linux/webcam_v4l2.c
        )
    else()
        message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
    endif()
endif()

set(PLATFORM_SRCS ${PLATFORM_SRCS} PARENT_SCOPE)
