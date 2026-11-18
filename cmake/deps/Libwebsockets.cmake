# Installed packages retain their own static/shared dependency configuration.
option(ASCIICHAT_USE_SYSTEM_LIBWEBSOCKETS
    "Use system libwebsockets when the package is available"
    ON)
if((WIN32 OR ASCIICHAT_SHARED_DEPS OR ASCIICHAT_USE_SYSTEM_LIBWEBSOCKETS)
   AND NOT ASCIICHAT_BUILD_DEPS_FROM_SOURCE
   AND NOT PLATFORM_IOS
   AND NOT USE_MUSL)
    if(WIN32)
        # The vcpkg port ships a config file, but its package is not visible
        # through the project's staged prefix during configure. Discover the
        # staged archive directly and retain the required system closure.
        find_path(_asciichat_lws_include NAMES libwebsockets.h
            PATHS "${VCPKG_INCLUDE_PATH}" REQUIRED)
        find_library(_asciichat_lws_library NAMES websockets
            PATHS "${VCPKG_LIB_PATH}" REQUIRED)
        add_library(asciichat-libwebsockets UNKNOWN IMPORTED GLOBAL)
        set_target_properties(asciichat-libwebsockets PROPERTIES
            IMPORTED_LOCATION "${_asciichat_lws_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_asciichat_lws_include}"
            INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;ZLIB::ZLIB;ws2_32")
        set(LIBWEBSOCKETS_LIBRARIES asciichat-libwebsockets)
        set(LIBWEBSOCKETS_INCLUDE_DIRS "${_asciichat_lws_include}")
        set(LIBWEBSOCKETS_FOUND TRUE)
        if(TARGET asciichat-project-options)
            target_compile_definitions(asciichat-project-options INTERFACE HAVE_LIBWEBSOCKETS=1)
        endif()
        return()
    endif()
    # Try CMake config first, then fall back to pkg-config
    find_package(libwebsockets QUIET CONFIG)
    if(NOT libwebsockets_FOUND)
        include(FindPkgConfig)
        pkg_check_modules(libwebsockets REQUIRED libwebsockets)
        if(NOT TARGET libwebsockets::libwebsockets)
            add_library(libwebsockets::libwebsockets INTERFACE IMPORTED)
            target_include_directories(libwebsockets::libwebsockets INTERFACE ${libwebsockets_INCLUDE_DIRS})
            target_link_libraries(libwebsockets::libwebsockets INTERFACE ${libwebsockets_LIBRARIES})
        endif()
    endif()
    message(STATUS "${BoldGreen}✓${ColorReset} libwebsockets found (installed package)")
    if(TARGET asciichat-project-options)
        target_compile_definitions(asciichat-project-options INTERFACE HAVE_LIBWEBSOCKETS=1)
    endif()

    # Prefer the shared target when the package exports both static and shared
    # variants. Linking the static target into libasciichat can fail when the
    # package was not compiled as position-independent code.
    if(TARGET websockets_shared)
        set(LIBWEBSOCKETS_LIBRARIES websockets_shared)
    elseif(TARGET websockets)
        set(LIBWEBSOCKETS_LIBRARIES websockets)
    elseif(TARGET libwebsockets::libwebsockets)
        set(LIBWEBSOCKETS_LIBRARIES libwebsockets::libwebsockets)
    elseif(libwebsockets_LINK_LIBRARIES)
        set(LIBWEBSOCKETS_LIBRARIES ${libwebsockets_LINK_LIBRARIES})
    else()
        message(FATAL_ERROR "libwebsockets was found, but it did not provide a link target or library")
    endif()
    set(LIBWEBSOCKETS_INCLUDE_DIRS ${libwebsockets_INCLUDE_DIRS})
    set(LIBWEBSOCKETS_FOUND TRUE)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_libwebsockets)
    asciichat_dependency_defaults()
    # Function probes must link to distinguish platform APIs from missing symbols.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    if(USE_MUSL)
        # Use the target CRT rather than the host's implicit runtime.
        set(CMAKE_C_STANDARD_LIBRARIES "${MUSL_COMPILER_RT_PATH} -lc -lm ${MUSL_LIBDIR}/crtn.o")
        list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_C_STANDARD_LIBRARIES)
    endif()
    set(LWS_WITH_SHARED ${BUILD_SHARED_LIBS})
    set(LWS_WITH_STATIC ON)
    if(BUILD_SHARED_LIBS)
        set(LWS_WITH_STATIC OFF)
    endif()
    set(LWS_WITHOUT_TESTAPPS ON)
    set(DISABLE_WERROR ON)
    set(LWS_WITH_SSL ON)
    set(LWS_WITH_SCHANNEL OFF)
    set(LWS_WITH_MBEDTLS OFF)
    set(LWS_WITH_GNUTLS OFF)
    set(LWS_WITH_LIBEV OFF)
    set(LWS_WITH_LIBUV OFF)
    set(LWS_WITH_LIBEVENT OFF)
    set(LWS_WITH_GLIB OFF)
    set(LWS_WITH_SYSTEMD OFF)
    set(LWS_WITH_LIBCAP OFF)
    set(LWS_WITH_JOSE OFF)
    set(LWS_WITH_GENCRYPTO OFF)
    set(LWS_WITH_HTTP3 OFF)
    set(LWS_ROLE_QUIC OFF)
    set(LWS_WITHOUT_EXTENSIONS OFF)
    set(LWS_WITH_ZLIB ON)
    set(LWS_WITH_BUNDLED_ZLIB OFF)
    # libwebsockets' CMake uses the legacy singular variable when it links
    # its target.  pkg-config may populate only ZLIB_LIBRARIES, while a prior
    # FindZLIB probe can leave ZLIB_LIBRARY cached as NOTFOUND.
    if(DEFINED ZLIB_LIBRARIES AND NOT ZLIB_LIBRARIES STREQUAL "")
        if(NOT DEFINED ZLIB_LIBRARY OR ZLIB_LIBRARY MATCHES "-NOTFOUND$")
            set(ZLIB_LIBRARY "${ZLIB_LIBRARIES}" CACHE STRING "zlib libraries" FORCE)
        endif()
    endif()
    set(LWS_WITH_SOCKS5 OFF)
    set(LWS_WITHOUT_DAEMONIZE ON)
    set(LWS_IPV6 ON)
    FetchContent_Declare(asciichat_libwebsockets
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_libwebsockets"
        URL https://github.com/warmcat/libwebsockets/archive/4cb71af648294663ebd38550ccf47b29adec7974.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(asciichat_libwebsockets)
    if(NOT asciichat_libwebsockets_POPULATED)
        cmake_policy(PUSH)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(asciichat_libwebsockets)
        cmake_policy(POP)
    endif()
    set(LWS_SOURCE_DIR "${asciichat_libwebsockets_SOURCE_DIR}")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/patch-libwebsockets-cmake.cmake")
    add_subdirectory("${asciichat_libwebsockets_SOURCE_DIR}" "${asciichat_libwebsockets_BINARY_DIR}" EXCLUDE_FROM_ALL)
    # An OpenSSL package prefix can also contain installed copies of libraries
    # being built here. Keep those external headers after the selected sources.
    foreach(_target websockets websockets_shared)
        if(TARGET ${_target})
            foreach(_include IN LISTS OPENSSL_INCLUDE_DIR OPENSSL_INCLUDE_DIRS)
                target_include_directories(${_target} SYSTEM PUBLIC "$<BUILD_INTERFACE:${_include}>")
            endforeach()
        endif()
    endforeach()
    if(BUILD_SHARED_LIBS)
        set(LIBWEBSOCKETS_LIBRARIES websockets_shared PARENT_SCOPE)
    else()
        set(LIBWEBSOCKETS_LIBRARIES websockets PARENT_SCOPE)
    endif()
    set(LIBWEBSOCKETS_INCLUDE_DIRS "${asciichat_libwebsockets_SOURCE_DIR}/include;${asciichat_libwebsockets_BINARY_DIR}" PARENT_SCOPE)
endfunction()
asciichat_build_libwebsockets()
set(LIBWEBSOCKETS_FOUND TRUE)
if(TARGET asciichat-project-options)
    target_compile_definitions(asciichat-project-options INTERFACE HAVE_LIBWEBSOCKETS=1)
endif()
