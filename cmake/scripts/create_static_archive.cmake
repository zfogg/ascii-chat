# =============================================================================
# create_static_archive.cmake
# =============================================================================
# Custom static archive creator that eliminates basename collisions
#
# This script is invoked by CMake to create libasciichat.a from OBJECT files.
# It solves the duplicate object file problem by encoding full paths into
# object file basenames before archiving.
#
# Problem:
#   - ar only stores basenames: common.c.o, transport.c.o, etc.
#   - Multiple modules have files with same names
#   - Results in duplicates in archive (3.9MB → should be 1.5MB)
#
# Solution:
#   - Encode full path in basename: util_common.c.o, crypto_common.c.o
#   - Each object file gets a unique name in the archive
#   - No duplicates, smaller library size
#
# Usage:
#   cmake -DAR=${CMAKE_AR} -DRANLIB=${CMAKE_RANLIB} \
#         -DOUTPUT=/path/to/lib.a -DOBJECT_LIST_FILE=/path/to/objects.txt \
#         -DCMAKE_CURRENT_BINARY_DIR=/path/to/build \
#         -P create_static_archive.cmake
# =============================================================================

if(NOT DEFINED AR)
    message(FATAL_ERROR "AR not defined (pass -DAR=/path/to/ar)")
endif()

if(NOT DEFINED RANLIB)
    message(FATAL_ERROR "RANLIB not defined (pass -DRANLIB=/path/to/ranlib)")
endif()

if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT not defined (pass -DOUTPUT=/path/to/lib.a)")
endif()

if(NOT DEFINED OBJECT_LIST_FILE)
    message(FATAL_ERROR "OBJECT_LIST_FILE not defined (pass -DOBJECT_LIST_FILE=/path/to/objects.txt)")
endif()

if(NOT EXISTS "${OBJECT_LIST_FILE}")
    message(FATAL_ERROR "OBJECT_LIST_FILE does not exist: ${OBJECT_LIST_FILE}")
endif()

# Read object file list from generated file
file(READ "${OBJECT_LIST_FILE}" OBJECTS_STR)
# The file is already semicolon-separated (no newlines), so just use it as a list
set(OBJECT_LIST "${OBJECTS_STR}")
# Remove empty entries
list(REMOVE_ITEM OBJECT_LIST "")

# Filter out precompiled headers (.pch files) - these are huge and not needed in archives
set(FILTERED_OBJECTS "")
foreach(OBJ ${OBJECT_LIST})
    # Skip .pch files
    if(NOT OBJ MATCHES "\\.pch$")
        list(APPEND FILTERED_OBJECTS "${OBJ}")
    endif()
endforeach()
set(OBJECT_LIST "${FILTERED_OBJECTS}")

# Create temporary directory for renamed objects
set(TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/static_archive_temp")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

list(LENGTH OBJECT_LIST OBJECT_COUNT)
message(STATUS "Creating static archive with unique object names")
message(STATUS "  Output: ${OUTPUT}")
message(STATUS "  Object count: ${OBJECT_COUNT} files (PCH filtered out)")
message(STATUS "  First object: ${CMAKE_MATCH_1}")

set(RENAMED_OBJECTS "")
set(DEBUG_COUNT 0)

foreach(OBJ_FILE ${OBJECT_LIST})
    # Get absolute path to object file
    get_filename_component(OBJ_ABS "${OBJ_FILE}" ABSOLUTE)

    # Extract just the module name and relative path from CMakeFiles
    # .../CMakeFiles/ascii-chat-util.dir/lib/util/common.c.o → lib_util_common.c.o
    # .../CMakeFiles/ascii-chat-util.dir/Unity/unity_0_c.c.o → util_Unity_unity_0_c.c.o (needs prefix!)
    # This avoids embedding full build paths in the archive (security/privacy)
    if(OBJ_ABS MATCHES "/CMakeFiles/ascii-chat-([^/]+)\\.dir/(.*)")
        set(MODULE_NAME "${CMAKE_MATCH_1}")
        set(SOURCE_PATH "${CMAKE_MATCH_2}")

        # Clean path separators
        string(REPLACE "/" "_" SOURCE_PATH_CLEAN "${SOURCE_PATH}")
        string(REPLACE "\\" "_" SOURCE_PATH_CLEAN "${SOURCE_PATH_CLEAN}")

        # Only add module prefix for Unity build files (they share basenames across modules)
        # Regular source files have unique paths, so no prefix needed
        if(SOURCE_PATH MATCHES "^Unity/")
            set(UNIQUE_NAME "${MODULE_NAME}_${SOURCE_PATH_CLEAN}")
        else()
            set(UNIQUE_NAME "${SOURCE_PATH_CLEAN}")
        endif()

        # Debug first few matches
        math(EXPR DEBUG_COUNT "${DEBUG_COUNT} + 1")
        if(DEBUG_COUNT LESS 4)
            message(STATUS "  Renamed: ${SOURCE_PATH} -> ${UNIQUE_NAME}")
        endif()
    else()
        # Fallback: use basename (shouldn't happen for OBJECT libraries)
        get_filename_component(UNIQUE_NAME "${OBJ_FILE}" NAME)
        message(WARNING "Could not parse object path: ${OBJ_ABS}")
    endif()

    # Copy object file to temp directory with unique name
    set(RENAMED_OBJ "${TEMP_DIR}/${UNIQUE_NAME}")
    file(COPY_FILE "${OBJ_ABS}" "${RENAMED_OBJ}")

    list(APPEND RENAMED_OBJECTS "${RENAMED_OBJ}")
endforeach()

# Create archive from renamed objects
message(STATUS "  Running: ${AR} crs ${OUTPUT} <${CMAKE_MATCH_COUNT} objects>")
execute_process(
    COMMAND ${AR} crs "${OUTPUT}" ${RENAMED_OBJECTS}
    RESULT_VARIABLE AR_RESULT
    OUTPUT_VARIABLE AR_OUTPUT
    ERROR_VARIABLE AR_ERROR
)

if(NOT AR_RESULT EQUAL 0)
    message(FATAL_ERROR "ar failed with code ${AR_RESULT}: ${AR_ERROR}")
endif()

# Run ranlib to update symbol table
message(STATUS "  Running: ${RANLIB} ${OUTPUT}")
execute_process(
    COMMAND ${RANLIB} "${OUTPUT}"
    RESULT_VARIABLE RANLIB_RESULT
    OUTPUT_VARIABLE RANLIB_OUTPUT
    ERROR_VARIABLE RANLIB_ERROR
)

if(NOT RANLIB_RESULT EQUAL 0)
    message(FATAL_ERROR "ranlib failed with code ${RANLIB_RESULT}: ${RANLIB_ERROR}")
endif()

# Clean up temp directory
file(REMOVE_RECURSE "${TEMP_DIR}")

message(STATUS "Static archive created successfully: ${OUTPUT}")
