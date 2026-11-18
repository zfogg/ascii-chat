# Keep vcpkg's installed packages in ascii-chat's persistent dependency cache.
set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.deps-cache/vcpkg"
    CACHE PATH "Persistent vcpkg installed dependency tree" FORCE)

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
