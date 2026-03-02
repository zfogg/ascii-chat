# lib/util - Core utilities

set(UTIL_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/parsing.c
    ${CMAKE_CURRENT_SOURCE_DIR}/path.c
    ${CMAKE_CURRENT_SOURCE_DIR}/url.c
    ${CMAKE_CURRENT_SOURCE_DIR}/string.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/aspect_ratio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/password.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fps.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utf8.c
    ${CMAKE_CURRENT_SOURCE_DIR}/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pcre2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/env.c
    # utf8proc Unicode library
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/utf8proc/utf8proc.c
)

if(USE_MUSL)
    list(APPEND UTIL_SRCS ${CMAKE_SOURCE_DIR}/lib/musl_c23_compat.c)
endif()

# Data structures (from lib/ root)
set(DATA_STRUCTURES_SRCS
    ${CMAKE_SOURCE_DIR}/lib/ringbuffer.c
    ${CMAKE_SOURCE_DIR}/lib/buffer_pool.c
    ${CMAKE_SOURCE_DIR}/lib/thread_pool.c
)

set(UTIL_SRCS ${UTIL_SRCS} PARENT_SCOPE)
set(DATA_STRUCTURES_SRCS ${DATA_STRUCTURES_SRCS} PARENT_SCOPE)
