# Native network I/O uses epoll, kqueue or IOCP through libuv. Browser transports
# own their event loops and do not link this native dependency.
function(configure_libuv)
    if(EMSCRIPTEN)
        return()
    endif()

    if(WIN32)
        find_package(libuv CONFIG REQUIRED)
        if(BUILD_SHARED_LIBS AND TARGET libuv::uv)
            set(ASCIICHAT_LIBUV_TARGET libuv::uv)
        elseif(TARGET libuv::uv_a)
            set(ASCIICHAT_LIBUV_TARGET libuv::uv_a)
        else()
            message(FATAL_ERROR
                "libuv was found, but its CMake package exported neither libuv::uv nor libuv::uv_a")
        endif()
    elseif(USE_MUSL)
        include(FetchContent)
        set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(asciichat_libuv
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/asciichat_libuv"
            EXCLUDE_FROM_ALL
            GIT_REPOSITORY https://github.com/libuv/libuv.git
            GIT_TAG v1.51.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(asciichat_libuv)
        # A Clang musl target triple still finds the host's glibc headers.
        # Keep Clang's builtin headers, but select musl and the isolated Linux
        # headers explicitly so libuv does not reference glibc-only symbols.
        # GCC uses -nostdinc from the shared musl toolchain flags; it does not
        # accept Clang's -nostdlibinc spelling.
        if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER MATCHES "(^|[/\\\\])clang(\\.exe)?$")
            target_compile_options(uv_a PRIVATE -nostdlibinc)
        endif()
        # Consumers compile sources that include <uv.h>; propagate the musl
        # and kernel header roots through the static target.
        target_include_directories(uv_a SYSTEM PUBLIC
            "$<BUILD_INTERFACE:${MUSL_INCLUDE_DIR}>"
            "$<BUILD_INTERFACE:${KERNEL_HEADERS_DIR}>")
        set(ASCIICHAT_LIBUV_INCLUDE_DIRS "${asciichat_libuv_SOURCE_DIR}/include")
        set(ASCIICHAT_LIBUV_TARGET uv_a)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(ASCIICHAT_LIBUV REQUIRED IMPORTED_TARGET libuv)
        # glibc development builds also produce a shared library; distro libuv
        # archives are not necessarily PIC. Linux static releases use musl above.
        if(NOT APPLE OR BUILD_SHARED_LIBS)
            set(ASCIICHAT_LIBUV_TARGET PkgConfig::ASCIICHAT_LIBUV)
        else()
            find_library(ASCIICHAT_LIBUV_STATIC_ARCHIVE NAMES libuv.a
                HINTS ${ASCIICHAT_LIBUV_LIBRARY_DIRS} REQUIRED)
            set(_libuv_system_libraries ${ASCIICHAT_LIBUV_STATIC_LIBRARIES})
            list(REMOVE_ITEM _libuv_system_libraries uv)
            add_library(asciichat-libuv-static STATIC IMPORTED)
            set_target_properties(asciichat-libuv-static PROPERTIES
                IMPORTED_LOCATION "${ASCIICHAT_LIBUV_STATIC_ARCHIVE}"
                INTERFACE_INCLUDE_DIRECTORIES "${ASCIICHAT_LIBUV_INCLUDE_DIRS}"
                INTERFACE_COMPILE_OPTIONS "${ASCIICHAT_LIBUV_STATIC_CFLAGS_OTHER}"
                INTERFACE_LINK_LIBRARIES "${_libuv_system_libraries}")
            set(ASCIICHAT_LIBUV_TARGET asciichat-libuv-static)
        endif()
    endif()

    if(NOT ASCIICHAT_LIBUV_TARGET)
        message(FATAL_ERROR "libuv configuration did not produce a link target")
    endif()

    set(ASCIICHAT_LIBUV_TARGET "${ASCIICHAT_LIBUV_TARGET}" PARENT_SCOPE)
    set(ASCIICHAT_LIBUV_INCLUDE_DIRS "${ASCIICHAT_LIBUV_INCLUDE_DIRS}" PARENT_SCOPE)
endfunction()

configure_libuv()
