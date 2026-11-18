# Keep vcpkg's installed packages in ascii-chat's persistent dependency cache.
set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.deps-cache/vcpkg"
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

# A restored dependency cache can contain a complete static triplet without
# vcpkg's global manifest status file.  Do not make CMake rebuild FFmpeg and
# the other static packages merely because that status file is absent.
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
    foreach(_cached_triplet IN ITEMS x64-windows-static arm64-windows-static)
        if(EXISTS "${VCPKG_INSTALLED_DIR}/${_cached_triplet}/lib/avcodec.lib"
           AND EXISTS "${VCPKG_INSTALLED_DIR}/${_cached_triplet}/lib/avformat.lib"
           AND EXISTS "${VCPKG_INSTALLED_DIR}/${_cached_triplet}/lib/avutil.lib")
            set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL
                "Use the restored ascii-chat vcpkg cache without reinstalling packages" FORCE)
            message(STATUS "vcpkg: using complete cached static triplet ${_cached_triplet}")
            break()
        endif()
    endforeach()
endif()

if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "VCPKG_ROOT must be set to use the ascii-chat vcpkg toolchain")
endif()

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
