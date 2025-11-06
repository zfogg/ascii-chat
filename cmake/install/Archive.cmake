# =============================================================================
# Archive Package Configuration (Cross-Platform)
# =============================================================================
# Configures CPack to generate archive packages (ZIP, TGZ, TBZ2, TXZ)
#
# Archive formats are always available and work on all platforms:
#   - TGZ (.tar.gz) - Unix/Linux standard
#   - ZIP (.zip) - Windows/cross-platform
#   - TBZ2 (.tar.bz2) - Better compression than TGZ
#   - TXZ (.tar.xz) - Best compression
#
# Prerequisites:
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds archive generators to CPACK_GENERATOR
# =============================================================================

# =============================================================================
# Platform-Specific Archive Selection
# =============================================================================

if(UNIX AND NOT APPLE)
    # Linux: TGZ is always available, add ZIP if zip command exists
    list(APPEND CPACK_GENERATOR "TGZ")
    message(STATUS "${Yellow}CPack:${ColorReset} TGZ generator enabled (always available)")

    # Check for zip command
    find_program(ZIP_EXECUTABLE zip)
    if(ZIP_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "ZIP")
        message(STATUS "${Yellow}CPack:${ColorReset} ZIP generator enabled (${BoldBlue}zip${ColorReset} found)")
    else()
        message(STATUS "${Yellow}CPack:${ColorReset} ZIP generator disabled (${BoldBlue}zip${ColorReset} not found)")
    endif()

elseif(APPLE)
    # macOS: TGZ is always available
    list(APPEND CPACK_GENERATOR "TGZ")
    message(STATUS "${Yellow}CPack:${ColorReset} TGZ generator enabled (always available)")

elseif(WIN32)
    # Windows: ZIP is always available (uses CMake's built-in tar command)
    # Only add if not already in the list (it may have been set as default if WiX not found)
    if(NOT "ZIP" IN_LIST CPACK_GENERATOR)
        list(APPEND CPACK_GENERATOR "ZIP")
        message(STATUS "${Yellow}CPack:${ColorReset} ZIP generator enabled (always available)")
    endif()

else()
    # Unknown platform: fallback to TGZ
    list(APPEND CPACK_GENERATOR "TGZ")
    message(STATUS "${Yellow}CPack:${ColorReset} Using fallback generator ${Magenta}TGZ${ColorReset} for unknown platform")
endif()

# =============================================================================
# Source Package Configuration (Optional)
# =============================================================================
# Source packages contain the source code for distribution

set(CPACK_SOURCE_GENERATOR "TGZ")
if(UNIX AND NOT APPLE)
    find_program(ZIP_EXECUTABLE zip)
    if(ZIP_EXECUTABLE)
        list(APPEND CPACK_SOURCE_GENERATOR "ZIP")
    endif()
endif()

# Source package ignore patterns
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/build_release/"
    "/\\.git/"
    "/deps/"
    "/\\.deps-cache/"
    "/\\.deps-cache-docker/"
    "/\\.vscode/"
    "/\\.idea/"
    "/CMakeFiles/"
    "/CMakeCache\\.txt$"
    "/cmake_install\\.cmake$"
    "/\\.ninja_log$"
    "/\\.ninja_deps$"
    "/compile_commands\\.json$"
)

message(STATUS "${Yellow}CPack:${ColorReset} Archive configuration complete")
