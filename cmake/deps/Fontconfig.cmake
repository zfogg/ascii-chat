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
#   - _asciichat_fontconfig_pkg_LDFLAGS: Fontconfig libraries to link
#   - _asciichat_fontconfig_include_dirs: Fontconfig include directories
# =============================================================================

if(PLATFORM_IOS AND NOT BUILD_EXECUTABLES)
    return()
endif()
if(EMSCRIPTEN)
    message(STATUS "⚠ Fontconfig not needed for Emscripten web targets")
    return()
endif()
if(PLATFORM_IOS OR USE_MUSL)
    find_program(GPERF_EXECUTABLE NAMES gperf REQUIRED)
    include(${CMAKE_CURRENT_LIST_DIR}/Expat.cmake)
    include(ExternalProject)
    set(FONTCONFIG_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/build/installed/fontconfig")
    set(_asciichat_fontconfig_include_dirs "${FONTCONFIG_PREFIX}/include")
    file(MAKE_DIRECTORY "${_asciichat_fontconfig_include_dirs}")
    set(_fontconfig_cflags "-fPIC -I${EXPAT_PREFIX}/include -I${FREETYPE_PREFIX}/include/freetype2")
    set(_fontconfig_host)
    if(PLATFORM_IOS)
        set(_fontconfig_cc "${CMAKE_C_COMPILER}")
        string(APPEND _fontconfig_cflags " -isysroot ${CMAKE_OSX_SYSROOT} -arch ${CMAKE_OSX_ARCHITECTURES} -miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        set(_fontconfig_host --host=aarch64-apple-darwin)
    else()
        set(_fontconfig_cc "${ASCIICHAT_CLANG_EXECUTABLE} ${ASCIICHAT_MUSL_TARGET_FLAG}")
    endif()
    set(_fontconfig_env "${CMAKE_COMMAND}" -E env "CC=${_fontconfig_cc}"
        "GPERF=${GPERF_EXECUTABLE}"
        "CFLAGS=${_fontconfig_cflags}"
        "LDFLAGS=-L${EXPAT_PREFIX}/lib -L${FREETYPE_PREFIX}/lib"
        "PKG_CONFIG_PATH=${EXPAT_PREFIX}/lib/pkgconfig:${FREETYPE_PREFIX}/lib/pkgconfig"
        "PKG_CONFIG_LIBDIR=${EXPAT_PREFIX}/lib/pkgconfig:${FREETYPE_PREFIX}/lib/pkgconfig" --)
    ExternalProject_Add(asciichat-fontconfig-musl
        URL https://www.freedesktop.org/software/fontconfig/release/fontconfig-2.14.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/build/fontconfig"
        CONFIGURE_COMMAND ${_fontconfig_env} <SOURCE_DIR>/configure
            --prefix=${FONTCONFIG_PREFIX} --disable-docs --disable-cache-build
            --enable-static --disable-shared
            --with-baseconfigdir=${FONTCONFIG_PREFIX}/etc/fonts ${_fontconfig_host}
        BUILD_COMMAND ${_fontconfig_env} make -j8
        INSTALL_COMMAND ${_fontconfig_env} make install
        INSTALL_BYPRODUCTS "${FONTCONFIG_PREFIX}/lib/libfontconfig.a"
        DEPENDS asciichat-expat-stage asciichat-freetype-stage
        LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_INSTALL TRUE LOG_OUTPUT_ON_FAILURE TRUE)
    add_library(fontconfig STATIC IMPORTED GLOBAL)
    set_target_properties(fontconfig PROPERTIES
        IMPORTED_LOCATION "${FONTCONFIG_PREFIX}/lib/libfontconfig.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_asciichat_fontconfig_include_dirs}"
        INTERFACE_LINK_LIBRARIES "asciichat-expat;asciichat-freetype")
    add_dependencies(fontconfig asciichat-fontconfig-musl)
    add_library(asciichat-fontconfig INTERFACE)
    target_link_libraries(asciichat-fontconfig INTERFACE fontconfig)
    set(_asciichat_fontconfig_pkg_LDFLAGS fontconfig)
    set(_asciichat_fontconfig_link_libraries fontconfig)
    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_asciichat_fontconfig_pkg fontconfig REQUIRED)

    # pkg_check_modules already sets _asciichat_fontconfig_pkg_LDFLAGS to proper linker flags
    # (-L/path/to/lib -lfontconfig), so use it as-is
    set(_asciichat_fontconfig_link_libraries ${_asciichat_fontconfig_pkg_LDFLAGS})

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${_asciichat_fontconfig_pkg_LDFLAGS}")

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_asciichat_fontconfig_pkg fontconfig REQUIRED)

    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT BUILD_SHARED_LIBS)
        # Use archives so a release executable does not depend on the
        # developer's Homebrew versions of Fontconfig and its libraries.
        set(_fontconfig_static_paths)
        foreach(_dependency fontconfig expat freetype intl png16)
            if(_dependency STREQUAL "freetype")
                # Share the renderer's selected FreeType, including pinned HVF builds.
                list(APPEND _fontconfig_static_paths asciichat-freetype)
                continue()
            endif()
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
        set(_asciichat_fontconfig_link_libraries ${_fontconfig_static_paths})
    else()
        # pkg_check_modules supplies the normal system linker flags.
        set(_asciichat_fontconfig_link_libraries ${_asciichat_fontconfig_pkg_LDFLAGS})
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${_asciichat_fontconfig_pkg_LDFLAGS}")

elseif(WIN32)
    # Windows: fontconfig is not typically available or needed (Windows uses GDI for fonts)
    # Set empty variables so the build can proceed without fontconfig
    set(_asciichat_fontconfig_link_libraries "")
    set(_asciichat_fontconfig_pkg_LDFLAGS "")
    set(_asciichat_fontconfig_include_dirs "")
    message(STATUS "⚠ Fontconfig not needed on Windows (using GDI for font resolution)")

else()
    message(FATAL_ERROR "Unsupported platform for Fontconfig")
endif()

# Keep the dependency target contract identical for system and source-built
# fontconfig. CoreTargets consumes this stable target name on Unix platforms.
if((UNIX OR APPLE) AND NOT TARGET asciichat-fontconfig)
    add_library(asciichat-fontconfig INTERFACE)
    target_link_libraries(asciichat-fontconfig INTERFACE ${_asciichat_fontconfig_link_libraries})
    if(_asciichat_fontconfig_include_dirs)
        target_include_directories(asciichat-fontconfig INTERFACE ${_asciichat_fontconfig_include_dirs})
    endif()
endif()
