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

find_package(zstd REQUIRED)

# Set uppercase variable for consistency with build system
set(ZSTD_FOUND TRUE)

message(STATUS "${BoldGreen}✓${ColorReset} zstd found: ${zstd_DIR}")
