# =============================================================================
# WiX Installer Configuration
# =============================================================================
# Configures CPack to generate WiX (.msi) installers on Windows
#
# WiX (Windows Installer XML) creates native Microsoft Installer packages (.msi)
# that integrate properly with Windows, support enterprise deployment, and provide
# better upgrade/patch management.
#
# Prerequisites:
#   - WiX Toolset must be installed (candle.exe and light.exe in PATH)
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds "WIX" to CPACK_GENERATOR if WiX Toolset is found
#   - Configures WiX-specific CPack variables
# =============================================================================

if(NOT WIN32)
    return()
endif()

# Use centralized WiX executables from FindPrograms.cmake
# WiX v4+ uses unified 'wix' command
# WiX v3 uses candle.exe (compiler) and light.exe (linker)

if(ASCIICHAT_WIX_EXECUTABLE)
    # WiX v4+ detected - get version
    execute_process(
        COMMAND ${ASCIICHAT_WIX_EXECUTABLE} --version
        OUTPUT_VARIABLE WIX_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Extract major version (e.g., "4.0.4+a8592982" -> "4")
    string(REGEX MATCH "^([0-9]+)" WIX_MAJOR_VERSION "${WIX_VERSION_OUTPUT}")

    # Set CPACK_WIX_VERSION to tell CPack which toolset we're using
    if(WIX_MAJOR_VERSION GREATER_EQUAL 4)
        set(CPACK_WIX_VERSION "4")
        set(CPACK_WIX_EXECUTABLE "${ASCIICHAT_WIX_EXECUTABLE}")
        set(WIX_FOUND TRUE)
        message(STATUS "${Yellow}CPack:${ColorReset} WiX v4+ detected (${BoldBlue}wix${ColorReset} v${WIX_VERSION_OUTPUT} found at ${BoldBlue}${ASCIICHAT_WIX_EXECUTABLE}${ColorReset})")
        message(STATUS "${Yellow}CPack:${ColorReset} ${Magenta}WiX${ColorReset} generator will be enabled using ${BoldBlue}WiX .NET Tools${ColorReset}")
    else()
        message(STATUS "${Red}CPack:${ColorReset} ${BoldBlue}WiX${ColorReset} version ${BoldGreen}${WIX_VERSION_OUTPUT}${ColorReset} is too old (need v4+)")
    endif()
endif()

# If WiX v4+ not found, try WiX Toolset v3 from FindPrograms.cmake
if(NOT WIX_FOUND AND ASCIICHAT_WIX_CANDLE_EXECUTABLE AND ASCIICHAT_WIX_LIGHT_EXECUTABLE)
    set(CPACK_WIX_VERSION "3")
    set(WIX_FOUND TRUE)
    message(STATUS "${Yellow}CPack:${ColorReset} WiX v3 detected (${BoldBlue}candle.exe${ColorReset} found at ${BoldBlue}${ASCIICHAT_WIX_CANDLE_EXECUTABLE}${ColorReset})")
    message(STATUS "${Yellow}CPack:${ColorReset} ${Magenta}WiX${ColorReset} generator will be enabled using ${BoldBlue}WiX Toolset v3${ColorReset}")
endif()

if(NOT WIX_FOUND)
    message(STATUS "${Red}CPack:${ColorReset} WiX generator disabled - no ${BoldBlue}WiX${ColorReset} installation found")
    message(STATUS "${Yellow}CPack:${ColorReset} Install WiX v4: ${BoldBlue}dotnet tool install --global wix --version 4.0.4${ColorReset}")
    message(STATUS "${Yellow}CPack:${ColorReset} Then install UI extension: ${BoldBlue}wix extension add --global WixToolset.UI.wixext/4.0.4${ColorReset}")
    message(STATUS "${Yellow}CPack:${ColorReset} Or download WiX v3.14: ${BoldBlue}https://github.com/wixtoolset/wix3/releases/tag/wix3141rtm${ColorReset}")
    return()
endif()

# =============================================================================
# WiX Package Configuration
# =============================================================================

# Enable component-based packaging
# Note: WiX doesn't support creating truly separate .msi files per component
# Instead, this creates a single .msi with selectable features/components
# For truly separate packages on Windows, use ZIP archives (Archive.cmake)
set(CPACK_WIX_COMPONENT_INSTALL ON)

# Product name and version
set(CPACK_WIX_PRODUCT_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_WIX_PRODUCT_VERSION "${CPACK_PACKAGE_VERSION}")

# Vendor/Manufacturer
set(CPACK_WIX_VENDOR "${CPACK_PACKAGE_VENDOR}")

# =============================================================================
# Windows "Programs and Features" Properties
# =============================================================================
# These appear in Control Panel > Programs and Features

# Product icon shown in Add/Remove Programs
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/images/installer_icon.ico")

# Help and support information
if(DEFINED CPACK_PACKAGE_HOMEPAGE_URL)
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK "${CPACK_PACKAGE_HOMEPAGE_URL}")
endif()

