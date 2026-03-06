# =============================================================================
# Fontconfig Dependency
# =============================================================================
# Cross-platform configuration for fontconfig font resolution
#
# For iOS: fontconfig is built from source (with expat dependency)
# For musl builds: fontconfig is built from source
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - FONTCONFIG_LDFLAGS: Fontconfig libraries to link
#   - FONTCONFIG_INCLUDE_DIRS: Fontconfig include directories
# =============================================================================

# iOS: Build fontconfig from source (with expat)
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}fontconfig${ColorReset} from source (iOS cross-compile)...")

    set(EXPAT_PREFIX "${IOS_DEPS_CACHE_DIR}/expat")
    set(EXPAT_LIBRARY "${EXPAT_PREFIX}/lib/libexpat.a")
    set(EXPAT_INCLUDE_DIR "${EXPAT_PREFIX}/include")

    set(FONTCONFIG_PREFIX "${IOS_DEPS_CACHE_DIR}/fontconfig")
    set(FONTCONFIG_LIBRARY "${FONTCONFIG_PREFIX}/lib/libfontconfig.a")
    set(FONTCONFIG_INCLUDE_DIR "${FONTCONFIG_PREFIX}/include")

    # Get iOS SDK path
    if(BUILD_IOS_SIM)
        execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
        execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    # Build expat first if needed
    if(NOT EXISTS "${EXPAT_LIBRARY}")
        message(STATUS "  expat library not found in cache, will build from source")

        set(EXPAT_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/expat-build")
        file(MAKE_DIRECTORY "${EXPAT_BUILD_DIR}")

        message(STATUS "  Downloading expat 2.5.0...")
        file(DOWNLOAD
            "https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.gz"
            "${EXPAT_BUILD_DIR}/expat-2.5.0.tar.gz"
            TIMEOUT 30
            SHOW_PROGRESS
        )

        message(STATUS "  Extracting expat...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf expat-2.5.0.tar.gz
            WORKING_DIRECTORY "${EXPAT_BUILD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract expat")
        endif()

        message(STATUS "  Building expat for iOS...")
        execute_process(
            COMMAND bash -c "cd '${EXPAT_BUILD_DIR}/expat-2.5.0' && \
                    CC=clang \
                    CFLAGS='-fPIC -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0' \
                    LDFLAGS='-isysroot ${IOS_SDK_PATH} -arch arm64' \
                    ./configure --prefix='${EXPAT_PREFIX}' --host=aarch64-apple-darwin --disable-shared --enable-static && \
                    make -j && make install"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "expat iOS build failed:\\n${BUILD_ERROR}")
        endif()
    else()
        message(STATUS "  ${BoldBlue}expat${ColorReset} library found in iOS cache: ${BoldMagenta}${EXPAT_LIBRARY}${ColorReset}")
    endif()

    # Build fontconfig
    if(NOT EXISTS "${FONTCONFIG_LIBRARY}")
        message(STATUS "  fontconfig library not found in cache, will build from source")

        set(FONTCONFIG_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/fontconfig-build")
        file(MAKE_DIRECTORY "${FONTCONFIG_BUILD_DIR}")

        message(STATUS "  Downloading fontconfig 2.14.2...")
        file(DOWNLOAD
            "https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.14.2.tar.gz"
            "${FONTCONFIG_BUILD_DIR}/fontconfig-2.14.2.tar.gz"
            TIMEOUT 30
            SHOW_PROGRESS
        )

        message(STATUS "  Extracting fontconfig...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf fontconfig-2.14.2.tar.gz
            WORKING_DIRECTORY "${FONTCONFIG_BUILD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract fontconfig")
        endif()

        message(STATUS "  Building fontconfig for iOS...")
        execute_process(
            COMMAND bash -c "cd '${FONTCONFIG_BUILD_DIR}/fontconfig-2.14.2' && \
                    CC=clang \
                    CFLAGS='-fPIC -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0 -I${EXPAT_INCLUDE_DIR} -I${FREETYPE_PREFIX}/include -I${FREETYPE_PREFIX}/include/freetype2' \
                    LDFLAGS='-isysroot ${IOS_SDK_PATH} -arch arm64 -L${EXPAT_PREFIX}/lib -L${FREETYPE_PREFIX}/lib' \
                    FREETYPE_CFLAGS='-I${FREETYPE_PREFIX}/include -I${FREETYPE_PREFIX}/include/freetype2' \
                    FREETYPE_LIBS='-L${FREETYPE_PREFIX}/lib -lfreetype' \
                    ./configure --prefix='${FONTCONFIG_PREFIX}' --host=aarch64-apple-darwin --disable-shared --enable-static --disable-docs --with-expat='${EXPAT_PREFIX}' && \
                    make -j && make install"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "fontconfig iOS build failed:\\n${BUILD_ERROR}")
        endif()
    else()
        message(STATUS "  ${BoldBlue}fontconfig${ColorReset} library found in iOS cache: ${BoldMagenta}${FONTCONFIG_LIBRARY}${ColorReset}")
    endif()

    # Create imported targets
    add_library(expat STATIC IMPORTED GLOBAL)
    set_target_properties(expat PROPERTIES
        IMPORTED_LOCATION "${EXPAT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${EXPAT_INCLUDE_DIR}"
    )

    add_library(fontconfig STATIC IMPORTED GLOBAL)
    set_target_properties(fontconfig PROPERTIES
        IMPORTED_LOCATION "${FONTCONFIG_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FONTCONFIG_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES expat
    )

    set(FONTCONFIG_LDFLAGS fontconfig)
    set(FONTCONFIG_INCLUDE_DIRS "${FONTCONFIG_INCLUDE_DIR}")

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig (iOS): ${FONTCONFIG_LIBRARY}")
    return()
endif()

# Musl builds: Build fontconfig from source
if(USE_MUSL)
    set(EXPAT_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/expat-src")
    set(EXPAT_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/expat-install")
    set(EXPAT_TARBALL "${CMAKE_BINARY_DIR}/_deps/expat-2.5.0.tar.gz")

    # Download expat if not already present
    if(NOT EXISTS "${EXPAT_SOURCE_DIR}")
        message(STATUS "Downloading expat...")
        file(DOWNLOAD
            "https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.gz"
            "${EXPAT_TARBALL}"
            SHOW_PROGRESS
            STATUS download_status
        )
        list(GET download_status 0 download_result)
        if(NOT download_result EQUAL 0)
            message(FATAL_ERROR "Failed to download expat")
        endif()

        # Extract tarball
        message(STATUS "Extracting expat...")
        execute_process(
            COMMAND tar -xzf "${EXPAT_TARBALL}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/_deps"
            RESULT_VARIABLE tar_result
        )
        if(NOT tar_result EQUAL 0)
            message(FATAL_ERROR "Failed to extract expat tarball")
        endif()

        # Rename extracted directory
        execute_process(
            COMMAND mv expat-2.5.0 expat-src
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/_deps"
            RESULT_VARIABLE mv_result
        )
        if(NOT mv_result EQUAL 0)
            message(FATAL_ERROR "Failed to rename expat directory")
        endif()
    endif()

    # Build expat if not already built
    if(NOT EXISTS "${EXPAT_INSTALL_DIR}/lib/libexpat.a")
        message(STATUS "Building expat...")
        execute_process(
            COMMAND bash -c "cd '${EXPAT_SOURCE_DIR}' && ./configure --prefix='${EXPAT_INSTALL_DIR}' --enable-static --disable-shared CFLAGS='-fPIC' && make -j$(nproc) && make install"
            RESULT_VARIABLE build_result
        )
        if(NOT build_result EQUAL 0)
            message(FATAL_ERROR "Failed to build expat")
        endif()
        message(STATUS "Expat build complete")
    endif()

    set(FONTCONFIG_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/fontconfig-src")
    set(FONTCONFIG_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/fontconfig-install")
    set(FONTCONFIG_TARBALL "${CMAKE_BINARY_DIR}/_deps/fontconfig-2.14.2.tar.gz")

    # Download fontconfig if not already present
    if(NOT EXISTS "${FONTCONFIG_SOURCE_DIR}")
        message(STATUS "Downloading fontconfig...")
        file(DOWNLOAD
            "https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.14.2.tar.gz"
            "${FONTCONFIG_TARBALL}"
            SHOW_PROGRESS
            STATUS download_status
        )
        list(GET download_status 0 download_result)
        if(NOT download_result EQUAL 0)
            message(FATAL_ERROR "Failed to download fontconfig")
        endif()

        # Extract tarball
        message(STATUS "Extracting fontconfig...")
        execute_process(
            COMMAND tar -xzf "${FONTCONFIG_TARBALL}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/_deps"
            RESULT_VARIABLE tar_result
        )
        if(NOT tar_result EQUAL 0)
            message(FATAL_ERROR "Failed to extract fontconfig tarball")
        endif()

        # Rename extracted directory
        execute_process(
            COMMAND mv fontconfig-2.14.2 fontconfig-src
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/_deps"
            RESULT_VARIABLE mv_result
        )
        if(NOT mv_result EQUAL 0)
            message(FATAL_ERROR "Failed to rename fontconfig directory")
        endif()
    endif()

    # Build fontconfig if not already built
    if(NOT EXISTS "${FONTCONFIG_INSTALL_DIR}/lib/libfontconfig.a")
        message(STATUS "Building fontconfig...")
        execute_process(
            COMMAND bash -c "cd '${FONTCONFIG_SOURCE_DIR}' && ./configure --prefix='${FONTCONFIG_INSTALL_DIR}' --disable-docs --enable-static --disable-shared --with-baseconfigdir='${FONTCONFIG_INSTALL_DIR}/etc/fonts' --with-expat='${EXPAT_INSTALL_DIR}' CFLAGS='-fPIC' LDFLAGS='-L${EXPAT_INSTALL_DIR}/lib' CPPFLAGS='-I${EXPAT_INSTALL_DIR}/include' && make -j$(nproc) && make install"
            RESULT_VARIABLE build_result
        )
        if(NOT build_result EQUAL 0)
            message(FATAL_ERROR "Failed to build fontconfig")
        endif()
        message(STATUS "Fontconfig build complete")
    endif()

    # Create imported library target for expat
    add_library(expat STATIC IMPORTED GLOBAL)
    set_target_properties(expat PROPERTIES
        IMPORTED_LOCATION "${EXPAT_INSTALL_DIR}/lib/libexpat.a"
        INTERFACE_INCLUDE_DIRECTORIES "${EXPAT_INSTALL_DIR}/include"
    )

    # Create imported library target for fontconfig (depends on expat)
    add_library(fontconfig STATIC IMPORTED GLOBAL)
    set_target_properties(fontconfig PROPERTIES
        IMPORTED_LOCATION "${FONTCONFIG_INSTALL_DIR}/lib/libfontconfig.a"
        INTERFACE_INCLUDE_DIRECTORIES "${FONTCONFIG_INSTALL_DIR}/include"
        INTERFACE_LINK_LIBRARIES expat
    )

    set(FONTCONFIG_LDFLAGS fontconfig)
    set(FONTCONFIG_INCLUDE_DIRS "${FONTCONFIG_INSTALL_DIR}/include")

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig (musl): Built from source")
    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(WIN32)
    # Windows: Use vcpkg
    find_package(unofficial-fontconfig CONFIG REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: vcpkg")

else()
    message(FATAL_ERROR "Unsupported platform for Fontconfig")
endif()
