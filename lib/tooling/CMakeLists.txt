# =============================================================================
# Tooling Module (ascii-chat-panic)
# =============================================================================
# Runtime libraries for development tooling:
# - Panic instrumentation: execution tracing and coverage
# =============================================================================

# Panic instrumentation runtime sources
set(TOOLING_PANIC_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/panic/instrument_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/panic/instrument_cov.c
)

create_ascii_chat_module(ascii-chat-panic "${TOOLING_PANIC_SRCS}")

# Additional include paths for tooling
target_include_directories(ascii-chat-panic PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)

# =============================================================================
# Dependencies
# =============================================================================

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-panic
        PRIVATE
            ascii-chat-util
            ascii-chat-platform
            ascii-chat-core
    )
endif()

# Thread support
if(NOT WIN32 AND TARGET Threads::Threads)
    target_link_libraries(ascii-chat-panic PUBLIC Threads::Threads)
endif()

# =============================================================================
# Panic Report Tool Sources (for src/tooling/)
# =============================================================================
# Export panic report sources for the standalone tool
set(TOOLING_PANIC_REPORT_SRCS
    ${CMAKE_SOURCE_DIR}/src/tooling/panic/report.c
    PARENT_SCOPE
)
