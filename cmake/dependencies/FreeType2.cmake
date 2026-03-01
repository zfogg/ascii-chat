# =============================================================================
# FreeType2 Dependency
# =============================================================================
# Cross-platform configuration for FreeType2 font rasterization
#
# Outputs (variables set by this file):
#   - FREETYPE_LIBRARIES: FreeType2 libraries to link
#   - FREETYPE_INCLUDE_DIRS: FreeType2 include directories
# =============================================================================

if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(Freetype REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: ${FREETYPE_LIBRARIES}")

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(Freetype REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: ${FREETYPE_LIBRARIES}")

elseif(WIN32)
    # Windows: Use vcpkg
    find_package(freetype CONFIG REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: vcpkg")

else()
    message(FATAL_ERROR "Unsupported platform for FreeType2")
endif()
