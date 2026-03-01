# =============================================================================
# Fontconfig Dependency
# =============================================================================
# Cross-platform configuration for fontconfig font resolution
#
# For musl builds: fontconfig is not built from source (it depends on GTK ecosystem)
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - FONTCONFIG_LDFLAGS: Fontconfig libraries to link
#   - FONTCONFIG_INCLUDE_DIRS: Fontconfig include directories
# =============================================================================

# Musl builds: fontconfig is only available through the GTK4 ecosystem build
# which is intentionally excluded (NO GTK ecosystem requirement per task)
if(USE_MUSL)
    message(STATUS "${BoldYellow}⚠${ColorReset} Fontconfig: Not built for musl (GTK ecosystem excluded)")
    set(FONTCONFIG_LDFLAGS "")
    set(FONTCONFIG_INCLUDE_DIRS "")
    return()
endif()

# Non-musl builds: Use system package manager
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
