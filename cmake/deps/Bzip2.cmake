# =============================================================================
# bzip2 Compression Library Configuration
# =============================================================================
# Finds and configures bzip2 compression library (needed by freetype subproject)
#
# Platform-specific dependency management:
#   - musl: Built from source in this file with USE_MUSL check
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Outputs:
#   - BZIP2_FOUND - Whether bzip2 was found/configured
#   - BZIP2_LIBRARY - Library path
#   - BZIP2_INCLUDE_DIR - Include directory
#   - BZip2::BZip2 - Imported target (if created)
# =============================================================================

# =============================================================================
# iOS: Build from source for iOS cross-compilation
# =============================================================================
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}bzip2${ColorReset} from source (iOS cross-compile)...")

    set(BZIP2_PREFIX "${IOS_DEPS_CACHE_DIR}/bzip2")
    set(BZIP2_LIBRARY "${BZIP2_PREFIX}/lib/libbz2.a")
    set(BZIP2_INCLUDE_DIR "${BZIP2_PREFIX}/include")

    if(NOT EXISTS "${BZIP2_LIBRARY}")
        message(STATUS "  bzip2 library not found in cache, will build from source")

        set(BZIP2_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/bzip2-build")
        file(MAKE_DIRECTORY "${BZIP2_BUILD_DIR}")

        # Get iOS SDK path
        if(BUILD_IOS_SIM)
            execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        else()
            execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()

        message(STATUS "  Downloading bzip2 1.0.8...")
        file(DOWNLOAD
            "https://www.sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz"
            "${BZIP2_BUILD_DIR}/bzip2-1.0.8.tar.gz"
            EXPECTED_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
            TIMEOUT 30
            SHOW_PROGRESS
        )

        message(STATUS "  Extracting bzip2...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf bzip2-1.0.8.tar.gz
            WORKING_DIRECTORY "${BZIP2_BUILD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract bzip2")
        endif()

        message(STATUS "  Building bzip2 for iOS...")
        execute_process(
            COMMAND bash -c "cd '${BZIP2_BUILD_DIR}/bzip2-1.0.8' && \
                    env CC=clang \
                    CFLAGS='-fPIC -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0' \
                    make -j && \
                    mkdir -p '${BZIP2_PREFIX}/lib' '${BZIP2_PREFIX}/include' && \
                    cp libbz2.a '${BZIP2_PREFIX}/lib/' && \
                    cp bzlib.h '${BZIP2_PREFIX}/include/'"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "bzip2 iOS build failed:\n${BUILD_ERROR}")
        endif()
    else()
        message(STATUS "  ${BoldBlue}bzip2${ColorReset} library found in iOS cache: ${BoldMagenta}${BZIP2_LIBRARY}${ColorReset}")
    endif()

    set(BZIP2_FOUND TRUE)

    # Create imported target
    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 STATIC IMPORTED GLOBAL)
        set_target_properties(BZip2::BZip2 PROPERTIES
            IMPORTED_LOCATION "${BZIP2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BZIP2_INCLUDE_DIR}"
        )
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} bzip2 (iOS): ${BZIP2_LIBRARY}")
    return()
endif()

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}bzip2${ColorReset} from source (musl)...")

    set(BZIP2_PREFIX "${MUSL_DEPS_DIR_STATIC}/bzip2")
    set(BZIP2_LIBRARY "${BZIP2_PREFIX}/lib/libbz2.a")
    set(BZIP2_INCLUDE_DIR "${BZIP2_PREFIX}/include")

    if(NOT EXISTS "${BZIP2_LIBRARY}")
        ExternalProject_Add(bzip2-musl
            URL https://www.sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
            URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${MUSL_DEPS_DIR_STATIC}/bzip2-build
            STAMP_DIR ${MUSL_DEPS_DIR_STATIC}/bzip2-build/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND ""
            BUILD_COMMAND bash -c "cd <SOURCE_DIR> && env CC=${MUSL_GCC} CFLAGS=-fPIC make -j"
            INSTALL_COMMAND bash -c "mkdir -p ${BZIP2_PREFIX}/lib ${BZIP2_PREFIX}/include && cp <SOURCE_DIR>/bzlib.h ${BZIP2_PREFIX}/include/ && cp <SOURCE_DIR>/libbz2.a ${BZIP2_PREFIX}/lib/"
            BUILD_BYPRODUCTS ${BZIP2_LIBRARY}
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}bzip2${ColorReset} library found in cache: ${BoldMagenta}${BZIP2_LIBRARY}${ColorReset}")
        add_custom_target(bzip2-musl)
    endif()

    set(BZIP2_FOUND TRUE)
    set(BZIP2_LIBRARY "${BZIP2_LIBRARY}")
    set(BZIP2_INCLUDE_DIR "${BZIP2_INCLUDE_DIR}")

    # Create imported target for bzip2 to match system find_package behavior
    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 STATIC IMPORTED GLOBAL)
        set_target_properties(BZip2::BZip2 PROPERTIES
            IMPORTED_LOCATION "${BZIP2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BZIP2_INCLUDE_DIR}"
        )
    endif()

    # Create placeholder directories so CMake validation doesn't fail at configure time
    file(MAKE_DIRECTORY "${BZIP2_INCLUDE_DIR}")

    message(STATUS "${BoldGreen}✓${ColorReset} bzip2 (musl): ${BZIP2_LIBRARY}")
    return()
endif()

# =============================================================================
# Non-musl: Use system package or pkg-config
# =============================================================================

# Try to find bzip2 via CMake config first, then fall back to pkg-config
find_package(BZip2 QUIET CONFIG)

if(NOT BZIP2_FOUND)
    # Fall back to pkg-config if CMake config not found
    include(FindPkgConfig)
    pkg_check_modules(BZIP2 REQUIRED bzip2)

    # Create imported target for compatibility
    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 INTERFACE IMPORTED)
        target_include_directories(BZip2::BZip2 INTERFACE ${BZIP2_INCLUDE_DIRS})
        target_link_libraries(BZip2::BZip2 INTERFACE ${BZIP2_LIBRARIES})
    endif()
endif()

message(STATUS "${BoldGreen}✓${ColorReset} bzip2 found")
