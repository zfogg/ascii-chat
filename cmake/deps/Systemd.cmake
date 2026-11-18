# Build libsystemd from source for static musl releases.
if(NOT USE_MUSL)
    if(PLATFORM_LINUX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(_asciichat_systemd REQUIRED libsystemd)
        add_library(asciichat-systemd INTERFACE)
        target_include_directories(asciichat-systemd INTERFACE ${_asciichat_systemd_INCLUDE_DIRS})
        target_link_libraries(asciichat-systemd INTERFACE ${_asciichat_systemd_LIBRARIES})
        target_compile_definitions(asciichat-systemd INTERFACE HAVE_LIBSYSTEMD=1)
    endif()
    return()
endif()

include(ExternalProject)
find_program(ASCIICHAT_MESON_EXECUTABLE meson REQUIRED)
find_program(ASCIICHAT_MAKE_EXECUTABLE make REQUIRED)

set(SYSTEMD_VERSION 256.9)
set(SYSTEMD_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/musl/systemd-${SYSTEMD_VERSION}")
set(SYSTEMD_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/musl/systemd-${SYSTEMD_VERSION}")
set(SYSTEMD_SOURCE "${SYSTEMD_CACHE_DIR}/src")
set(SYSTEMD_BUILD "${SYSTEMD_CACHE_DIR}/build")
set(SYSTEMD_ARCHIVE "${SYSTEMD_CACHE_DIR}/systemd-${SYSTEMD_VERSION}.tar.gz")
file(MAKE_DIRECTORY "${SYSTEMD_CACHE_DIR}")
# Imported targets validate their interface include directories during CMake
# generation. Meson creates this directory during install, so stage it up front.
file(MAKE_DIRECTORY "${SYSTEMD_PREFIX}/include")

# systemd's static library requires libmount and libblkid.  Build those
# libraries from util-linux with the musl toolchain; using Ubuntu's glibc
# archives here would invalidate the fully-static musl artifact.
set(UTIL_LINUX_VERSION 2.40.4)
set(UTIL_LINUX_CACHE_DIR
    "${ASCIICHAT_DEPS_CACHE_DIR}/musl/util-linux-${UTIL_LINUX_VERSION}")
set(UTIL_LINUX_ARCHIVE
    "${UTIL_LINUX_CACHE_DIR}/util-linux-${UTIL_LINUX_VERSION}.tar.xz")
set(UTIL_LINUX_SOURCE "${UTIL_LINUX_CACHE_DIR}/src")
set(UTIL_LINUX_BUILD "${UTIL_LINUX_CACHE_DIR}/build")
set(UTIL_LINUX_PREFIX "${UTIL_LINUX_CACHE_DIR}/prefix")
file(MAKE_DIRECTORY "${UTIL_LINUX_CACHE_DIR}")
if(NOT EXISTS "${UTIL_LINUX_ARCHIVE}")
    file(DOWNLOAD
        "https://mirrors.edge.kernel.org/pub/linux/utils/util-linux/v2.40/util-linux-${UTIL_LINUX_VERSION}.tar.xz"
        "${UTIL_LINUX_ARCHIVE}"
        EXPECTED_HASH SHA256=5c1daf733b04e9859afdc3bd87cc481180ee0f88b5c0946b16fdec931975fb79
        SHOW_PROGRESS)
endif()
if(NOT EXISTS "${UTIL_LINUX_SOURCE}/configure")
    file(MAKE_DIRECTORY "${UTIL_LINUX_SOURCE}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${UTIL_LINUX_ARCHIVE}"
        WORKING_DIRECTORY "${UTIL_LINUX_CACHE_DIR}")
    file(RENAME "${UTIL_LINUX_CACHE_DIR}/util-linux-${UTIL_LINUX_VERSION}"
        "${UTIL_LINUX_SOURCE}")
endif()

set(_util_linux_cc "${ASCIICHAT_CLANG_EXECUTABLE}")
set(_util_linux_cflags "-nostdlibinc -isystem ${MUSL_INCLUDE_DIR} ${MUSL_KERNEL_CFLAGS} ${ASCIICHAT_MUSL_TARGET_FLAG}")
set(_util_linux_ldflags "${ASCIICHAT_MUSL_TARGET_FLAG} -fuse-ld=lld -static -L${MUSL_LIBDIR}")
ExternalProject_Add(asciichat-util-linux-musl
    SOURCE_DIR "${UTIL_LINUX_SOURCE}"
    BINARY_DIR "${UTIL_LINUX_BUILD}"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        CC=${_util_linux_cc}
        CFLAGS=${_util_linux_cflags}
        LDFLAGS=${_util_linux_ldflags}
        ac_cv_func_getttynam=no
        ac_cv_func_scandirat=no
        "${UTIL_LINUX_SOURCE}/configure"
        --host=${MUSL_TARGET}
        --prefix=${UTIL_LINUX_PREFIX}
        --libdir=${UTIL_LINUX_PREFIX}/lib
        --disable-shared --enable-static --disable-all-programs
        --enable-libmount --enable-libblkid --disable-libsmartcols
        --disable-libuuid --disable-libfdisk --disable-liblastlog2 --disable-nls
        ac_cv_func_getttynam=no
        ac_cv_func_scandirat=no
    BUILD_COMMAND ${ASCIICHAT_MAKE_EXECUTABLE} -C "${UTIL_LINUX_BUILD}" -j
        libmount.la libblkid.la
    INSTALL_COMMAND ${ASCIICHAT_MAKE_EXECUTABLE} -C "${UTIL_LINUX_BUILD}" install
    BUILD_BYPRODUCTS
        "${UTIL_LINUX_PREFIX}/lib/libmount.a"
        "${UTIL_LINUX_PREFIX}/lib/libblkid.a"
    USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE USES_TERMINAL_INSTALL TRUE)

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
set(_systemd_cflags "${ASCIICHAT_MUSL_TARGET_FLAG} ${MUSL_KERNEL_CFLAGS}")
set(_systemd_cppflags "-I${UTIL_LINUX_PREFIX}/include")
set(_systemd_ldflags "${ASCIICHAT_MUSL_TARGET_FLAG} -fuse-ld=lld -static -L${UTIL_LINUX_PREFIX}/lib")

ExternalProject_Add(asciichat-systemd-build
    SOURCE_DIR "${SYSTEMD_SOURCE}"
    BINARY_DIR "${SYSTEMD_BUILD}"
    DEPENDS asciichat-util-linux-musl
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        CC=${_systemd_cc} CXX=${_systemd_cxx}
        CFLAGS=${_systemd_cflags} CXXFLAGS=${_systemd_cflags}
        CPPFLAGS=${_systemd_cppflags}
        LDFLAGS=${_systemd_ldflags}
        PKG_CONFIG_PATH=${UTIL_LINUX_PREFIX}/lib/pkgconfig
        ${ASCIICHAT_MESON_EXECUTABLE} setup "${SYSTEMD_BUILD}" "${SYSTEMD_SOURCE}"
        --prefix=${SYSTEMD_PREFIX} --libdir=lib --buildtype=release
        -Dmode=release -Dtests=false -Dman=false
        -Dstatic-libsystemd=pic
        -Ddefault-library=static
        -Dstatic-libudev=false -Dnetworkd=false -Dresolve=false
        -Ddefault-hierarchy=unified
    BUILD_COMMAND ninja -C "${SYSTEMD_BUILD}" libsystemd.a
    INSTALL_COMMAND ${ASCIICHAT_MESON_EXECUTABLE} install --no-rebuild --tags devel -C "${SYSTEMD_BUILD}"
    BUILD_BYPRODUCTS "${SYSTEMD_PREFIX}/lib/libsystemd.a"
    USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE USES_TERMINAL_INSTALL TRUE)

add_library(asciichat-systemd STATIC IMPORTED GLOBAL)
set_target_properties(asciichat-systemd PROPERTIES
    IMPORTED_LOCATION "${SYSTEMD_PREFIX}/lib/libsystemd.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SYSTEMD_PREFIX}/include")
add_dependencies(asciichat-systemd asciichat-systemd-build)
