# =============================================================================
# Module 8: Core Application (changes daily)
# =============================================================================
# Core utilities, error handling, logging, and configuration

set(CORE_SRCS
    lib/common.c
    lib/asciichat_errno.c
    lib/log/logging.c
    lib/log/mmap.c
    lib/options/options.c
    lib/options/common.c
    lib/options/client.c
    lib/options/server.c
    lib/options/mirror.c
    lib/options/acds.c
    lib/options/validation.c
    lib/options/levenshtein.c
    lib/options/config.c
    lib/version.c
    # Add tomlc17 parser source
    ${CMAKE_SOURCE_DIR}/deps/tomlc17/src/tomlc17.c
)

# Only include lock debugging runtime in non-release builds (when NDEBUG is not defined)
# debug/lock.c is wrapped in #ifndef NDEBUG, so it's safe to compile in release,
# but we exclude it for clarity and to avoid unnecessary compilation
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND CORE_SRCS lib/debug/lock.c)
    list(APPEND CORE_SRCS lib/debug/memory.c)
endif()

# Disable precompiled headers and static analyzers for tomlc17 (third-party code)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/tomlc17/src/tomlc17.c
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    SKIP_LINTING ON
)
