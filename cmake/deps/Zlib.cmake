# Required zlib dependency. Discovery state stays function-local; consumers
# use only asciichat-zlib and its compatibility aliases.

include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)

function(asciichat_configure_zlib)
    set(_provider "")

    if(PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
        set(ZLIB_BUILD_EXAMPLES OFF)
        asciichat_fetchcontent_declare(asciichat_zlib
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/zlib"
            EXCLUDE_FROM_ALL
            URL https://zlib.net/fossils/zlib-1.3.1.tar.gz
            URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_zlib)
        if(BUILD_SHARED_LIBS)
            set(_provider zlib)
        else()
            set(_provider zlibstatic)
        endif()
        add_library(asciichat-zlib INTERFACE)
        target_link_libraries(asciichat-zlib INTERFACE "${_provider}")
        target_include_directories(asciichat-zlib INTERFACE
            "${asciichat_zlib_BINARY_DIR}" "${asciichat_zlib_SOURCE_DIR}")
    else()
        find_package(ZLIB QUIET PATHS "${VCPKG_LIB_PATH}/.." NO_DEFAULT_PATH)
        if(TARGET ZLIB::ZLIB)
            set(_provider ZLIB::ZLIB)
        endif()

        if(NOT _provider)
            find_package(ZLIB QUIET CONFIG PATHS "${VCPKG_LIB_PATH}/.." NO_DEFAULT_PATH)
            if(TARGET ZLIB::ZLIB)
                set(_provider ZLIB::ZLIB)
            endif()
        endif()

        if(NOT _provider)
            find_package(PkgConfig QUIET)
            if(PKG_CONFIG_FOUND)
                pkg_check_modules(_asciichat_zlib IMPORTED_TARGET REQUIRED zlib)
                set(_provider PkgConfig::_asciichat_zlib)
            endif()
        endif()

        if(NOT _provider AND DEFINED VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
            set(_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
            find_library(_zlib_library NAMES zlib zlibd PATHS "${_vcpkg_root}/lib" REQUIRED)
            find_path(_zlib_include NAMES zlib.h PATHS "${_vcpkg_root}/include" REQUIRED)
            add_library(asciichat-zlib-provider UNKNOWN IMPORTED)
            set_target_properties(asciichat-zlib-provider PROPERTIES
                IMPORTED_LOCATION "${_zlib_library}"
                INTERFACE_INCLUDE_DIRECTORIES "${_zlib_include}")
            set(_provider asciichat-zlib-provider)
        endif()

        if(NOT _provider)
            message(FATAL_ERROR "zlib is required but no target provider was found")
        endif()

        add_library(asciichat-zlib INTERFACE)
        target_link_libraries(asciichat-zlib INTERFACE "${_provider}")
    endif()

    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS asciichat-zlib)
    endif()
    if(NOT TARGET ZLIB::zlib)
        add_library(ZLIB::zlib ALIAS asciichat-zlib)
    endif()
    message(STATUS "${BoldGreen}✓${ColorReset} zlib configured through asciichat-zlib")
endfunction()

asciichat_configure_zlib()
