# Keep vcpkg's installed packages in ascii-chat's persistent dependency cache.
if(NOT ASCIICHAT_DEPS_CACHE_ROOT)
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.deps-cache"
        CACHE PATH "Root directory for dependency cache" FORCE)
endif()
set(VCPKG_INSTALLED_DIR "${ASCIICHAT_DEPS_CACHE_ROOT}/vcpkg"
    CACHE PATH "Persistent vcpkg installed dependency tree" FORCE)

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
elseif(WIN32 AND (CMAKE_GENERATOR_PLATFORM STREQUAL "x64"
        OR CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64"))
    set(_asciichat_vcpkg_triplet "x64-windows")
endif()
if(_asciichat_vcpkg_triplet AND EXISTS
        "${VCPKG_INSTALLED_DIR}/${_asciichat_vcpkg_triplet}")
    set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL
        "Do not install an existing vcpkg triplet during CMake configure" FORCE)
endif()

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
