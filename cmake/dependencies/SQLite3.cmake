# =============================================================================
# SQLite3 Library Configuration
# =============================================================================
# Finds and configures SQLite3 database library
#
# SQLite3 is used for:
#   - ACDS session storage
#   - Rate limiting backend
#
# Build strategy:
#   - For musl: Built from source in MuslDependencies.cmake
#   - For vcpkg: Uses vcpkg-installed SQLite3
#   - Otherwise: Uses system-installed SQLite3 via pkg-config or find_package
#
# Prerequisites (must be set before including this file):
#   - USE_VCPKG: Whether using vcpkg
#   - USE_MUSL: Whether using musl (skip system search if true)
#   - VCPKG_ROOT, VCPKG_LIB_PATH, etc.: vcpkg config (if USE_VCPKG=ON)
#
# Outputs (variables set by this file):
#   - SQLITE3_FOUND: Whether SQLite3 was found
#   - SQLITE3_LIBRARIES: Libraries to link against
#   - SQLITE3_INCLUDE_DIRS: Include directories
# =============================================================================

# =============================================================================
# Handle musl builds - SQLite3 is built from source at configure time
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}SQLite3${ColorReset} from source...")

    set(SQLITE3_PREFIX "${MUSL_DEPS_DIR_STATIC}/sqlite3")
    set(SQLITE3_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/sqlite3-build")
    set(SQLITE3_SOURCE_DIR "${SQLITE3_BUILD_DIR}/src/sqlite3")

    # Build SQLite3 synchronously at configure time if not cached
    if(NOT EXISTS "${SQLITE3_PREFIX}/lib/libsqlite3.a")
        message(STATUS "  SQLite3 library not found in cache, will build from source")
        message(STATUS "  This should be quick (SQLite is a single file)...")

        file(MAKE_DIRECTORY "${SQLITE3_BUILD_DIR}")
        file(MAKE_DIRECTORY "${SQLITE3_SOURCE_DIR}")

        # Download SQLite3 amalgamation (single-file distribution)
        set(SQLITE3_TARBALL "${SQLITE3_BUILD_DIR}/sqlite-amalgamation-3480000.zip")
        if(NOT EXISTS "${SQLITE3_TARBALL}")
            message(STATUS "  Downloading SQLite3 3.48.0...")
            file(DOWNLOAD
                "https://www.sqlite.org/2025/sqlite-amalgamation-3480000.zip"
                "${SQLITE3_TARBALL}"
                EXPECTED_HASH SHA256=d9a15a42db7c78f88fe3d3c5945acce2f4bfe9e4da9f685cd19f6ea1d40aa884
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download SQLite3: ${ERROR_MSG}")
            endif()
        endif()

        # Extract archive
        if(NOT EXISTS "${SQLITE3_SOURCE_DIR}/sqlite3.c")
            message(STATUS "  Extracting SQLite3...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xf "${SQLITE3_TARBALL}"
                WORKING_DIRECTORY "${SQLITE3_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract SQLite3 archive")
            endif()
            # Move from sqlite-amalgamation-3480000/ to src/sqlite3/
            file(RENAME "${SQLITE3_BUILD_DIR}/sqlite-amalgamation-3480000" "${SQLITE3_SOURCE_DIR}")
        endif()

        # Compile SQLite3 to static library
        message(STATUS "  Building SQLite3...")
        file(MAKE_DIRECTORY "${SQLITE3_PREFIX}/lib")
        file(MAKE_DIRECTORY "${SQLITE3_PREFIX}/include")

        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                CC=${MUSL_GCC}
                REALGCC=${REAL_GCC}
                CFLAGS=${MUSL_KERNEL_CFLAGS}
                ${MUSL_GCC} -c
                -DSQLITE_THREADSAFE=1
                -DSQLITE_ENABLE_FTS5
                -DSQLITE_ENABLE_RTREE
                -DSQLITE_ENABLE_JSON1
                -O3
                -fPIC
                "${SQLITE3_SOURCE_DIR}/sqlite3.c"
                -o "${SQLITE3_BUILD_DIR}/sqlite3.o"
            RESULT_VARIABLE COMPILE_RESULT
            OUTPUT_VARIABLE COMPILE_OUTPUT
            ERROR_VARIABLE COMPILE_ERROR
        )
        if(NOT COMPILE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to compile SQLite3:\n${COMPILE_ERROR}")
        endif()

        # Create static library
        execute_process(
            COMMAND ${CMAKE_AR} rcs "${SQLITE3_PREFIX}/lib/libsqlite3.a" "${SQLITE3_BUILD_DIR}/sqlite3.o"
            RESULT_VARIABLE AR_RESULT
            OUTPUT_VARIABLE AR_OUTPUT
            ERROR_VARIABLE AR_ERROR
        )
        if(NOT AR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to create SQLite3 static library:\n${AR_ERROR}")
        endif()

        # Copy headers
        file(COPY "${SQLITE3_SOURCE_DIR}/sqlite3.h" DESTINATION "${SQLITE3_PREFIX}/include")
        file(COPY "${SQLITE3_SOURCE_DIR}/sqlite3ext.h" DESTINATION "${SQLITE3_PREFIX}/include")

        message(STATUS "  ${BoldGreen}SQLite3${ColorReset} built and cached successfully")
    else()
        message(STATUS "  ${BoldBlue}SQLite3${ColorReset} library found in cache: ${BoldMagenta}${SQLITE3_PREFIX}/lib/libsqlite3.a${ColorReset}")
    endif()

    set(SQLITE3_FOUND TRUE PARENT_SCOPE)
    set(SQLITE3_LIBRARIES "${SQLITE3_PREFIX}/lib/libsqlite3.a" PARENT_SCOPE)
    set(SQLITE3_INCLUDE_DIRS "${SQLITE3_PREFIX}/include" PARENT_SCOPE)
    return()
endif()

# =============================================================================
# Try vcpkg first if enabled
# =============================================================================
if(USE_VCPKG AND VCPKG_ROOT)
    find_library(SQLITE3_LIB_RELEASE NAMES sqlite3 libsqlite3
                 PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
    find_library(SQLITE3_LIB_DEBUG NAMES sqlite3 libsqlite3
                 PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
    find_path(SQLITE3_INC NAMES sqlite3.h
              PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

    if(SQLITE3_LIB_RELEASE OR SQLITE3_LIB_DEBUG)
        set(SQLITE3_FOUND TRUE)
        include(${CMAKE_SOURCE_DIR}/cmake/utils/SelectLibraryConfig.cmake)
        asciichat_select_library_config(
            RELEASE_LIB ${SQLITE3_LIB_RELEASE}
            DEBUG_LIB   ${SQLITE3_LIB_DEBUG}
            OUTPUT      SQLITE3_LIBRARIES
        )
        set(SQLITE3_INCLUDE_DIRS "${SQLITE3_INC}")
        message(STATUS "Found ${BoldGreen}SQLite3${ColorReset} via vcpkg: ${SQLITE3_LIB_RELEASE}${SQLITE3_LIB_DEBUG}")
        return()
    endif()
endif()

# =============================================================================
# Fallback: System SQLite3 via CMake's find_package or pkg-config
# =============================================================================

# Try CMake's built-in FindSQLite3
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    set(SQLITE3_FOUND TRUE)
    set(SQLITE3_LIBRARIES SQLite::SQLite3)
    set(SQLITE3_INCLUDE_DIRS ${SQLite3_INCLUDE_DIRS})
    message(STATUS "Found ${BoldGreen}SQLite3${ColorReset} via find_package: ${SQLite3_VERSION}")
    return()
endif()

# Try pkg-config
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 QUIET sqlite3)
    if(SQLITE3_FOUND)
        message(STATUS "Found ${BoldGreen}SQLite3${ColorReset} via pkg-config: ${SQLITE3_VERSION}")
        return()
    endif()
endif()

# Last resort: just link to sqlite3 and hope it's in the system path
set(SQLITE3_FOUND TRUE)
set(SQLITE3_LIBRARIES sqlite3)
set(SQLITE3_INCLUDE_DIRS "")
message(STATUS "Using system ${BoldYellow}SQLite3${ColorReset} (assuming available)")
