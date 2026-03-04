# =============================================================================
# Fontconfig Dependency
# =============================================================================
# Cross-platform configuration for fontconfig font resolution
#
# For musl builds: fontconfig is built from source
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - FONTCONFIG_LDFLAGS: Fontconfig libraries to link
#   - FONTCONFIG_INCLUDE_DIRS: Fontconfig include directories
# =============================================================================

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
