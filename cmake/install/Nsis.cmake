# =============================================================================
# NSIS Installer Configuration
# =============================================================================
# Configures CPack to generate NSIS (.exe) installers on Windows
#
# NSIS (Nullsoft Scriptable Install System) creates self-extracting executable installers
# that work without admin privileges and are easy to distribute.
#
# Prerequisites:
#   - NSIS must be installed (makensis.exe in PATH)
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds "NSIS" to CPACK_GENERATOR if makensis is found
#   - Configures NSIS-specific CPack variables
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/CPackGenerator.cmake)

enable_cpack_generator(
    NAME "NSIS"
    PLATFORM WIN32
    REQUIRED_TOOL ASCIICHAT_NSIS_EXECUTABLE
    TOOL_DISPLAY_NAME "makensis"
)

if(NOT NSIS_GENERATOR_ENABLED)
    return()
endif()

# =============================================================================
# NSIS Package Configuration
# =============================================================================

set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")

# Display name without version (just "ascii-chat")
set(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_NAME}")

# CPACK_PACKAGE_INSTALL_DIRECTORY controls the install directory
# This ensures NSIS installs to "ascii-chat" instead of "ascii-chat 0.3.4"
set(CPACK_NSIS_HELP_LINK "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")

# =============================================================================
# PATH Environment Variable Configuration
# =============================================================================
# Add bin directory to PATH using custom NSIS code
# CPACK_NSIS_MODIFY_PATH only adds $INSTDIR, but we need $INSTDIR\bin
# This ensures 'ascii-chat' can be run from any directory
# The installer will add to user PATH (HKCU) for non-admin installations

set(CPACK_NSIS_MODIFY_PATH ON)

# Custom NSIS code to add bin directory to PATH
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    ; Add bin directory to user PATH (works without admin)
    EnVar::SetHKCU
    EnVar::AddValue \\\"PATH\\\" \\\"$INSTDIR\\\\bin\\\"
    Pop \\\$0
")

set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    ; Remove bin directory from user PATH
    EnVar::SetHKCU
    EnVar::DeleteValue \\\"PATH\\\" \\\"$INSTDIR\\\\bin\\\"
    Pop \\\$0
")

# =============================================================================
# Installer Behavior
# =============================================================================

# Enable uninstall before install (clean upgrade)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

# Enable component-based installation
# This allows users to select which components to install
set(CPACK_NSIS_COMPONENT_INSTALL ON)

# Custom installer icon
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/images/installer_icon.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_SOURCE_DIR}/images/installer_icon.ico")

# =============================================================================
# Start Menu Shortcuts
# =============================================================================
# Create shortcuts with command-line arguments using custom NSIS code
# NSIS CreateShortCut syntax: CreateShortCut "link.lnk" "target.exe" "parameters" "icon.file" icon_index start_options

set(CPACK_NSIS_CREATE_ICONS_EXTRA "
  CreateShortCut \\\"\$SMPROGRAMS\\\\\$STARTMENU_FOLDER\\\\ascii-chat Client.lnk\\\" \\\"\$INSTDIR\\\\bin\\\\ascii-chat.exe\\\" \\\"client\\\"
  CreateShortCut \\\"\$SMPROGRAMS\\\\\$STARTMENU_FOLDER\\\\ascii-chat Server.lnk\\\" \\\"\$INSTDIR\\\\bin\\\\ascii-chat.exe\\\" \\\"server\\\"
")

# Remove custom shortcuts on uninstall
set(CPACK_NSIS_DELETE_ICONS_EXTRA "
  Delete \\\"\$SMPROGRAMS\\\\\$STARTMENU_FOLDER\\\\ascii-chat Client.lnk\\\"
  Delete \\\"\$SMPROGRAMS\\\\\$STARTMENU_FOLDER\\\\ascii-chat Server.lnk\\\"
")

# Add documentation link to Start Menu
# CPACK_NSIS_MENU_LINKS uses pairs of (source_file, display_name)
# The source_file path is relative to the installation directory
set(CPACK_NSIS_MENU_LINKS
    "doc/html/index.html" "Documentation"
   
)

message(STATUS "${Yellow}CPack:${ColorReset} NSIS configuration complete")
