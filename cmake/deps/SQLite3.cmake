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
#   - _asciichat_sqlite3_found: Whether SQLite3 was found
#   - _asciichat_sqlite3_link: Libraries to link against
#   - _asciichat_sqlite3_include: Include directories
# =============================================================================

# =============================================================================
# iOS build: Use system SQLite3 (included in iOS SDK)
# =============================================================================
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}SQLite3${ColorReset} (iOS system library)...")

    # SQLite3 is included in iOS SDK, no build needed
    # Find it via system paths
    find_library(_asciichat_sqlite3_link sqlite3)
    find_path(_asciichat_sqlite3_include sqlite3.h)

    if(_asciichat_sqlite3_link AND _asciichat_sqlite3_include)
        set(_asciichat_sqlite3_found TRUE)
        message(STATUS "${BoldGreen}✓${ColorReset} SQLite3 found (iOS system library): ${_asciichat_sqlite3_link}")
    else()
        message(FATAL_ERROR "SQLite3 not found in iOS SDK")
    endif()

    return()
endif()

# =============================================================================
# Handle musl builds - SQLite3 is built from source at configure time
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}SQLite3${ColorReset} from source...")
    include(ExternalProject)

    set(SQLITE3_PREFIX "${MUSL_DEPS_DIR_STATIC}/sqlite3")
    set(SQLITE3_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/sqlite3-build")
    set(SQLITE3_SOURCE_DIR "${SQLITE3_BUILD_DIR}/src/sqlite3")
    set(_sqlite3_signature "${MUSL_TARGET};${ASCIICHAT_CLANG_EXECUTABLE};musl-headers-v5;fcntl64=fcntl")
    set(_sqlite3_signature_file "${SQLITE3_PREFIX}/.asciichat-musl-signature")
    # Reject archives produced before the musl fcntl compatibility flags were
    # applied.  The cache is shared across configure runs, so the signature
    # alone cannot detect an archive that was interrupted or built from an
    # older source checkout.
    find_program(_asciichat_sqlite3_nm NAMES llvm-nm nm)
    if(EXISTS "${SQLITE3_PREFIX}/lib/libsqlite3.a" AND _asciichat_sqlite3_nm)
        execute_process(
            COMMAND "${_asciichat_sqlite3_nm}" -u "${SQLITE3_PREFIX}/lib/libsqlite3.a"
            OUTPUT_VARIABLE _sqlite3_undefined_symbols
            ERROR_QUIET
        )
        if(_sqlite3_undefined_symbols MATCHES "(^|[ \r\n])[^\r\n]*fcntl64([ \r\n]|$)")
            message(STATUS "  SQLite3 cache contains a glibc fcntl64 reference; rebuilding")
            file(REMOVE "${SQLITE3_PREFIX}/lib/libsqlite3.a")
        endif()
    endif()
    if(EXISTS "${SQLITE3_PREFIX}/lib/libsqlite3.a" AND EXISTS "${_sqlite3_signature_file}")
        file(READ "${_sqlite3_signature_file}" _sqlite3_cached_signature)
        if(NOT _sqlite3_cached_signature STREQUAL "${_sqlite3_signature}")
            message(STATUS "  SQLite3 cache signature changed; rebuilding the musl archive")
            file(REMOVE "${SQLITE3_PREFIX}/lib/libsqlite3.a")
        endif()
    elseif(EXISTS "${SQLITE3_PREFIX}/lib/libsqlite3.a")
        message(STATUS "  SQLite3 cache has no musl signature; rebuilding the archive")
        file(REMOVE "${SQLITE3_PREFIX}/lib/libsqlite3.a")
    endif()
    file(WRITE "${_sqlite3_signature_file}" "${_sqlite3_signature}")

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

        # Prepare the wrapper during configure; compilation and archiving are
        # owned by Ninja through ExternalProject_Add below.
        file(READ "${SQLITE3_SOURCE_DIR}/sqlite3.c" _sqlite3_source)
        string(REPLACE
            "#if defined(__GNUC__) && !defined(_GNU_SOURCE)"
            "#if defined(__GNUC__) && !defined(_GNU_SOURCE) && !defined(__linux__)"
            _sqlite3_source "${_sqlite3_source}")
        file(WRITE "${SQLITE3_SOURCE_DIR}/sqlite3.c" "${_sqlite3_source}")
        # Keep the compatibility mapping local to this third-party translation
        # unit.  Some SQLite feature/configuration combinations still emit the
        # large-file entry point as fcntl64 even when the compiler command line
        # undefines the glibc feature macros; the musl ABI provides it as fcntl.
        set(_sqlite3_musl_wrapper "${SQLITE3_BUILD_DIR}/sqlite3-musl.c")
        file(WRITE "${_sqlite3_musl_wrapper}"
            "#define fcntl64 fcntl\n#include \"${SQLITE3_SOURCE_DIR}/sqlite3.c\"\n")
        ExternalProject_Add(asciichat-sqlite3-musl
            SOURCE_DIR "${SQLITE3_SOURCE_DIR}"
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ${CMAKE_COMMAND}
                "-DSQLITE3_CC:FILEPATH=${ASCIICHAT_CLANG_EXECUTABLE}"
                "-DSQLITE3_AR:FILEPATH=${CMAKE_AR}"
                "-DSQLITE3_TARGET_FLAG:STRING=${ASCIICHAT_MUSL_TARGET_FLAG}"
                "-DSQLITE3_CFLAGS:STRING=${MUSL_KERNEL_CFLAGS}"
                "-DSQLITE3_WRAPPER:FILEPATH=${_sqlite3_musl_wrapper}"
                "-DSQLITE3_OBJECT:FILEPATH=${SQLITE3_BUILD_DIR}/sqlite3.o"
                "-DSQLITE3_ARCHIVE:FILEPATH=${SQLITE3_PREFIX}/lib/libsqlite3.a"
                "-DSQLITE3_SOURCE_DIR:PATH=${SQLITE3_SOURCE_DIR}"
                "-DSQLITE3_PREFIX:PATH=${SQLITE3_PREFIX}"
                "-DSQLITE3_SIGNATURE_FILE:FILEPATH=${_sqlite3_signature_file}"
                "-DSQLITE3_SIGNATURE:STRING=${_sqlite3_signature}"
                -P "${CMAKE_SOURCE_DIR}/cmake/deps/BuildSQLite3Musl.cmake"
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${SQLITE3_PREFIX}/lib/libsqlite3.a"
            LOG_BUILD TRUE
            LOG_OUTPUT_ON_FAILURE TRUE)
        message(STATUS "  ${BoldGreen}SQLite3${ColorReset} build scheduled through ExternalProject")
    else()
        message(STATUS "  ${BoldBlue}SQLite3${ColorReset} library found in cache: ${BoldMagenta}${SQLITE3_PREFIX}/lib/libsqlite3.a${ColorReset}")
    endif()

    if(NOT TARGET asciichat-sqlite3)
        add_library(asciichat-sqlite3 STATIC IMPORTED GLOBAL)
        set_target_properties(asciichat-sqlite3 PROPERTIES
            IMPORTED_LOCATION "${SQLITE3_PREFIX}/lib/libsqlite3.a"
            INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_PREFIX}/include")
    endif()
    if(TARGET asciichat-sqlite3-musl)
        add_dependencies(asciichat-sqlite3 asciichat-sqlite3-musl)
    endif()
    set(_asciichat_sqlite3_found TRUE)
    set(_asciichat_sqlite3_link asciichat-sqlite3)
    set(_asciichat_sqlite3_include "${SQLITE3_PREFIX}/include")
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
        set(_asciichat_sqlite3_found TRUE)
        include(${CMAKE_SOURCE_DIR}/cmake/utils/SelectLibraryConfig.cmake)
        asciichat_select_library_config(
            RELEASE_LIB ${SQLITE3_LIB_RELEASE}
            DEBUG_LIB   ${SQLITE3_LIB_DEBUG}
            OUTPUT      _asciichat_sqlite3_link
        )
        set(_asciichat_sqlite3_include "${SQLITE3_INC}")
        if(NOT TARGET asciichat-sqlite3)
            add_library(asciichat-sqlite3 INTERFACE)
            if(TARGET SQLite3::SQLite3)
                target_link_libraries(asciichat-sqlite3 INTERFACE SQLite3::SQLite3)
            else()
                target_link_libraries(asciichat-sqlite3 INTERFACE "${_asciichat_sqlite3_link}")
            endif()
            target_include_directories(asciichat-sqlite3 INTERFACE "${SQLITE3_INC}")
        endif()
        message(STATUS "Found ${BoldGreen}SQLite3${ColorReset} via vcpkg: ${SQLITE3_LIB_RELEASE}${SQLITE3_LIB_DEBUG}")
        return()
    endif()
endif()

# The vcpkg toolchain can be loaded before ascii-chat establishes its
# persistent install root. Prefer the explicitly configured dependency cache
# before falling back to pkg-config, which otherwise returns only `sqlite3`
# and loses the absolute Windows library path.
if(WIN32 AND ASCIICHAT_DEPS_CACHE_ROOT AND VCPKG_TARGET_TRIPLET)
    find_library(_asciichat_sqlite3_release NAMES sqlite3 libsqlite3
        PATHS "${ASCIICHAT_DEPS_CACHE_ROOT}/vcpkg/${VCPKG_TARGET_TRIPLET}/lib"
        NO_DEFAULT_PATH)
    find_library(_asciichat_sqlite3_debug NAMES sqlite3 libsqlite3
        PATHS "${ASCIICHAT_DEPS_CACHE_ROOT}/vcpkg/${VCPKG_TARGET_TRIPLET}/debug/lib"
        NO_DEFAULT_PATH)
    find_path(_asciichat_sqlite3_include NAMES sqlite3.h
        PATHS "${ASCIICHAT_DEPS_CACHE_ROOT}/vcpkg/${VCPKG_TARGET_TRIPLET}/include"
        NO_DEFAULT_PATH)
    if(_asciichat_sqlite3_release AND _asciichat_sqlite3_debug AND _asciichat_sqlite3_include)
        include(${CMAKE_SOURCE_DIR}/cmake/utils/SelectLibraryConfig.cmake)
        asciichat_select_library_config(
            RELEASE_LIB ${_asciichat_sqlite3_release}
            DEBUG_LIB   ${_asciichat_sqlite3_debug}
            OUTPUT      _asciichat_sqlite3_link
        )
        set(_asciichat_sqlite3_found TRUE)
        set(_asciichat_sqlite3_include "${_asciichat_sqlite3_include}")
        if(NOT TARGET asciichat-sqlite3)
            add_library(asciichat-sqlite3 INTERFACE)
            target_link_libraries(asciichat-sqlite3 INTERFACE "${_asciichat_sqlite3_link}")
            target_include_directories(asciichat-sqlite3 INTERFACE "${_asciichat_sqlite3_include}")
        endif()
        message(STATUS "Found ${BoldGreen}SQLite3${ColorReset} in dependency cache: ${_asciichat_sqlite3_release}${_asciichat_sqlite3_debug}")
        return()
    endif()
endif()

# =============================================================================
# Fallback: System SQLite3 via pkg-config or find_package
# =============================================================================

# Try pkg-config first (more reliable for getting library paths)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(_asciichat_sqlite3_pkg QUIET IMPORTED_TARGET sqlite3)
    if(_asciichat_sqlite3_pkg_FOUND)
        if(TARGET PkgConfig::_asciichat_sqlite3_pkg)
            set(_asciichat_sqlite3_link PkgConfig::_asciichat_sqlite3_pkg)
            if(NOT TARGET asciichat-sqlite3)
                add_library(asciichat-sqlite3 INTERFACE)
                target_link_libraries(asciichat-sqlite3 INTERFACE PkgConfig::_asciichat_sqlite3_pkg)
            endif()
        endif()
        # pkg-config returns a bare `-lsqlite3` on Windows even when it has
        # resolved the actual vcpkg library.  Preserve the absolute path so
        # clang/lld does not turn it into the invalid `-lsqlite3.lib`.
        if(WIN32 AND DEFINED pkgcfg_lib__asciichat_sqlite3_pkg_sqlite3
           AND EXISTS "${pkgcfg_lib__asciichat_sqlite3_pkg_sqlite3}")
            set(_asciichat_sqlite3_link "${pkgcfg_lib__asciichat_sqlite3_pkg_sqlite3}")
        endif()
        message(STATUS "Found ${BoldGreen}SQLite3${ColorReset} via pkg-config: ${_asciichat_sqlite3_pkg_VERSION}")
        return()
    endif()
endif()

# Try CMake's built-in FindSQLite3
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    set(_asciichat_sqlite3_found TRUE)
    set(_asciichat_sqlite3_include ${SQLite3_INCLUDE_DIRS})
    # Check if the target exists; if not, use the library path directly
    if(TARGET SQLite3::SQLite3)
        set(_asciichat_sqlite3_link SQLite3::SQLite3)
    else()
        set(_asciichat_sqlite3_link ${SQLite3_LIBRARIES})
    endif()
    if(NOT TARGET asciichat-sqlite3)
        add_library(asciichat-sqlite3 INTERFACE)
        target_link_libraries(asciichat-sqlite3 INTERFACE ${_asciichat_sqlite3_link})
        if(SQLite3_INCLUDE_DIRS)
            target_include_directories(asciichat-sqlite3 INTERFACE "${SQLite3_INCLUDE_DIRS}")
        endif()
    endif()
    message(STATUS "Found ${BoldGreen}SQLite3${ColorReset} via find_package: ${SQLite3_VERSION}")
    return()
endif()

# Last resort: just link to sqlite3 and hope it's in the system path
set(_asciichat_sqlite3_found TRUE)
set(_asciichat_sqlite3_link sqlite3)
set(_asciichat_sqlite3_include "")
if(NOT TARGET asciichat-sqlite3)
    add_library(asciichat-sqlite3 INTERFACE)
    target_link_libraries(asciichat-sqlite3 INTERFACE sqlite3)
endif()
message(STATUS "Using system ${BoldYellow}SQLite3${ColorReset} (assuming available)")