# Update information URL
if(DEFINED CPACK_PACKAGE_HOMEPAGE_URL)
    set(CPACK_WIX_PROPERTY_ARPURLUPDATEINFO "${CPACK_PACKAGE_HOMEPAGE_URL}/releases")
endif()

# Comments shown in Add/Remove Programs
set(CPACK_WIX_PROPERTY_ARPCOMMENTS "Real-time terminal-based video chat with ASCII art conversion")

# Contact/support information
if(DEFINED CPACK_PACKAGE_CONTACT)
    set(CPACK_WIX_PROPERTY_ARPCONTACT "${CPACK_PACKAGE_CONTACT}")
endif()

# =============================================================================
# WiX UI Configuration
# =============================================================================

# Use WixUI_FeatureTree for component selection
# This provides a tree view of components (Runtime, Development, Documentation)
set(CPACK_WIX_UI_REF "WixUI_FeatureTree")

# License file - WiX requires RTF format
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/install/LICENSE.rtf")
    set(CPACK_WIX_LICENSE_RTF "${CMAKE_SOURCE_DIR}/cmake/install/LICENSE.rtf")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE.txt")
    set(CPACK_WIX_LICENSE_RTF "${CMAKE_SOURCE_DIR}/LICENSE.txt")
endif()

# Program Menu Folder (Start Menu)
set(CPACK_WIX_PROGRAM_MENU_FOLDER "${CPACK_PACKAGE_NAME}")

# Root feature customization (the top-level install feature)
set(CPACK_WIX_ROOT_FEATURE_TITLE "${PROJECT_NAME_FULL}")
set(CPACK_WIX_ROOT_FEATURE_DESCRIPTION "${PROJECT_DESCRIPTION_FULL}")

# Language/Culture for installer UI
# Default is en-US, but can be customized
set(CPACK_WIX_CULTURES "en-US")

# Architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CPACK_WIX_ARCHITECTURE "x64")
    set(CPACK_WIX_SIZEOF_VOID_P 8)
else()
    set(CPACK_WIX_ARCHITECTURE "x86")
    set(CPACK_WIX_SIZEOF_VOID_P 4)
endif()

# Install scope - install for all users (requires admin)
set(CPACK_WIX_INSTALL_SCOPE "perMachine")

# =============================================================================
# Product and Upgrade GUIDs
# =============================================================================
# These GUIDs identify the product and upgrade path
# - Product GUID: Changes with each version (allows side-by-side installs)
# - Upgrade GUID: Stays constant (allows upgrades to replace old versions)
#
# IMPORTANT: The Upgrade GUID should NEVER change for this product!
# Changing it will break upgrade detection and cause side-by-side installs.

# Upgrade GUID - NEVER CHANGE THIS!
# This GUID identifies the ascii-chat product family for upgrade detection
set(CPACK_WIX_UPGRADE_GUID "A1B2C3D4-E5F6-7890-ABCD-EF1234567890")

# Product GUID - can be "*" to auto-generate per version (recommended)
# Auto-generation ensures each version gets a unique GUID
# NOTE: I'm not sure if claude's note^ above^ about "*" actually generates it so
# just disable it because the cmake docs say it gets auto-generated if it's not
# set.
#set(CPACK_WIX_PRODUCT_GUID "*")

# =============================================================================
# Installation Behavior
# =============================================================================

# Allow upgrades and downgrades
set(CPACK_WIX_UPGRADE_BEHAVIOR "UPGRADE")

# Skip license dialog if desired (set to "1" to skip)
# set(CPACK_WIX_SKIP_PROGRAM_FOLDER ON)

set(CPACK_WIX_CMAKE_PACKAGE_REGISTRY "${PROJECT_VERSION}")

# =============================================================================
# Custom WiX Fragments
# =============================================================================
# Add custom WiX XML to create shortcuts, modify PATH, etc.

