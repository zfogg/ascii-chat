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

if(PLATFORM_IOS AND NOT BUILD_EXECUTABLES)
    return()
endif()
if(PLATFORM_IOS OR USE_MUSL)
    find_program(GPERF_EXECUTABLE NAMES gperf REQUIRED)
    include(${CMAKE_CURRENT_LIST_DIR}/Expat.cmake)
    include(ExternalProject)
    set(FONTCONFIG_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/_deps/installed/fontconfig")
    set(FONTCONFIG_INCLUDE_DIRS "${FONTCONFIG_PREFIX}/include")
    file(MAKE_DIRECTORY "${FONTCONFIG_INCLUDE_DIRS}")
    set(_fontconfig_cflags "-fPIC -I${EXPAT_PREFIX}/include -I${FREETYPE_PREFIX}/include/freetype2")
    set(_fontconfig_host)
    if(PLATFORM_IOS)
        set(_fontconfig_cc "${CMAKE_C_COMPILER}")
        string(APPEND _fontconfig_cflags " -isysroot ${CMAKE_OSX_SYSROOT} -arch ${CMAKE_OSX_ARCHITECTURES} -miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        set(_fontconfig_host --host=aarch64-apple-darwin)
    else()
        set(_fontconfig_cc "${MUSL_GCC}")
    endif()
    set(_fontconfig_env "${CMAKE_COMMAND}" -E env "CC=${_fontconfig_cc}"
        "GPERF=${GPERF_EXECUTABLE}"
        "REALGCC=${REAL_GCC}" "CFLAGS=${_fontconfig_cflags}"
        "LDFLAGS=-L${EXPAT_PREFIX}/lib -L${FREETYPE_PREFIX}/lib"
        "PKG_CONFIG_PATH=${EXPAT_PREFIX}/lib/pkgconfig:${FREETYPE_PREFIX}/lib/pkgconfig"
        "PKG_CONFIG_LIBDIR=${EXPAT_PREFIX}/lib/pkgconfig:${FREETYPE_PREFIX}/lib/pkgconfig" --)
    ExternalProject_Add(asciichat-fontconfig
        URL https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.14.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX "${CMAKE_CURRENT_BINARY_DIR}/_deps/fontconfig"
        CONFIGURE_COMMAND ${_fontconfig_env} <SOURCE_DIR>/configure
            --prefix=${FONTCONFIG_PREFIX} --disable-docs --enable-static --disable-shared
            --with-baseconfigdir=${FONTCONFIG_PREFIX}/etc/fonts ${_fontconfig_host}
        BUILD_COMMAND ${_fontconfig_env} make -j8
        INSTALL_COMMAND ${_fontconfig_env} make install
        INSTALL_BYPRODUCTS "${FONTCONFIG_PREFIX}/lib/libfontconfig.a"
        DEPENDS asciichat-expat-stage asciichat-freetype-stage
        LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_INSTALL TRUE LOG_OUTPUT_ON_FAILURE TRUE)
    add_library(fontconfig STATIC IMPORTED GLOBAL)
    set_target_properties(fontconfig PROPERTIES
        IMPORTED_LOCATION "${FONTCONFIG_PREFIX}/lib/libfontconfig.a"
        INTERFACE_INCLUDE_DIRECTORIES "${FONTCONFIG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "expat;${FREETYPE_LIBRARIES}")
    add_dependencies(fontconfig asciichat-fontconfig)
    set(FONTCONFIG_LDFLAGS fontconfig)
    set(FONTCONFIG_LINK_LIBRARIES fontconfig)
    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    # pkg_check_modules already sets FONTCONFIG_LDFLAGS to proper linker flags
    # (-L/path/to/lib -lfontconfig), so use it as-is

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
        # Use archives so a release executable does not depend on the
        # developer's Homebrew versions of Fontconfig and its libraries.
        set(_fontconfig_static_paths)
        foreach(_dependency fontconfig expat freetype intl png16)
            set(_brew_package "${_dependency}")
            if(_dependency STREQUAL "intl")
                set(_brew_package gettext)
            elseif(_dependency STREQUAL "png16")
                set(_brew_package libpng)
            endif()
            find_file(_${_dependency}_static_archive NAMES "lib${_dependency}.a"
                PATHS "${HOMEBREW_PREFIX}/opt/${_brew_package}/lib"
                      "/usr/local/opt/${_brew_package}/lib"
                      "/opt/homebrew/opt/${_brew_package}/lib"
                NO_DEFAULT_PATH)
            if(NOT _${_dependency}_static_archive)
                message(FATAL_ERROR "macOS Release requires the static ${_dependency} archive (lib${_dependency}.a)")
            endif()
            list(APPEND _fontconfig_static_paths "${_${_dependency}_static_archive}")
        endforeach()
        # fontconfig's static pkg-config closure also uses the system math,
        # compression, and zlib libraries on macOS.
        list(APPEND _fontconfig_static_paths bz2 z m)
        set(FONTCONFIG_LINK_LIBRARIES ${_fontconfig_static_paths})
    else()
        # pkg_check_modules supplies the normal system linker flags.
        set(FONTCONFIG_LINK_LIBRARIES ${FONTCONFIG_LDFLAGS})
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(WIN32)
    # Windows: fontconfig is not typically available or needed (Windows uses GDI for fonts)
    # Set empty variables so the build can proceed without fontconfig
    set(FONTCONFIG_LINK_LIBRARIES "")
    set(FONTCONFIG_LDFLAGS "")
    set(FONTCONFIG_INCLUDE_DIRS "")
    message(STATUS "⚠ Fontconfig not needed on Windows (using GDI for font resolution)")

else()
    message(FATAL_ERROR "Unsupported platform for Fontconfig")
endif()
