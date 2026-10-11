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
        set(ASCIICHAT_LIBUV_TARGET uv_a)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(ASCIICHAT_LIBUV REQUIRED IMPORTED_TARGET libuv)
        set(ASCIICHAT_LIBUV_TARGET PkgConfig::ASCIICHAT_LIBUV)
    endif()
endif()
