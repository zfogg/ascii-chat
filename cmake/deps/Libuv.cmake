# Native network I/O uses epoll, kqueue or IOCP through libuv. Browser transports
# own their event loops and do not link this native dependency.
if(NOT EMSCRIPTEN)
    if(WIN32)
        find_package(libuv CONFIG REQUIRED)
        if(TARGET libuv::uv_a)
            set(ASCIICHAT_LIBUV_TARGET libuv::uv_a)
        else()
            set(ASCIICHAT_LIBUV_TARGET libuv::uv)
        endif()
    elseif(USE_MUSL)
        include(FetchContent)
        set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(asciichat_libuv
            GIT_REPOSITORY https://github.com/libuv/libuv.git
            GIT_TAG v1.51.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(asciichat_libuv)
        # A musl target triple alone still finds the host's glibc headers.
        # Keep Clang's builtin headers, but select musl and the isolated Linux
        # headers explicitly so libuv does not reference glibc-only symbols.
        target_compile_options(uv_a PRIVATE -nostdlibinc)
        target_include_directories(uv_a SYSTEM PRIVATE
            "${MUSL_INCLUDE_DIR}" "${KERNEL_HEADERS_DIR}")
        set(ASCIICHAT_LIBUV_TARGET uv_a)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(ASCIICHAT_LIBUV REQUIRED IMPORTED_TARGET libuv)
        if(ASCIICHAT_SHARED_DEPS)
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
endif()
