# =============================================================================
# Utility Module (ascii-chat-util)
# =============================================================================
# Ultra-stable utility functions - changes rarely
# No dependencies on other ascii-chat modules
# =============================================================================

set(UTIL_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/parsing.c
    ${CMAKE_CURRENT_SOURCE_DIR}/path.c
    ${CMAKE_CURRENT_SOURCE_DIR}/string.c
    ${CMAKE_CURRENT_SOURCE_DIR}/math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/aspect_ratio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/levenshtein.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utf8.c
)

# Add C23 compatibility wrappers for musl (provides __isoc23_* symbols)
if(USE_MUSL)
    list(APPEND UTIL_SRCS ${CMAKE_SOURCE_DIR}/lib/musl_c23_compat.c)
endif()

create_ascii_chat_module(ascii-chat-util "${UTIL_SRCS}")

# Export sources to parent scope for unified library builds
set(UTIL_SRCS ${UTIL_SRCS} PARENT_SCOPE)
