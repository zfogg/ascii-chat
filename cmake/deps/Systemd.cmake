# Build libsystemd from source for static musl releases.
if(NOT USE_MUSL)
    if(PLATFORM_LINUX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBSYSTEMD REQUIRED libsystemd)
        add_library(asciichat-systemd INTERFACE)
        target_include_directories(asciichat-systemd INTERFACE ${LIBSYSTEMD_INCLUDE_DIRS})
        target_link_libraries(asciichat-systemd INTERFACE ${LIBSYSTEMD_LIBRARIES})
        target_compile_definitions(asciichat-systemd INTERFACE HAVE_LIBSYSTEMD=1)
        set(LIBSYSTEMD_INCLUDE_DIRS "${LIBSYSTEMD_INCLUDE_DIRS}")
        set(LIBSYSTEMD_LIBRARIES asciichat-systemd)
        set(LIBSYSTEMD_FOUND TRUE)
    endif()
    return()
endif()

include(ExternalProject)
find_program(ASCIICHAT_MESON_EXECUTABLE meson REQUIRED)

set(SYSTEMD_VERSION 256.9)
set(SYSTEMD_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/musl/systemd-${SYSTEMD_VERSION}")
set(SYSTEMD_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/musl/systemd-${SYSTEMD_VERSION}")
set(SYSTEMD_SOURCE "${SYSTEMD_CACHE_DIR}/src")
set(SYSTEMD_BUILD "${SYSTEMD_CACHE_DIR}/build")
set(SYSTEMD_ARCHIVE "${SYSTEMD_CACHE_DIR}/systemd-${SYSTEMD_VERSION}.tar.gz")
file(MAKE_DIRECTORY "${SYSTEMD_CACHE_DIR}")

if(NOT EXISTS "${SYSTEMD_ARCHIVE}")
    file(DOWNLOAD
        "https://github.com/systemd/systemd/archive/refs/tags/v${SYSTEMD_VERSION}.tar.gz"
        "${SYSTEMD_ARCHIVE}"
        EXPECTED_HASH SHA256=9f2bda967a30ec4602e7ea93d565eb43670ca1dffbb808c72758d5f0213508c8
        SHOW_PROGRESS)
endif()
if(NOT EXISTS "${SYSTEMD_SOURCE}/meson.build")
    file(MAKE_DIRECTORY "${SYSTEMD_SOURCE}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${SYSTEMD_ARCHIVE}"
        WORKING_DIRECTORY "${SYSTEMD_CACHE_DIR}")
    file(RENAME "${SYSTEMD_CACHE_DIR}/systemd-${SYSTEMD_VERSION}" "${SYSTEMD_SOURCE}")
endif()

set(_systemd_cc "${ASCIICHAT_CLANG_EXECUTABLE}")
set(_systemd_cxx "${ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE}")
set(_systemd_cflags "--target=${MUSL_TARGET} ${MUSL_KERNEL_CFLAGS}")
set(_systemd_ldflags "--target=${MUSL_TARGET} -fuse-ld=lld -static")

ExternalProject_Add(asciichat-systemd
    SOURCE_DIR "${SYSTEMD_SOURCE}"
    BINARY_DIR "${SYSTEMD_BUILD}"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        CC=${_systemd_cc} CXX=${_systemd_cxx}
        CFLAGS=${_systemd_cflags} CXXFLAGS=${_systemd_cflags}
        LDFLAGS=${_systemd_ldflags}
        ${ASCIICHAT_MESON_EXECUTABLE} setup "${SYSTEMD_BUILD}" "${SYSTEMD_SOURCE}"
        --prefix=${SYSTEMD_PREFIX} --libdir=lib --buildtype=release
        -Dmode=release -Dtests=false -Dman=false -Ddocs=false
        -Dstatic-libsystemd=enabled -Dstatic-libsystemd-pic=enabled
        -Dstatic-libudev=false -Dudev=false -Dnetworkd=false -Dresolve=false
        -Ddefault-hierarchy=unified
    BUILD_COMMAND ${ASCIICHAT_MESON_EXECUTABLE} compile -C "${SYSTEMD_BUILD}" libsystemd
    INSTALL_COMMAND ${ASCIICHAT_MESON_EXECUTABLE} install -C "${SYSTEMD_BUILD}"
    BUILD_BYPRODUCTS "${SYSTEMD_PREFIX}/lib/libsystemd.a"
    USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE USES_TERMINAL_INSTALL TRUE)

add_library(asciichat-systemd-static STATIC IMPORTED GLOBAL)
set_target_properties(asciichat-systemd-static PROPERTIES
    IMPORTED_LOCATION "${SYSTEMD_PREFIX}/lib/libsystemd.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SYSTEMD_PREFIX}/include")
add_dependencies(asciichat-systemd-static asciichat-systemd)
set(LIBSYSTEMD_INCLUDE_DIRS "${SYSTEMD_PREFIX}/include")
set(LIBSYSTEMD_LIBRARIES asciichat-systemd-static)
set(LIBSYSTEMD_FOUND TRUE)
