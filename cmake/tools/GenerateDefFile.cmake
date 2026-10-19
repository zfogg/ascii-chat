# Generate a Windows .def file from object files for DLL export
cmake_minimum_required(VERSION 3.16)

# Use the active target object list. Directory scans retain stale exports after
# a source is removed or renamed in an incremental build.
foreach(required NM_TOOL OBJECT_LIST_FILE OUTPUT_FILE LIBRARY_NAME)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is not defined")
    endif()
endforeach()
file(STRINGS "${OBJECT_LIST_FILE}" OBJ_FILES)
list(FILTER OBJ_FILES EXCLUDE REGEX "[/\\\\]deps[/\\\\]")
if(NOT OBJ_FILES)
    message(FATAL_ERROR "No active library object files in ${OBJECT_LIST_FILE}")
endif()

list(LENGTH OBJ_FILES obj_count)
message(STATUS "Scanning ${obj_count} object files from library modules")

message(STATUS "Generating .def file: ${OUTPUT_FILE}")

# Extract all public symbols from object files
set(ALL_SYMBOLS "")
foreach(OBJ_FILE ${OBJ_FILES})
    execute_process(
        COMMAND ${NM_TOOL} --format=just-symbols --extern-only --defined-only "${OBJ_FILE}"
        OUTPUT_VARIABLE NM_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Request only symbol names. The default and POSIX llvm-nm record layouts
    # differ across Windows toolchain versions.
    string(REPLACE "\r\n" "\n" NM_OUTPUT "${NM_OUTPUT}")
    string(REPLACE "\r" "\n" NM_OUTPUT "${NM_OUTPUT}")
    string(REPLACE "\n" ";" NM_LINES "${NM_OUTPUT}")
    foreach(NM_LINE ${NM_LINES})
        string(STRIP "${NM_LINE}" NM_LINE)
        set(SYMBOL_NAME "")
        if(NM_LINE MATCHES "^([^ \t]+)$")
            set(SYMBOL_NAME "${CMAKE_MATCH_1}")
        endif()
        if(SYMBOL_NAME)
            list(APPEND ALL_SYMBOLS "${SYMBOL_NAME}")
        endif()
    endforeach()
endforeach()

# Remove duplicates and sort
list(REMOVE_DUPLICATES ALL_SYMBOLS)
list(SORT ALL_SYMBOLS)

# Write .def file
file(WRITE "${OUTPUT_FILE}" "LIBRARY ${LIBRARY_NAME}\nEXPORTS\n")
set(EXPORT_COUNT 0)
foreach(SYMBOL ${ALL_SYMBOLS})
    # Skip symbols that shouldn't be exported:
    # - Compiler/runtime internals (__imp_, __real_, __security, etc.)
    # - Windows API/SDK symbols (CLSID_, IID_, GUID_, MF*, KSPR*, ME*, IN6_, etc.)
    # - C++ name mangling (?)
    # - Debug symbols (DW.)
    # Filter out symbols that shouldn't be exported:
    # - Compiler/runtime internals (__imp_, __real_, __security, etc.)
    # - Windows SDK/Media Foundation symbols (MF*, MR_*, CLSID_, IID_, GUID_, etc.)
    # - C++ name mangling (?)
    # - Debug symbols (DW.)
    # - CRT internals (_CT, _CTA, _TI, __local_stdio, _snprintf, etc.)
    # - Sanitizer internals (__sanitizer_*)
    if(NOT SYMBOL MATCHES "^(__imp_|__real[@_]|__xmm@|__ymm@|_?GLOBAL__|DW\\.|_fltused|_tls_|__security|__guard|GS_|_RTC|\\?|^\\.|___|CLSID_|IID_|GUID_|KSPROPERTYSETID_|MF[A-Z_]|MR_|ME[A-Z]|IN6_|_CT\\?|_CTA[0-9]|_TI[0-9]|__local_stdio|__sanitizer_|_Avx2|_Check_memory)")
        file(APPEND "${OUTPUT_FILE}" "    ${SYMBOL}\n")
        math(EXPR EXPORT_COUNT "${EXPORT_COUNT} + 1")
    endif()
endforeach()

message(STATUS "Generated ${OUTPUT_FILE} with ${EXPORT_COUNT} exported symbols")
