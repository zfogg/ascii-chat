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
# Output:
#   - asciichat-bzip2 / asciichat::bzip2 - canonical consumer targets
# =============================================================================

# =============================================================================
# iOS: Build from source for iOS cross-compilation
# =============================================================================
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}bzip2${ColorReset} from source (iOS cross-compile)...")
    include(ExternalProject)

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

        ExternalProject_Add(asciichat-bzip2-ios
            SOURCE_DIR "${BZIP2_BUILD_DIR}/bzip2-1.0.8"
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ${CMAKE_COMMAND} -E env
                "CC=clang"
                "CFLAGS=-fPIC -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0"
                make -j
            INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${BZIP2_PREFIX}/lib" "${BZIP2_PREFIX}/include"
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${BZIP2_BUILD_DIR}/bzip2-1.0.8/libbz2.a" "${BZIP2_LIBRARY}"
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${BZIP2_BUILD_DIR}/bzip2-1.0.8/bzlib.h" "${BZIP2_INCLUDE_DIR}/bzlib.h"
            BUILD_BYPRODUCTS "${BZIP2_LIBRARY}"
            LOG_BUILD TRUE LOG_INSTALL TRUE LOG_OUTPUT_ON_FAILURE TRUE)
    else()
        message(STATUS "  ${BoldBlue}bzip2${ColorReset} library found in iOS cache: ${BoldMagenta}${BZIP2_LIBRARY}${ColorReset}")
    endif()

    # Create imported target
    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 STATIC IMPORTED GLOBAL)
        set_target_properties(BZip2::BZip2 PROPERTIES
            IMPORTED_LOCATION "${BZIP2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BZIP2_INCLUDE_DIR}"
        )
    endif()
    if(TARGET asciichat-bzip2-ios)
        add_dependencies(BZip2::BZip2 asciichat-bzip2-ios)
    endif()
    if(NOT TARGET asciichat-bzip2)
        add_library(asciichat-bzip2 INTERFACE)
        target_link_libraries(asciichat-bzip2 INTERFACE BZip2::BZip2)
    endif()
    if(NOT TARGET asciichat::bzip2)
        add_library(asciichat::bzip2 ALIAS asciichat-bzip2)
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
        ExternalProject_Add(asciichat-bzip2-musl
            URL https://www.sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
            URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${MUSL_DEPS_DIR_STATIC}/bzip2-build
            STAMP_DIR ${MUSL_DEPS_DIR_STATIC}/bzip2-build/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND ""
            BUILD_COMMAND bash -c "cd <SOURCE_DIR> && env CC=${ASCIICHAT_CLANG_EXECUTABLE} CFLAGS='${ASCIICHAT_MUSL_TARGET_FLAG} -fPIC' make -j"
            INSTALL_COMMAND bash -c "mkdir -p ${BZIP2_PREFIX}/lib ${BZIP2_PREFIX}/include && cp <SOURCE_DIR>/bzlib.h ${BZIP2_PREFIX}/include/ && cp <SOURCE_DIR>/libbz2.a ${BZIP2_PREFIX}/lib/"
            BUILD_BYPRODUCTS ${BZIP2_LIBRARY}
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}bzip2${ColorReset} library found in cache: ${BoldMagenta}${BZIP2_LIBRARY}${ColorReset}")
        add_custom_target(asciichat-bzip2-musl)
    endif()

    # Create imported target for bzip2 to match system find_package behavior
    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 STATIC IMPORTED GLOBAL)
        set_target_properties(BZip2::BZip2 PROPERTIES
            IMPORTED_LOCATION "${BZIP2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BZIP2_INCLUDE_DIR}"
        )
    endif()
    if(NOT TARGET asciichat-bzip2)
        add_library(asciichat-bzip2 INTERFACE)
        target_link_libraries(asciichat-bzip2 INTERFACE BZip2::BZip2)
    endif()
    if(NOT TARGET asciichat::bzip2)
        add_library(asciichat::bzip2 ALIAS asciichat-bzip2)
    endif()

    # Create placeholder directories so CMake validation doesn't fail at configure time
    file(MAKE_DIRECTORY "${BZIP2_INCLUDE_DIR}")

    message(STATUS "${BoldGreen}✓${ColorReset} bzip2 (musl): ${BZIP2_LIBRARY}")
    return()
endif()

# =============================================================================
# Non-musl: Use system package or pkg-config
# =============================================================================

# Windows vcpkg provides the authoritative static package.  Use CMake's
# module finder against that staged triplet; Strawberry Perl's pkg-config is
# not a usable fallback on the Windows runners.
if(WIN32)
    set(_asciichat_bzip2_vcpkg_root
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(NOT EXISTS "${_asciichat_bzip2_vcpkg_root}")
        set(_asciichat_bzip2_vcpkg_root
            "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    if(NOT EXISTS "${_asciichat_bzip2_vcpkg_root}/include/bzlib.h")
        message(FATAL_ERROR "Windows BZip2 vcpkg triplet is missing bzlib.h: ${_asciichat_bzip2_vcpkg_root}")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${_asciichat_bzip2_vcpkg_root}")
    find_package(BZip2 MODULE REQUIRED)
    if(NOT TARGET BZip2::BZip2)
        message(FATAL_ERROR "Windows BZip2 module did not provide BZip2::BZip2")
    endif()
    string(FIND "${BZIP2_INCLUDE_DIR}" "${_asciichat_bzip2_vcpkg_root}" _asciichat_bzip2_include_match)
    if(_asciichat_bzip2_include_match LESS 0)
        message(FATAL_ERROR "Windows BZip2 resolved outside the staged vcpkg triplet: ${BZIP2_INCLUDE_DIR}")
    endif()
else()
    # Try to find bzip2 via CMake config first, then fall back to pkg-config.
    find_package(BZip2 QUIET CONFIG)

    if(NOT TARGET BZip2::BZip2)
        include(FindPkgConfig)
        pkg_check_modules(_asciichat_bzip2 REQUIRED IMPORTED_TARGET bzip2)
        add_library(BZip2::BZip2 INTERFACE IMPORTED)
        target_link_libraries(BZip2::BZip2 INTERFACE PkgConfig::_asciichat_bzip2)
    endif()

endif()

# Some distro BZip2 config files report success without publishing the
# conventional target.  Normalize both discovery paths to one target.
if(NOT TARGET asciichat-bzip2)
    add_library(asciichat-bzip2 INTERFACE)
    target_link_libraries(asciichat-bzip2 INTERFACE BZip2::BZip2)
endif()
if(NOT TARGET asciichat::bzip2)
    add_library(asciichat::bzip2 ALIAS asciichat-bzip2)
endif()

message(STATUS "${BoldGreen}✓${ColorReset} bzip2 found")
