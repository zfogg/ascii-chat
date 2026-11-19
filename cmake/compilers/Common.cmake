# Shared compiler/toolchain conventions.
include_guard(GLOBAL)

# Shared warning, hardening, sanitizer, coverage, and linker flag functions.
include(${CMAKE_CURRENT_LIST_DIR}/Flags.cmake)

function(asciichat_compiler_is FAMILY RESULT)
    set(_asciichat_compiler_family "")
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(_asciichat_compiler_family CLANG)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_asciichat_compiler_family GCC)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(_asciichat_compiler_family MSVC)
    else()
        set(_asciichat_compiler_family "${_ASCIICHAT_PRE_PROJECT_COMPILER}")
    endif()
    if(_asciichat_compiler_family STREQUAL FAMILY)
        set(${RESULT} TRUE PARENT_SCOPE)
    else()
        set(${RESULT} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(asciichat_require_tool VARIABLE LABEL)
    if(NOT ${VARIABLE} OR NOT EXISTS "${${VARIABLE}}")
        message(FATAL_ERROR "The selected compiler requires ${LABEL}; set ${VARIABLE} or install the selected toolchain.")
    endif()
endfunction()

function(asciichat_set_tool VARIABLE VALUE LABEL)
    if(NOT VALUE OR NOT EXISTS "${VALUE}")
        message(FATAL_ERROR "Selected compiler tool ${LABEL} was not found: '${VALUE}'")
    endif()
    set(${VARIABLE} "${VALUE}" CACHE FILEPATH "${LABEL}" FORCE)
endfunction()

function(asciichat_configure_common_tools)
    asciichat_compiler_is(MSVC _asciichat_is_msvc)
    asciichat_compiler_is(GCC _asciichat_is_gcc)
    if(_asciichat_is_msvc)
        set(CMAKE_AR "${ASCIICHAT_MSVC_LIB_EXECUTABLE}" CACHE FILEPATH "MSVC librarian" FORCE)
        # lib.exe writes the archive index itself; expose it through CMake's
        # ranlib slots so consumers see a complete native tool suite.
        set(CMAKE_RANLIB "${ASCIICHAT_MSVC_LIB_EXECUTABLE}" CACHE INTERNAL "MSVC archive indexer" FORCE)
        set(CMAKE_LINKER "${ASCIICHAT_MSVC_LINK_EXECUTABLE}" CACHE FILEPATH "MSVC linker" FORCE)
        set(CMAKE_NM "${ASCIICHAT_MSVC_DUMPBIN_EXECUTABLE}" CACHE FILEPATH "MSVC symbol tool" FORCE)
        set(CMAKE_OBJDUMP "${ASCIICHAT_MSVC_DUMPBIN_EXECUTABLE}" CACHE FILEPATH "MSVC object inspection tool" FORCE)
        set(CMAKE_C_COMPILER_AR "${ASCIICHAT_MSVC_LIB_EXECUTABLE}" CACHE FILEPATH "MSVC C librarian" FORCE)
        set(CMAKE_CXX_COMPILER_AR "${ASCIICHAT_MSVC_LIB_EXECUTABLE}" CACHE FILEPATH "MSVC CXX librarian" FORCE)
        set(CMAKE_C_COMPILER_RANLIB "${ASCIICHAT_MSVC_LIB_EXECUTABLE}" CACHE INTERNAL "MSVC C archive indexer" FORCE)
        set(CMAKE_CXX_COMPILER_RANLIB "${ASCIICHAT_MSVC_LIB_EXECUTABLE}" CACHE INTERNAL "MSVC CXX archive indexer" FORCE)
    elseif(_asciichat_is_gcc)
        set(CMAKE_AR "${ASCIICHAT_GCC_AR_EXECUTABLE}" CACHE FILEPATH "GNU archiver" FORCE)
        set(CMAKE_RANLIB "${ASCIICHAT_GCC_RANLIB_EXECUTABLE}" CACHE FILEPATH "GNU ranlib" FORCE)
        set(CMAKE_NM "${ASCIICHAT_GCC_NM_EXECUTABLE}" CACHE FILEPATH "GNU nm" FORCE)
        set(CMAKE_OBJDUMP "${ASCIICHAT_GCC_OBJDUMP_EXECUTABLE}" CACHE FILEPATH "GNU objdump" FORCE)
        set(CMAKE_C_COMPILER_AR "${ASCIICHAT_GCC_AR_EXECUTABLE}" CACHE FILEPATH "GNU C archiver" FORCE)
        set(CMAKE_CXX_COMPILER_AR "${ASCIICHAT_GCC_AR_EXECUTABLE}" CACHE FILEPATH "GNU CXX archiver" FORCE)
        set(CMAKE_C_COMPILER_RANLIB "${ASCIICHAT_GCC_RANLIB_EXECUTABLE}" CACHE FILEPATH "GNU C ranlib" FORCE)
        set(CMAKE_CXX_COMPILER_RANLIB "${ASCIICHAT_GCC_RANLIB_EXECUTABLE}" CACHE FILEPATH "GNU CXX ranlib" FORCE)
    endif()
endfunction()

function(asciichat_validate_selected_compiler)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(_asciichat_compiler_name Clang)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_asciichat_compiler_name GCC)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(_asciichat_compiler_name MSVC)
    else()
        message(FATAL_ERROR "CMake identified incompatible C=${CMAKE_C_COMPILER_ID} and CXX=${CMAKE_CXX_COMPILER_ID}")
    endif()
    if(NOT CMAKE_C_COMPILER_VERSION OR NOT CMAKE_CXX_COMPILER_VERSION)
        message(FATAL_ERROR "The selected C/C++ compiler did not report a usable version")
    endif()
    message(STATUS "Validated ${_asciichat_compiler_name} C/C++ toolchain ${CMAKE_C_COMPILER_VERSION}")
