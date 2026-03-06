# =============================================================================
# zstd Compression Library Configuration
# =============================================================================
# Finds and configures zstd compression library using CMake's find_package()
#
# Platform-specific dependency management:
#   - iOS: Built from source with iOS cross-compilation
#   - musl: Built from source in this file with USE_MUSL check
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Outputs:
#   - zstd::zstd or zstd::libzstd - Imported target for linking
#   - ZSTD_FOUND - Whether zstd was found/configured
#   - ZSTD_LIBRARIES - Library paths
#   - ZSTD_INCLUDE_DIRS - Include directories
# =============================================================================

# =============================================================================
# iOS: Build from source
# =============================================================================
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}zstd${ColorReset} from source (iOS cross-compile)...")

    set(ZSTD_PREFIX "${IOS_DEPS_CACHE_DIR}/zstd")
    set(ZSTD_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/zstd-build")

    # Get iOS SDK path
    if(BUILD_IOS_SIM)
        execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
        execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    if(NOT EXISTS "${ZSTD_PREFIX}/lib/libzstd.a")
        message(STATUS "  zstd library not found in cache, will build from source")

        file(MAKE_DIRECTORY "${ZSTD_BUILD_DIR}")

        # Download zstd
        set(ZSTD_TARBALL "${ZSTD_BUILD_DIR}/zstd-1.5.7.tar.gz")
        if(NOT EXISTS "${ZSTD_TARBALL}")
            message(STATUS "  Downloading zstd 1.5.7...")
            file(DOWNLOAD
                "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"
                "${ZSTD_TARBALL}"
                EXPECTED_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
                SHOW_PROGRESS
            )
        endif()

        # Extract
        if(NOT EXISTS "${ZSTD_BUILD_DIR}/zstd-1.5.7")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${ZSTD_TARBALL}"
                WORKING_DIRECTORY "${ZSTD_BUILD_DIR}"
            )
        endif()

        # Build
        execute_process(
            COMMAND bash -c "make -j lib-release CC=clang 'CFLAGS=-arch arm64 -isysroot ${IOS_SDK_PATH} -miphoneos-version-min=16.0 -fPIC' PREFIX=${ZSTD_PREFIX}"
            WORKING_DIRECTORY "${ZSTD_BUILD_DIR}/zstd-1.5.7"
            RESULT_VARIABLE ZSTD_BUILD_RESULT
            ERROR_VARIABLE ZSTD_BUILD_ERROR
        )
        if(NOT ZSTD_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build zstd for iOS:\n${ZSTD_BUILD_ERROR}")
        endif()

        # Install
        execute_process(
            COMMAND make install PREFIX=${ZSTD_PREFIX}
            WORKING_DIRECTORY "${ZSTD_BUILD_DIR}/zstd-1.5.7"
            RESULT_VARIABLE ZSTD_INSTALL_RESULT
            ERROR_VARIABLE ZSTD_INSTALL_ERROR
        )
        if(NOT ZSTD_INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install zstd:\n${ZSTD_INSTALL_ERROR}")
        endif()

        message(STATUS "  ${BoldBlue}zstd${ColorReset} built and installed successfully")
    else()
        message(STATUS "  ${BoldBlue}zstd${ColorReset} library found in cache: ${BoldMagenta}${ZSTD_PREFIX}/lib/libzstd.a${ColorReset}")
    endif()

    set(ZSTD_LIBRARIES "${ZSTD_PREFIX}/lib/libzstd.a")
    set(ZSTD_INCLUDE_DIRS "${ZSTD_PREFIX}/include")

    # Create placeholder directories
    file(MAKE_DIRECTORY "${ZSTD_PREFIX}/include" "${ZSTD_PREFIX}/lib")

    # Create imported target
    if(NOT TARGET zstd::libzstd)
        add_library(zstd::libzstd STATIC IMPORTED GLOBAL)
        set_target_properties(zstd::libzstd PROPERTIES
            IMPORTED_LOCATION "${ZSTD_PREFIX}/lib/libzstd.a"
            INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_PREFIX}/include"
        )
    endif()

    set(ZSTD_FOUND TRUE)
    return()
endif()

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}zstd${ColorReset} from source (musl)...")

    set(ZSTD_PREFIX "${MUSL_DEPS_DIR_STATIC}/zstd")
    set(ZSTD_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/zstd-build")

    # Only add external project if library doesn't exist
    if(NOT EXISTS "${ZSTD_PREFIX}/lib/libzstd.a")
        message(STATUS "  zstd library not found in cache, will build from source")
        ExternalProject_Add(zstd-musl
            URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
            URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${ZSTD_BUILD_DIR}
            STAMP_DIR ${ZSTD_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND ""
            BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j -C <SOURCE_DIR> lib-release PREFIX=${ZSTD_PREFIX}
            INSTALL_COMMAND make -C <SOURCE_DIR> install PREFIX=${ZSTD_PREFIX}
            BUILD_IN_SOURCE 1
            BUILD_BYPRODUCTS ${ZSTD_PREFIX}/lib/libzstd.a
            LOG_DOWNLOAD TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}zstd${ColorReset} library found in cache: ${BoldMagenta}${ZSTD_PREFIX}/lib/libzstd.a${ColorReset}")
        # Create a dummy target so dependencies can reference it
        add_custom_target(zstd-musl)
    endif()

    set(ZSTD_LIBRARIES "${ZSTD_PREFIX}/lib/libzstd.a")
    set(ZSTD_INCLUDE_DIRS "${ZSTD_PREFIX}/include")

    # Create placeholder directories so CMake validation doesn't fail at configure time
    file(MAKE_DIRECTORY "${ZSTD_PREFIX}/include")

    # Create imported target for zstd to match system find_package behavior
    if(NOT TARGET zstd::libzstd)
        add_library(zstd::libzstd STATIC IMPORTED GLOBAL)
        set_target_properties(zstd::libzstd PROPERTIES
            IMPORTED_LOCATION "${ZSTD_PREFIX}/lib/libzstd.a"
            INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_PREFIX}/include"
        )
    endif()

    set(ZSTD_FOUND TRUE)
    message(STATUS "${BoldGreen}✓${ColorReset} zstd (musl): ${ZSTD_PREFIX}/lib/libzstd.a")
    return()
endif()

# =============================================================================
# Non-musl: Use system package or pkg-config
# =============================================================================

# Try to find zstd via CMake config first, then fall back to pkg-config
find_package(zstd QUIET CONFIG)

if(NOT zstd_FOUND)
    # Fall back to pkg-config if CMake config not found
    include(FindPkgConfig)
    pkg_check_modules(zstd REQUIRED libzstd)

    # Create interface library for compatibility
    if(NOT TARGET zstd::zstd)
        add_library(zstd::zstd INTERFACE IMPORTED)
        target_include_directories(zstd::zstd INTERFACE ${zstd_INCLUDE_DIRS})
        target_link_libraries(zstd::zstd INTERFACE ${zstd_LIBRARIES})
    endif()
endif()

set(ZSTD_FOUND TRUE)

message(STATUS "${BoldGreen}✓${ColorReset} zstd found")
