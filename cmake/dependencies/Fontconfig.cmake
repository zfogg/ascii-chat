# =============================================================================
# Fontconfig Dependency
# =============================================================================
# Cross-platform configuration for fontconfig font resolution
#
# Outputs (variables set by this file):
#   - FONTCONFIG_LDFLAGS: Fontconfig libraries to link
#   - FONTCONFIG_INCLUDE_DIRS: Fontconfig include directories
# =============================================================================

if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(WIN32)
    # Windows: Use vcpkg
    find_package(unofficial-fontconfig CONFIG REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} Fontconfig: vcpkg")

else()
    message(FATAL_ERROR "Unsupported platform for Fontconfig")
endif()
