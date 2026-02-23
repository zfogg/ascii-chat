# Generate a Windows .def file from object files for DLL export
cmake_minimum_required(VERSION 3.16)

# Check required parameters
if(NOT DEFINED NM_TOOL)
    message(FATAL_ERROR "NM_TOOL is not defined")
endif()
if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is not defined")
endif()
if(NOT DEFINED MODULE_TARGETS)
    message(FATAL_ERROR "MODULE_TARGETS is not defined")
endif()
if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is not defined")
endif()
if(NOT DEFINED LIBRARY_NAME)
    message(FATAL_ERROR "LIBRARY_NAME is not defined")
endif()

# Convert MODULE_TARGETS from comma-separated string to list
string(REPLACE "," ";" MODULE_LIST "${MODULE_TARGETS}")
message(STATUS "Module targets: ${MODULE_LIST}")

# Find object files only from library modules (not from ascii-chat executable)
# Exclude deps/ directories as those may contain objects that aren't actually linked
set(OBJ_FILES)
foreach(module ${MODULE_LIST})
    message(STATUS "Searching for: ${BUILD_DIR}/CMakeFiles/${module}.dir/*.obj")
    file(GLOB_RECURSE module_objs "${BUILD_DIR}/CMakeFiles/${module}.dir/*.obj")

    # Filter out deps/ subdirectories
    set(filtered_objs)
    foreach(obj ${module_objs})
        string(FIND "${obj}" "/deps/" has_deps_slash)
        string(FIND "${obj}" "\\deps\\" has_deps_backslash)
        if(has_deps_slash EQUAL -1 AND has_deps_backslash EQUAL -1)
            list(APPEND filtered_objs "${obj}")
        endif()
    endforeach()

    list(LENGTH filtered_objs module_obj_count)
    message(STATUS "  Found ${module_obj_count} object files for ${module} (after excluding deps/)")
    list(APPEND OBJ_FILES ${filtered_objs})
endforeach()

if(NOT OBJ_FILES)
    message(FATAL_ERROR "No object files found for modules: ${MODULE_TARGETS}")
endif()

list(LENGTH OBJ_FILES obj_count)
message(STATUS "Scanning ${obj_count} object files from library modules")

message(STATUS "Generating .def file: ${OUTPUT_FILE}")

# Extract all public symbols from object files
set(ALL_SYMBOLS "")
foreach(OBJ_FILE ${OBJ_FILES})
    execute_process(
        COMMAND ${NM_TOOL} --extern-only --defined-only --print-file-name "${OBJ_FILE}"
        OUTPUT_VARIABLE NM_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Parse nm output and extract symbol names
    string(REGEX MATCHALL "[^\n]+ [TDBRCStdbrc] [^\n]+" SYMBOLS "${NM_OUTPUT}")
    foreach(SYMBOL_LINE ${SYMBOLS})
        string(REGEX REPLACE ".*[TDBRCStdbrc] ([^ \n]+)" "\\1" SYMBOL_NAME "${SYMBOL_LINE}")
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
