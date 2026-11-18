# Keep vcpkg's installed packages in ascii-chat's persistent dependency cache.
if(NOT ASCIICHAT_DEPS_CACHE_ROOT)
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.deps-cache"
        CACHE PATH "Root directory for dependency cache" FORCE)
endif()
set(VCPKG_INSTALLED_DIR "${ASCIICHAT_DEPS_CACHE_ROOT}/vcpkg"
    CACHE PATH "Persistent vcpkg installed dependency tree" FORCE)

# Build only the vcpkg configuration selected by this single-config CMake
# build.  Without this, vcpkg builds both release and debug variants even
# though ascii-chat links exactly one of them.  Keep the custom Dev
# configuration on vcpkg's Debug variant, matching SelectLibraryConfig.cmake
# and the runtime DLL staging in PostBuild.cmake.
if(CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev)$")
    set(VCPKG_BUILD_TYPE "debug" CACHE STRING
        "vcpkg dependency configuration to build" FORCE)
elseif(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
    set(VCPKG_BUILD_TYPE "release" CACHE STRING
        "vcpkg dependency configuration to build" FORCE)
endif()
message(STATUS "vcpkg: building only ${VCPKG_BUILD_TYPE} dependency variants")

if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "VCPKG_ROOT must be set to use the ascii-chat vcpkg toolchain")
endif()

# Consume an already restored triplet without invoking installation. If the
# selected triplet is absent, leave manifest installation enabled so vcpkg can
# populate the missing dependency tree.
if(VCPKG_TARGET_TRIPLET)
    set(_asciichat_vcpkg_triplet "${VCPKG_TARGET_TRIPLET}")
elseif(VCPKG_DEFAULT_TRIPLET)
    set(_asciichat_vcpkg_triplet "${VCPKG_DEFAULT_TRIPLET}")
elseif(WIN32)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "^(ARM64|arm64)$"
            OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
        set(_asciichat_vcpkg_triplet "arm64-windows")
    elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^(ARM|arm)$"
            OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM|arm)$")
        set(_asciichat_vcpkg_triplet "arm-windows")
    elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^(Win32|x86)$"
            OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86|i[3-6]86)$")
        set(_asciichat_vcpkg_triplet "x86-windows")
    elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^(x64|AMD64|amd64)$"
            OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        set(_asciichat_vcpkg_triplet "x64-windows")
    endif()
endif()

# A restored dependency cache can contain a complete triplet without vcpkg's
# global manifest status file.  Disable manifest installation only when the
# selected triplet contains the packages this project actually requires.
# Checking libuv's config file is important: an empty or partially restored
# triplet directory must still be allowed to install missing dependencies.
set(_asciichat_vcpkg_triplet_root
    "${VCPKG_INSTALLED_DIR}/${_asciichat_vcpkg_triplet}")
if(_asciichat_vcpkg_triplet AND EXISTS "${_asciichat_vcpkg_triplet_root}"
        AND EXISTS "${_asciichat_vcpkg_triplet_root}/share/libuv/libuvConfig.cmake")
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT BUILD_SHARED_LIBS
            AND EXISTS "${_asciichat_vcpkg_triplet_root}/lib/avcodec.lib"
            AND EXISTS "${_asciichat_vcpkg_triplet_root}/lib/avformat.lib"
            AND EXISTS "${_asciichat_vcpkg_triplet_root}/lib/avutil.lib")
        message(STATUS "vcpkg: using complete cached static triplet ${_asciichat_vcpkg_triplet}")
    else()
        message(STATUS "vcpkg: using restored triplet ${_asciichat_vcpkg_triplet}")
    endif()
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL
        "Do not install an existing vcpkg triplet during CMake configure" FORCE)
endif()

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
