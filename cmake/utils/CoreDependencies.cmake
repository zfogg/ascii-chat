# =============================================================================
# Core Dependencies Configuration
# =============================================================================
# Centralizes the list of core system dependencies to avoid repetition
# across Include.cmake, Libraries.cmake, and Executables.cmake
#
# This module provides:
#   - ASCIICHAT_CORE_DEPS: List of core dependency names
#   - Functions to add include directories, compile flags, and link libraries
# =============================================================================

# List of core system dependencies (in order of precedence)
set(ASCIICHAT_CORE_DEPS
    PORTAUDIO
    OPUS
    ZSTD
    LIBSODIUM
)

# =============================================================================
# Helper Functions
# =============================================================================

# Add include directories for all core dependencies
# Usage: add_core_deps_include_directories([SYSTEM])
function(add_core_deps_include_directories)
    set(SYSTEM_FLAG "")
    if(ARGC GREATER 0 AND "${ARGV0}" STREQUAL "SYSTEM")
        set(SYSTEM_FLAG "SYSTEM")
    endif()

    foreach(DEP ${ASCIICHAT_CORE_DEPS})
        set(INCLUDE_VAR "${DEP}_INCLUDE_DIRS")
        if(DEFINED ${INCLUDE_VAR})
            if(SYSTEM_FLAG)
                include_directories(SYSTEM ${${INCLUDE_VAR}})
            else()
                include_directories(${${INCLUDE_VAR}})
            endif()
        endif()
    endforeach()
endfunction()

# Add compile flags for all core dependencies
# Usage: add_core_deps_compile_flags()
function(add_core_deps_compile_flags)
    foreach(DEP ${ASCIICHAT_CORE_DEPS})
        set(CFLAGS_VAR "${DEP}_CFLAGS_OTHER")
        if(DEFINED ${CFLAGS_VAR} AND NOT "${${CFLAGS_VAR}}" STREQUAL "")
            add_compile_options(${${CFLAGS_VAR}})
        endif()
    endforeach()
endfunction()

# Get list of all core dependency libraries
# Usage: get_core_deps_libraries(OUTPUT_VAR)
function(get_core_deps_libraries OUTPUT_VAR)
    set(DEPS_LIBS)
    foreach(DEP ${ASCIICHAT_CORE_DEPS})
        set(LIB_VAR "${DEP}_LIBRARIES")
        if(DEFINED ${LIB_VAR})
            list(APPEND DEPS_LIBS ${${LIB_VAR}})
        endif()
    endforeach()
    set(${OUTPUT_VAR} ${DEPS_LIBS} PARENT_SCOPE)
endfunction()

# Get list of all core dependency libraries (for musl system packages)
# Usage: get_core_deps_sys_libraries(OUTPUT_VAR)
function(get_core_deps_sys_libraries OUTPUT_VAR)
    set(DEPS_LIBS)
    foreach(DEP ${ASCIICHAT_CORE_DEPS})
        set(LIB_VAR "${DEP}_SYS_LIBRARIES")
        if(DEFINED ${LIB_VAR})
            list(APPEND DEPS_LIBS ${${LIB_VAR}})
        endif()
    endforeach()
    set(${OUTPUT_VAR} ${DEPS_LIBS} PARENT_SCOPE)
endfunction()

# Get list of all core dependency include directories
# Usage: get_core_deps_include_dirs(OUTPUT_VAR)
function(get_core_deps_include_dirs OUTPUT_VAR)
    set(DEPS_INCS)
    foreach(DEP ${ASCIICHAT_CORE_DEPS})
        set(INC_VAR "${DEP}_INCLUDE_DIRS")
        if(DEFINED ${INC_VAR})
            list(APPEND DEPS_INCS ${${INC_VAR}})
        endif()
    endforeach()
    set(${OUTPUT_VAR} ${DEPS_INCS} PARENT_SCOPE)
endfunction()

# Get list of all core dependency sys include directories (for musl)
# Usage: get_core_deps_sys_include_dirs(OUTPUT_VAR)
function(get_core_deps_sys_include_dirs OUTPUT_VAR)
    set(DEPS_INCS)
    foreach(DEP ${ASCIICHAT_CORE_DEPS})
        set(INC_VAR "${DEP}_SYS_INCLUDE_DIRS")
        if(DEFINED ${INC_VAR})
            list(APPEND DEPS_INCS ${${INC_VAR}})
        endif()
    endforeach()
    set(${OUTPUT_VAR} ${DEPS_INCS} PARENT_SCOPE)
endfunction()

# Strip a suffix from all items in a list
# Usage: strip_suffix_from_list(INPUT_LIST "/opus" OUTPUT_VAR)
function(strip_suffix_from_list INPUT_LIST SUFFIX OUTPUT_VAR)
    set(CLEANED_LIST)
    foreach(ITEM ${INPUT_LIST})
        string(REGEX REPLACE "${SUFFIX}$" "" ITEM_CLEANED "${ITEM}")
        list(APPEND CLEANED_LIST ${ITEM_CLEANED})
    endforeach()
    set(${OUTPUT_VAR} ${CLEANED_LIST} PARENT_SCOPE)
endfunction()