endfunction()

function(configure_compiler_post_project)
    asciichat_validate_selected_compiler()
    asciichat_configure_common_tools()
    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # vcpkg's pthreads port can provide a POSIX compatibility header ahead
        # of MinGW's native pthread.h. Consumers that include C++ threading
        # headers add this directory before dependency include directories.
        execute_process(
            COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
            OUTPUT_VARIABLE _asciichat_gcc_target
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        get_filename_component(_asciichat_gcc_bin "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(_asciichat_gcc_root "${_asciichat_gcc_bin}/.." ABSOLUTE)
        set(_asciichat_gcc_target_include
            "${_asciichat_gcc_root}/${_asciichat_gcc_target}/include")
        if(EXISTS "${_asciichat_gcc_target_include}/pthread.h")
            set(ASCIICHAT_GCC_TARGET_INCLUDE_DIR "${_asciichat_gcc_target_include}"
                CACHE INTERNAL "MinGW target headers for native GCC C++ targets")
            set(ASCIICHAT_GCC_TARGET_INCLUDE_DIR "${_asciichat_gcc_target_include}"
                PARENT_SCOPE)
            set(_asciichat_gcc_pthread_compat_dir
                "${CMAKE_BINARY_DIR}/generated/compilers/mingw")
            file(MAKE_DIRECTORY "${_asciichat_gcc_pthread_compat_dir}")
            file(WRITE "${_asciichat_gcc_pthread_compat_dir}/pthread.h"
                "#pragma once\n"
                "#include \"${_asciichat_gcc_target_include}/pthread.h\"\n"
                "#define PTHREAD_H 1\n"
                "#ifndef _POSIX_SEM_VALUE_MAX\n"
                "#define _POSIX_SEM_VALUE_MAX 32767\n"
                "#endif\n")
            set(ASCIICHAT_GCC_PTHREAD_COMPAT_DIR
                "${_asciichat_gcc_pthread_compat_dir}"
                CACHE INTERNAL "Generated MinGW pthread compatibility header")
        endif()
    endif()
    # Use CMake's launcher interface for every native compiler family.  This
    # keeps ccache in the compile path without wrapping the compiler name and
    # lets each preset retain its native compiler/tool suite.
    if(ASCIICHAT_CCACHE_EXECUTABLE)
        if(NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${ASCIICHAT_CCACHE_EXECUTABLE}" CACHE FILEPATH
                "C compiler cache launcher" FORCE)
        endif()
        if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${ASCIICHAT_CCACHE_EXECUTABLE}" CACHE FILEPATH
                "C++ compiler cache launcher" FORCE)
        endif()
    endif()
    asciichat_compiler_is(MSVC _asciichat_is_msvc)
    if(_asciichat_is_msvc)
        # MSVC's librarian creates the archive index itself.
        set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> /nologo /out:<TARGET> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> /nologo /out:<TARGET> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_C_ARCHIVE_FINISH "" PARENT_SCOPE)
        set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> /nologo /out:<TARGET> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> /nologo /out:<TARGET> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_CXX_ARCHIVE_FINISH "" PARENT_SCOPE)
    elseif(CMAKE_RANLIB)
        set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> q <TARGET> <LINK_FLAGS> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>" PARENT_SCOPE)
        set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> q <TARGET> <LINK_FLAGS> <OBJECTS>" PARENT_SCOPE)
        set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>" PARENT_SCOPE)
    endif()
endfunction()

function(asciichat_sanitize_gnu_imported_link_options)
    if(NOT WIN32 OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        return()
    endif()

    get_property(_asciichat_imported_targets DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(_asciichat_imported_target IN LISTS _asciichat_imported_targets)
        if(NOT TARGET "${_asciichat_imported_target}")
            continue()
        endif()
        get_target_property(_asciichat_link_libraries
            "${_asciichat_imported_target}" INTERFACE_LINK_LIBRARIES)
        if(_asciichat_link_libraries)
            list(FILTER _asciichat_link_libraries EXCLUDE REGEX "^-ignore:4221$")
            set_property(TARGET "${_asciichat_imported_target}"
                PROPERTY INTERFACE_LINK_LIBRARIES "${_asciichat_link_libraries}")
        endif()
    endforeach()
endfunction()