# WiX patch file for custom configuration (temporarily disabled - needs WiX v4 format)
# This will be merged with the auto-generated WiX file
set(CPACK_WIX_PATCH_FILE "${CMAKE_SOURCE_DIR}/cmake/install/wix_patch.xml")

# Create the WiX patch file if it doesn't exist
#if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cmake/install/wix_patch.xml")
#    file(WRITE "${CMAKE_SOURCE_DIR}/cmake/install/wix_patch.xml" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
#<CPackWiXPatch>
#  <!--
#    Custom WiX configuration for ascii-chat
#
#    This patch adds:
#    1. Start Menu shortcuts for ascii-chat client and server
#    2. PATH environment variable modification to add bin directory
#  -->
#
#  <!-- Add shortcuts to Start Menu -->
#  <CPackWiXFragment Id=\"CM_SHORTCUTS\">
#    <DirectoryRef Id=\"ApplicationProgramsFolder\">
#      <Component Id=\"ApplicationShortcuts\" Guid=\"B1C2D3E4-F5A6-7890-BCDE-FA1234567890\">
#        <!-- Client shortcut -->
#        <Shortcut Id=\"ClientShortcut\"
#                  Name=\"ascii-chat Client\"
#                  Description=\"Launch ascii-chat client\"
#                  Target=\"[INSTALL_ROOT]bin\\\\ascii-chat.exe\"
#                  Arguments=\"client\"
#                  WorkingDirectory=\"INSTALL_ROOT\" />
#
#        <!-- Server shortcut -->
#        <Shortcut Id=\"ServerShortcut\"
#                  Name=\"ascii-chat Server\"
#                  Description=\"Launch ascii-chat server\"
#                  Target=\"[INSTALL_ROOT]bin\\\\ascii-chat.exe\"
#                  Arguments=\"server\"
#                  WorkingDirectory=\"INSTALL_ROOT\" />
#
#        <!-- Uninstall shortcut (required by Windows Installer) -->
#        <RemoveFolder Id=\"ApplicationProgramsFolder\" On=\"uninstall\" />
#
#        <!-- Registry key for Windows Installer (required) -->
#        <RegistryValue Root=\"HKCU\"
#                      Key=\"Software\\\\ascii-chat\"
#                      Name=\"installed\"
#                      Type=\"integer\"
#                      Value=\"1\"
#                      KeyPath=\"yes\" />
#      </Component>
#    </DirectoryRef>
#  </CPackWiXFragment>
#
#  <!-- Add bin directory to PATH environment variable -->
#  <CPackWiXFragment Id=\"CM_PATH\">
#    <Component Id=\"PathEnvironment\" Guid=\"C1D2E3F4-A5B6-7890-CDEF-AB1234567890\" Directory=\"INSTALL_ROOT\">
#      <!-- Add to user PATH (HKCU) - works without admin -->
#      <Environment Id=\"PATH\"
#                  Name=\"PATH\"
#                  Value=\"[INSTALL_ROOT]bin\"
#                  Permanent=\"no\"
#                  Part=\"last\"
#                  Action=\"set\"
#                  System=\"no\" />
#
#      <!-- Registry key required for component -->
#      <RegistryValue Root=\"HKCU\"
#                    Key=\"Software\\\\ascii-chat\"
#                    Name=\"path\"
#                    Type=\"integer\"
#                    Value=\"1\"
#                    KeyPath=\"yes\" />
#    </Component>
#  </CPackWiXFragment>
#
#  <!-- Install the custom components -->
#  <CPackWiXFragment Id=\"CM_INSTALL_COMPONENTS\">
#    <Feature Id=\"ProductFeature\">
#      <ComponentRef Id=\"ApplicationShortcuts\" />
#      <ComponentRef Id=\"PathEnvironment\" />
#    </Feature>
#  </CPackWiXFragment>
#</CPackWiXPatch>
#")
#    message(STATUS "${Yellow}CPack:${ColorReset} Created WiX patch file: ${BoldBlue}cmake/install/wix_patch.xml${ColorReset}")
#endif()

# =============================================================================
# Additional WiX Configuration
# =============================================================================

# Additional compiler/linker flags for WiX (optional)
# set(CPACK_WIX_CANDLE_EXTRA_FLAGS "")
# set(CPACK_WIX_LIGHT_EXTRA_FLAGS "-sval")  # Skip validation for faster builds
