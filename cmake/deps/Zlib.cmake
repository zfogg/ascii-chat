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


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_zlib)
        asciichat_dependency_defaults()
        set(ZLIB_BUILD_EXAMPLES OFF)
        FetchContent_Declare(asciichat_zlib
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_zlib"
            EXCLUDE_FROM_ALL
            URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
            URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_zlib)
        if(BUILD_SHARED_LIBS)
            set(_target zlib)
        else()
            set(_target zlibstatic)
        endif()
        # Bridge consumers using FindZLIB without adding zlib to their install exports.
        add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
        set_target_properties(ZLIB::ZLIB PROPERTIES
            SYSTEM FALSE
            INTERFACE_LINK_LIBRARIES ${_target}
            INTERFACE_INCLUDE_DIRECTORIES "${asciichat_zlib_BINARY_DIR};${asciichat_zlib_SOURCE_DIR}")
        add_library(asciichat-zlib ALIAS ZLIB::ZLIB)
        add_library(ZLIB::zlib ALIAS ZLIB::ZLIB)
        set(ZLIB_INCLUDE_DIR "${asciichat_zlib_BINARY_DIR};${asciichat_zlib_SOURCE_DIR}" PARENT_SCOPE)
    endfunction()
    asciichat_build_zlib()
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARY asciichat-zlib)
    set(ZLIB_LIBRARIES asciichat-zlib)
    set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
    return()
endif()

# =============================================================================
# Non-musl: Use system package or pkg-config
# =============================================================================

# Try to find zlib via CMake module mode first (vcpkg provides a wrapper),
# then config mode, then fall back to pkg-config
find_package(ZLIB QUIET)

if(NOT ZLIB_FOUND)
    find_package(ZLIB QUIET CONFIG)
endif()

if(NOT ZLIB_FOUND)
    # Fall back to pkg-config if CMake find didn't work
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(ZLIB REQUIRED zlib)
    else()
        # Last resort: check vcpkg installed directory directly
        if(DEFINED VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
            set(_vcpkg_zlib_lib "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/zlib.lib")
            set(_vcpkg_zlib_inc "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
            if(EXISTS "${_vcpkg_zlib_lib}")
                set(ZLIB_FOUND TRUE)
                set(ZLIB_LIBRARIES "${_vcpkg_zlib_lib}")
                set(ZLIB_INCLUDE_DIRS "${_vcpkg_zlib_inc}")
            endif()
        endif()
        if(NOT ZLIB_FOUND)
            message(FATAL_ERROR "zlib not found: no CMake config, no pkg-config, and no vcpkg fallback")
        endif()
    endif()

    # Create imported target for compatibility
    if(NOT TARGET ZLIB::zlib)
        add_library(ZLIB::zlib INTERFACE IMPORTED)
        target_include_directories(ZLIB::zlib SYSTEM INTERFACE ${ZLIB_INCLUDE_DIRS})
        target_link_libraries(ZLIB::zlib INTERFACE ${ZLIB_LIBRARIES})
    endif()
endif()

message(STATUS "${BoldGreen}✓${ColorReset} zlib found")
