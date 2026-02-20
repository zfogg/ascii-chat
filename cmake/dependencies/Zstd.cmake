# =============================================================================
# zstd Compression Library Configuration
# =============================================================================
# Finds and configures zstd compression library using CMake's find_package()
#
# Outputs:
#   - zstd::zstd - Imported target for linking
# =============================================================================

# Skip for musl builds - zstd is configured in MuslDependencies.cmake
if(USE_MUSL)
    return()
endif()

# Try to find zstd via CMake config first, then fall back to pkg-config
find_package(zstd QUIET CONFIG)

if(NOT zstd_FOUND)
    # Fall back to pkg-config if CMake config not found
    include(FindPkgConfig)
    pkg_check_modules(zstd REQUIRED libzstd)

    # Create interface library for compatibility
    if(NOT TARGET zstd::zstd)
        add_library(zstd::zstd INTERFACE IMPORTED)
        target_include_directories(zstd::zstd INTERFACE ${zstd_INCLUDE_DIRS})
        target_link_libraries(zstd::zstd INTERFACE ${zstd_LIBRARIES})
    endif()
endif()

# Set uppercase variable for consistency with build system
set(ZSTD_FOUND TRUE)

message(STATUS "${BoldGreen}✓${ColorReset} zstd found")
