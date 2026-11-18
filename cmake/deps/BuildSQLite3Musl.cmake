cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS SQLITE3_CC SQLITE3_AR SQLITE3_TARGET_FLAG SQLITE3_WRAPPER
    SQLITE3_OBJECT SQLITE3_ARCHIVE SQLITE3_SOURCE_DIR SQLITE3_PREFIX)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required to build SQLite3 for musl")
    endif()
endforeach()

separate_arguments(_sqlite3_cflags UNIX_COMMAND "${SQLITE3_CFLAGS}")
file(MAKE_DIRECTORY "${SQLITE3_PREFIX}/lib" "${SQLITE3_PREFIX}/include")

execute_process(
    COMMAND "${SQLITE3_CC}" "${SQLITE3_TARGET_FLAG}" ${_sqlite3_cflags}
        -U_GNU_SOURCE
        -DSQLITE_THREADSAFE=1
        -DSQLITE_ENABLE_FTS5
        -DSQLITE_ENABLE_RTREE
        -DSQLITE_ENABLE_JSON1
        -Ufcntl64
        -Dfcntl64=fcntl
        -O3 -fPIC "${SQLITE3_WRAPPER}" -o "${SQLITE3_OBJECT}"
    RESULT_VARIABLE _compile_result
    OUTPUT_VARIABLE _compile_output
    ERROR_VARIABLE _compile_error)
if(NOT _compile_result EQUAL 0)
    message(FATAL_ERROR "Failed to compile SQLite3:\n${_compile_output}\n${_compile_error}")
endif()

find_program(_sqlite3_objcopy NAMES llvm-objcopy objcopy REQUIRED)
execute_process(
    COMMAND "${_sqlite3_objcopy}" --redefine-sym fcntl64=fcntl "${SQLITE3_OBJECT}"
    RESULT_VARIABLE _objcopy_result
    ERROR_VARIABLE _objcopy_error)
if(NOT _objcopy_result EQUAL 0)
    message(FATAL_ERROR "Failed to normalize SQLite3 musl symbols:\n${_objcopy_error}")
endif()

execute_process(
    COMMAND "${SQLITE3_AR}" rcs "${SQLITE3_ARCHIVE}" "${SQLITE3_OBJECT}"
    RESULT_VARIABLE _archive_result
    ERROR_VARIABLE _archive_error)
if(NOT _archive_result EQUAL 0)
    message(FATAL_ERROR "Failed to create SQLite3 static library:\n${_archive_error}")
endif()

file(COPY "${SQLITE3_SOURCE_DIR}/sqlite3.h" "${SQLITE3_SOURCE_DIR}/sqlite3ext.h"
    DESTINATION "${SQLITE3_PREFIX}/include")
file(WRITE "${SQLITE3_SIGNATURE_FILE}" "${SQLITE3_SIGNATURE}")
