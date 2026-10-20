cmake_minimum_required(VERSION 3.30)
if(NOT SOURCE_DIR OR NOT RESPONSE_FILE OR NOT EXISTS "${CLANG_FORMAT_EXECUTABLE}")
    message(FATAL_ERROR "Formatting requires SOURCE_DIR, RESPONSE_FILE and a working clang-format executable")
endif()

# Include newly authored files but exclude ignored generated files and submodules.
# Collect at build time so adding a test does not require reconfiguring first.
set(_sources)
if(EXISTS "${SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" ls-files --cached --others --exclude-standard
            WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE _tracked
            RESULT_VARIABLE _git_result OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_git_result EQUAL 0)
            string(REPLACE "\n" ";" _sources "${_tracked}")
        endif()
    endif()
endif()

# Release source archives have no Git index. Use the authored source roots.
if(NOT _sources)
    foreach(_directory cmake include lib scripts src tests todo)
        file(GLOB_RECURSE _files LIST_DIRECTORIES FALSE RELATIVE "${SOURCE_DIR}"
            "${SOURCE_DIR}/${_directory}/*")
        foreach(_file IN LISTS _files)
            if(NOT _file MATCHES "(^|/)(CMakeFiles|_deps|node_modules)(/|$)"
                    AND NOT _file STREQUAL "include/ascii-chat/version.h")
                list(APPEND _sources "${_file}")
            endif()
        endforeach()
    endforeach()
endif()

set(_response "")
set(_count 0)
list(REMOVE_DUPLICATES _sources)
list(SORT _sources)
foreach(_source IN LISTS _sources)
    if(_source MATCHES "^deps/")
        continue()
    endif()
    string(TOLOWER "${_source}" _extension)
    if(_extension MATCHES "\\.(c|h|cc|cpp|cxx|hpp|hh|hxx|m|mm)(\\.in)?$"
            AND EXISTS "${SOURCE_DIR}/${_source}" AND NOT IS_DIRECTORY "${SOURCE_DIR}/${_source}")
        file(TO_CMAKE_PATH "${SOURCE_DIR}/${_source}" _path)
        string(REPLACE "\"" "\\\"" _path "${_path}")
        string(APPEND _response "\"${_path}\"\n")
        math(EXPR _count "${_count} + 1")
    endif()
endforeach()
if(_count EQUAL 0)
    message(FATAL_ERROR "No authored C-family sources found in ${SOURCE_DIR}")
endif()
file(WRITE "${RESPONSE_FILE}" "${_response}")
if(CHECK_ONLY)
    set(_mode --dry-run -Werror)
    message(STATUS "Checking ${_count} authored C/C++/Objective-C files")
else()
    set(_mode -i)
    message(STATUS "Formatting ${_count} authored C/C++/Objective-C files")
endif()
execute_process(COMMAND "${CLANG_FORMAT_EXECUTABLE}" ${_mode} "@${RESPONSE_FILE}"
    COMMAND_ERROR_IS_FATAL ANY)
