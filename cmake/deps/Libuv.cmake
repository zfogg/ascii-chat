# Native network I/O uses epoll, kqueue or IOCP through libuv. Browser transports
# own their event loops and do not link this native dependency.
function(configure_libuv)
    # The selected provider is a target property, not a sticky cache result.
    # Clear stale pkg-config selections so Homebrew's libuv cannot survive a
    # reconfigure after the distro library becomes available.
    unset(ASCIICHAT_LIBUV_TARGET)
    unset(ASCIICHAT_LIBUV_TARGET CACHE)
    if(EMSCRIPTEN)
        return()
    endif()

    if(WIN32)
        if(NOT USE_VCPKG)
            message(FATAL_ERROR
                "Windows builds require the restored vcpkg libuv package; "
                "enable USE_VCPKG and restore the selected triplet in the dependency cache")
        endif()
        find_package(libuv CONFIG REQUIRED)
        if(BUILD_SHARED_LIBS AND TARGET libuv::uv)
            set(ASCIICHAT_LIBUV_TARGET libuv::uv)
        elseif(TARGET libuv::uv_a)
            set(ASCIICHAT_LIBUV_TARGET libuv::uv_a)
        else()
            # Older/restored vcpkg packages may not export CMake targets.
            # Preserve the direct-import fallback used by master.
            find_path(ASCIICHAT_LIBUV_INCLUDE_DIR NAMES uv.h
                PATHS "${VCPKG_INCLUDE_PATH}" PATH_SUFFIXES uv REQUIRED)
            if(BUILD_SHARED_LIBS)
                find_library(ASCIICHAT_LIBUV_LIBRARY NAMES uv PATHS "${VCPKG_LIB_PATH}" REQUIRED)
            else()
                find_library(ASCIICHAT_LIBUV_LIBRARY NAMES uv_a uv PATHS "${VCPKG_LIB_PATH}" REQUIRED)
            endif()
            add_library(asciichat-libuv UNKNOWN IMPORTED GLOBAL)
            set_target_properties(asciichat-libuv PROPERTIES
                IMPORTED_LOCATION "${ASCIICHAT_LIBUV_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ASCIICHAT_LIBUV_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES userenv)
            set(ASCIICHAT_LIBUV_TARGET asciichat-libuv)
        endif()
    elseif(USE_MUSL)
        include(FetchContent)
        set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        # libuv selects its shared/static target from this option, not from
        # BUILD_SHARED_LIBS.  Force the musl provider to expose uv_a even when
        # a prior native configure left LIBUV_BUILD_SHARED cached as ON.
        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        asciichat_fetchcontent_declare(asciichat_libuv
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/libuv"
            EXCLUDE_FROM_ALL
            GIT_REPOSITORY https://github.com/libuv/libuv.git
            GIT_TAG v1.51.0
            GIT_SHALLOW TRUE)
        # We need the populated source directory to patch libuv's provider
        # selector before its subdirectory is configured.  CMake 4.4 makes
        # the legacy populate call fatal unless this policy is scoped here.
        cmake_policy(SET CMP0169 OLD)
        FetchContent_Populate(asciichat_libuv)
        # libuv's exported-target selector incorrectly keys off the parent
        # BUILD_SHARED_LIBS value, although its build options use
        # LIBUV_BUILD_SHARED.  Keep that provider-local choice consistent so
        # musl never attempts to alias a shared target it did not create.
        set(_asciichat_libuv_cmake "${asciichat_libuv_SOURCE_DIR}/CMakeLists.txt")
        file(READ "${_asciichat_libuv_cmake}" _asciichat_libuv_cmake_text)
        string(REPLACE
            "if(BUILD_SHARED_LIBS)\n  set(LIB_SELECTED uv)"
            "if(LIBUV_BUILD_SHARED)\n  set(LIB_SELECTED uv)"
            _asciichat_libuv_cmake_text "${_asciichat_libuv_cmake_text}")
        file(WRITE "${_asciichat_libuv_cmake}" "${_asciichat_libuv_cmake_text}")
        add_subdirectory(
            "${asciichat_libuv_SOURCE_DIR}"
            "${asciichat_libuv_BINARY_DIR}"
            EXCLUDE_FROM_ALL)
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
        set(ASCIICHAT_LIBUV_TARGET uv_a)
    else()
        # Prefer the distro libuv on Linux.  Homebrew can be earlier in
        # PKG_CONFIG_PATH and may provide a shared object built against a
        # newer glibc than the host; that is not a valid system dependency.
        if(UNIX AND NOT APPLE)
            unset(_asciichat_libuv_include CACHE)
            unset(_asciichat_libuv_library CACHE)
            find_path(_asciichat_libuv_include NAMES uv.h
                PATHS /usr/include /usr/local/include NO_DEFAULT_PATH)
            if(BUILD_SHARED_LIBS)
                find_library(_asciichat_libuv_library NAMES uv
                    PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib
                    NO_DEFAULT_PATH)
            else()
                find_library(_asciichat_libuv_library NAMES uv_a uv
                    PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib
                    NO_DEFAULT_PATH)
            endif()
            if(_asciichat_libuv_include AND _asciichat_libuv_library)
                add_library(asciichat-libuv UNKNOWN IMPORTED GLOBAL)
                set_target_properties(asciichat-libuv PROPERTIES
                    IMPORTED_LOCATION "${_asciichat_libuv_library}"
                    INTERFACE_INCLUDE_DIRECTORIES "${_asciichat_libuv_include}"
                    INTERFACE_LINK_LIBRARIES "dl;pthread")
                set(ASCIICHAT_LIBUV_TARGET asciichat-libuv)
            endif()
        endif()
        if(NOT ASCIICHAT_LIBUV_TARGET)
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

    if(NOT ASCIICHAT_LIBUV_TARGET)
        message(FATAL_ERROR "libuv configuration did not produce a link target")
    endif()

    # Export one stable target.  Provider-specific targets and discovery
    # variables stay private to this module.
    if(NOT TARGET asciichat-libuv)
        add_library(asciichat-libuv INTERFACE)
        target_link_libraries(asciichat-libuv INTERFACE "${ASCIICHAT_LIBUV_TARGET}")
    endif()
    if(NOT TARGET asciichat::libuv)
        add_library(asciichat::libuv ALIAS asciichat-libuv)
    endif()
endfunction()

configure_libuv()
