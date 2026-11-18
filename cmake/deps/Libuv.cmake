# Native network I/O uses epoll, kqueue or IOCP through libuv. Browser transports
# own their event loops and do not link this native dependency.
if(NOT EMSCRIPTEN)
    if(WIN32)
        # vcpkg's libuv port does not provide a CMake package config. Discover
        # the installed headers/archive directly and publish one project target.
        find_path(ASCIICHAT_LIBUV_INCLUDE_DIR NAMES uv.h
            PATHS "${VCPKG_INCLUDE_PATH}" PATH_SUFFIXES uv REQUIRED)
        if(BUILD_SHARED_LIBS)
            find_library(ASCIICHAT_LIBUV_LIBRARY NAMES uv PATHS "${VCPKG_LIB_PATH}" REQUIRED)
        else()
            find_library(ASCIICHAT_LIBUV_LIBRARY NAMES uv_a uv PATHS "${VCPKG_LIB_PATH}" REQUIRED)
        endif()
        # vcpkg's archive does not carry its Windows system closure when it is
        # imported directly (uv__getpwuid_r uses Userenv.dll).
        if(NOT TARGET asciichat-libuv)
            add_library(asciichat-libuv UNKNOWN IMPORTED GLOBAL)
            set_target_properties(asciichat-libuv PROPERTIES
                IMPORTED_LOCATION "${ASCIICHAT_LIBUV_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ASCIICHAT_LIBUV_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES userenv)
        endif()
        set(ASCIICHAT_LIBUV_TARGET asciichat-libuv)
    elseif(USE_MUSL)
        include(FetchContent)
        set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(asciichat_libuv
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/asciichat_libuv"
            EXCLUDE_FROM_ALL
            GIT_REPOSITORY https://github.com/libuv/libuv.git
            GIT_TAG v1.51.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(asciichat_libuv)
        # musl does not provide the glibc three-argument pthread name APIs.
        # Keep libuv's optional thread-name helpers buildable and let callers
        # receive UV_ENOSYS for the unsupported operation.
        set(_asciichat_libuv_thread_source
            "${asciichat_libuv_SOURCE_DIR}/src/unix/thread.c")
        file(READ "${_asciichat_libuv_thread_source}" _asciichat_libuv_thread_text)
        string(REPLACE
            "#else\nint uv__thread_setname(const char* name) {\n  char namebuf[UV_PTHREAD_MAX_NAMELEN_NP];"
            "#elif defined(__linux__) && !defined(__GLIBC__)\nint uv__thread_setname(const char* name) {\n  return UV_ENOSYS;\n}\n#else\nint uv__thread_setname(const char* name) {\n  char namebuf[UV_PTHREAD_MAX_NAMELEN_NP];"
            _asciichat_libuv_thread_text "${_asciichat_libuv_thread_text}")
        string(REPLACE
            "#if (defined(__ANDROID_API__) && __ANDROID_API__ < 26) || \\\n    defined(_AIX) || \\\n    defined(__MVS__) || \\\n    defined(__PASE__)"
            "#if defined(__linux__) && !defined(__GLIBC__) || \\\n    (defined(__ANDROID_API__) && __ANDROID_API__ < 26) || \\\n    defined(_AIX) || \\\n    defined(__MVS__) || \\\n    defined(__PASE__)"
            _asciichat_libuv_thread_text "${_asciichat_libuv_thread_text}")
        string(REPLACE
            "#else\nint uv__thread_getname(uv_thread_t* tid, char* name, size_t size) {\n  int r;"
            "#else\nint uv__thread_getname(uv_thread_t* tid, char* name, size_t size) {\n  int r;"
            _asciichat_libuv_thread_text "${_asciichat_libuv_thread_text}")
        file(WRITE "${_asciichat_libuv_thread_source}" "${_asciichat_libuv_thread_text}")
        # A musl target triple alone still finds the host's glibc headers.
        # Keep Clang's builtin headers, but select musl and the isolated Linux
        # headers explicitly so libuv does not reference glibc-only symbols.
        target_compile_options(uv_a PRIVATE -nostdlibinc)
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
endif()
