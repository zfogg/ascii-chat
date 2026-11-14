# Generate a Windows .def file from object files for DLL export
cmake_minimum_required(VERSION 3.30)

# Use the active target object list. Directory scans retain stale exports after
# a source is removed or renamed in an incremental build.
foreach(required NM_TOOL OBJECT_LIST_FILE OUTPUT_FILE LIBRARY_NAME)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is not defined or is empty")
    endif()
endforeach()
file(STRINGS "${OBJECT_LIST_FILE}" OBJ_FILES)
if(NOT OBJ_FILES)
    message(FATAL_ERROR "No active library object files in ${OBJECT_LIST_FILE}")
endif()

list(LENGTH OBJ_FILES obj_count)

# A response file avoids the Windows command-line limit and starts llvm-nm once.
# PCH entries in TARGET_OBJECTS are compiler inputs, not exportable object files.
list(FILTER OBJ_FILES INCLUDE REGEX "\\.(obj|o)$")
# TARGET_OBJECTS can contain objects from linked dependency targets.  Only
# objects belonging to the library itself may contribute exports; dependency
# symbols are either private implementation details or are exported by their
# own target.  Normalize separators first so this works on Windows and POSIX.
set(_project_objects "")
foreach(OBJ_FILE IN LISTS OBJ_FILES)
    file(TO_CMAKE_PATH "${OBJ_FILE}" _normalized_object)
    if(NOT _normalized_object MATCHES "(^|/)deps/")
        list(APPEND _project_objects "${OBJ_FILE}")
    endif()
endforeach()
set(OBJ_FILES "${_project_objects}")
if(NOT OBJ_FILES)
    message(FATAL_ERROR "No exportable object files in ${OBJECT_LIST_FILE}")
endif()
set(_nm_response "")
foreach(OBJ_FILE IN LISTS OBJ_FILES)
    file(TO_CMAKE_PATH "${OBJ_FILE}" _nm_object)
    string(APPEND _nm_response "\"${_nm_object}\"\n")
endforeach()
set(_nm_response_file "${OUTPUT_FILE}.objects.rsp")
file(WRITE "${_nm_response_file}" "${_nm_response}")
execute_process(
    COMMAND "${NM_TOOL}" --format=just-symbols --extern-only --defined-only "@${_nm_response_file}"
    OUTPUT_VARIABLE NM_OUTPUT
    ERROR_VARIABLE NM_ERROR
    RESULT_VARIABLE NM_RESULT
)
if(NOT NM_RESULT STREQUAL "0")
    message(FATAL_ERROR "Symbol extraction failed (${NM_RESULT}): ${NM_ERROR}")
endif()
string(REPLACE "\r\n" "\n" NM_OUTPUT "${NM_OUTPUT}")
string(REPLACE "\r" "\n" NM_OUTPUT "${NM_OUTPUT}")
string(REPLACE "\n" ";" ALL_SYMBOLS "${NM_OUTPUT}")
list(TRANSFORM ALL_SYMBOLS STRIP)
# Multiple inputs add filename headings; only accept symbol-name lines.
list(FILTER ALL_SYMBOLS INCLUDE REGEX "^[^ \t]+$")
list(FILTER ALL_SYMBOLS EXCLUDE REGEX ":$")

# Filter compiler/runtime, SDK, C++ and debug symbols before deduplication.
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^(__imp_|__real[@_]|__xmm@|__ymm@|_?GLOBAL__|DW\\.|_fltused|_tls_|__security|__guard|GS_|_RTC|\\?|^\\.|___|CLSID_|IID_|GUID_|KSPROPERTYSETID_|MF[A-Z_]|MR_|ME[A-Z]|IN6_|_CT\\?|_CTA[0-9]|_TI[0-9]|__local_stdio|__sanitizer_|_Avx2|_Check_memory|printf|fprintf|snprintf|vsnprintf|vfprintf|vsprintf|sprintf|malloc|calloc|realloc|free|memcpy|memmove|memset|strlen|strcmp|strncmp|strcpy|strncpy|abort|exit|_Exit)$")
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^\\?.*")
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^\\..*")
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^__sanitizer_.*")
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^__asan_.*")
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^__ubsan_.*")
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^__llvm_.*")
# llvm-nm can expose section/address tokens as bare symbols on Windows. They
# are not linker exports and make lld reject the generated .def file.
list(FILTER ALL_SYMBOLS EXCLUDE REGEX "^[A-Za-z]$|^a[0-9]+$")
list(REMOVE_DUPLICATES ALL_SYMBOLS)
list(SORT ALL_SYMBOLS)
list(LENGTH ALL_SYMBOLS EXPORT_COUNT)
list(TRANSFORM ALL_SYMBOLS PREPEND "    ")
list(JOIN ALL_SYMBOLS "\n" _def_exports)
file(WRITE "${OUTPUT_FILE}" "LIBRARY ${LIBRARY_NAME}\nEXPORTS\n${_def_exports}\n")
