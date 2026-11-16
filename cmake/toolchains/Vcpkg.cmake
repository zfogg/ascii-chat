# Keep vcpkg's installed packages in ascii-chat's persistent dependency cache.
set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.deps-cache/vcpkg"
    CACHE PATH "Persistent vcpkg installed dependency tree" FORCE)

if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "VCPKG_ROOT must be set to use the ascii-chat vcpkg toolchain")
endif()

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
