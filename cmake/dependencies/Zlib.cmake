# =============================================================================
# zlib Compression Library Configuration
# =============================================================================
# Finds and configures zlib compression library (required by libwebsockets)
#
# Platform-specific dependency management:
#   - musl: Built from source in this file with USE_MUSL check
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Outputs:
#   - ZLIB_FOUND - Whether zlib was found/configured
#   - ZLIB_LIBRARY - Library path
#   - ZLIB_INCLUDE_DIR - Include directory
#   - ZLIB::zlib - Imported target (if created)
# =============================================================================

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}zlib${ColorReset} from source (musl)...")

    set(ZLIB_PREFIX "${MUSL_DEPS_DIR_STATIC}/zlib")
    set(ZLIB_INCLUDE_DIR "${ZLIB_PREFIX}/include")
    set(ZLIB_LIBRARY "${ZLIB_PREFIX}/lib/libz.a")

    if(NOT EXISTS "${ZLIB_LIBRARY}")
        message(STATUS "  zlib library not found in cache, will build from source")

        # Download zlib source manually first - GitHub archive endpoint has non-deterministic hashes
        # So we download, verify, and let ExternalProject use the cached file
        set(ZLIB_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/zlib-src")
        set(ZLIB_TARBALL "${ZLIB_DOWNLOAD_DIR}/zlib-1.3.1.tar.gz")
        set(ZLIB_EXTRACTED "${ZLIB_DOWNLOAD_DIR}/zlib-1.3.1")

        file(MAKE_DIRECTORY "${ZLIB_DOWNLOAD_DIR}")

        if(NOT EXISTS "${ZLIB_EXTRACTED}")
            if(NOT EXISTS "${ZLIB_TARBALL}")
                message(STATUS "  Downloading zlib 1.3.1...")
                # Use official zlib source from GitHub releases (not archive endpoint)
                file(DOWNLOAD
                    "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz"
                    "${ZLIB_TARBALL}"
                    TIMEOUT 30
                    STATUS DOWNLOAD_STATUS
                    SHOW_PROGRESS
                )
                list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
                if(NOT STATUS_CODE EQUAL 0)
                    list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                    message(WARNING "Failed to download from releases endpoint: ${ERROR_MSG}")
                    message(STATUS "  Attempting fallback: downloading from archive endpoint...")
                    # Fallback to archive endpoint without hash verification
                    file(DOWNLOAD
                        "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz"
                        "${ZLIB_TARBALL}"
                        TIMEOUT 30
                        STATUS DOWNLOAD_STATUS
                        SHOW_PROGRESS
                    )
                    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
                    if(NOT STATUS_CODE EQUAL 0)
                        list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                        message(FATAL_ERROR "Failed to download zlib from both endpoints: ${ERROR_MSG}")
                    endif()
                endif()
            endif()

            message(STATUS "  Extracting zlib...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${ZLIB_TARBALL}"
                WORKING_DIRECTORY "${ZLIB_DOWNLOAD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
                OUTPUT_VARIABLE EXTRACT_OUTPUT
                ERROR_VARIABLE EXTRACT_ERROR
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract zlib: ${EXTRACT_ERROR}")
            endif()
        endif()

        ExternalProject_Add(zlib-musl
            SOURCE_DIR "${ZLIB_EXTRACTED}"
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            STAMP_DIR ${MUSL_DEPS_DIR_STATIC}/zlib-build/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} <SOURCE_DIR>/configure --prefix=${ZLIB_PREFIX} --static
            BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${ZLIB_LIBRARY}
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}zlib${ColorReset} library found in cache: ${BoldMagenta}${ZLIB_LIBRARY}${ColorReset}")
        add_custom_target(zlib-musl)
    endif()

    set(ZLIB_FOUND TRUE PARENT_SCOPE)
    set(ZLIB_LIBRARY "${ZLIB_LIBRARY}" PARENT_SCOPE)
    set(ZLIB_INCLUDE_DIR "${ZLIB_INCLUDE_DIR}" PARENT_SCOPE)

    # Create imported target for zlib to match system find_package behavior
    if(NOT TARGET ZLIB::zlib)
        add_library(ZLIB::zlib STATIC IMPORTED GLOBAL)
        set_target_properties(ZLIB::zlib PROPERTIES
            IMPORTED_LOCATION "${ZLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIR}"
        )
    endif()

    # Create placeholder directories so CMake validation doesn't fail at configure time
    file(MAKE_DIRECTORY "${ZLIB_INCLUDE_DIR}")

    message(STATUS "${BoldGreen}✓${ColorReset} zlib (musl): ${ZLIB_LIBRARY}")
    return()
endif()

# =============================================================================
# Non-musl: Use system package or pkg-config
# =============================================================================

# Try to find zlib via CMake config first, then fall back to pkg-config
find_package(ZLIB QUIET CONFIG)

if(NOT ZLIB_FOUND)
    # Fall back to pkg-config if CMake config not found
    include(FindPkgConfig)
    pkg_check_modules(ZLIB REQUIRED zlib)

    # Create imported target for compatibility
    if(NOT TARGET ZLIB::zlib)
        add_library(ZLIB::zlib INTERFACE IMPORTED)
        target_include_directories(ZLIB::zlib INTERFACE ${ZLIB_INCLUDE_DIRS})
        target_link_libraries(ZLIB::zlib INTERFACE ${ZLIB_LIBRARIES})
    endif()
endif()

message(STATUS "${BoldGreen}✓${ColorReset} zlib found")
