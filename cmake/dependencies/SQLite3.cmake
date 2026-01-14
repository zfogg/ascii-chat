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
#   - For vcpkg: Uses vcpkg-installed SQLite3
#   - Otherwise: Uses system-installed SQLite3 via pkg-config or find_package
#
# Prerequisites (must be set before including this file):
#   - USE_VCPKG: Whether using vcpkg
#   - VCPKG_ROOT, VCPKG_LIB_PATH, etc.: vcpkg config (if USE_VCPKG=ON)
#
# Outputs (variables set by this file):
#   - SQLITE3_FOUND: Whether SQLite3 was found
#   - SQLITE3_LIBRARIES: Libraries to link against
#   - SQLITE3_INCLUDE_DIRS: Include directories
# =============================================================================

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
        if(SQLITE3_LIB_RELEASE AND SQLITE3_LIB_DEBUG)
            set(SQLITE3_LIBRARIES optimized ${SQLITE3_LIB_RELEASE} debug ${SQLITE3_LIB_DEBUG})
        elseif(SQLITE3_LIB_RELEASE)
            set(SQLITE3_LIBRARIES ${SQLITE3_LIB_RELEASE})
        else()
            set(SQLITE3_LIBRARIES ${SQLITE3_LIB_DEBUG})
        endif()
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
