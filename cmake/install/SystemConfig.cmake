# =============================================================================
# System Configuration Directory Setup
# =============================================================================
# This module configures the system-wide configuration directory (SYSCONFDIR)
# and installs the example system config file.
#
# System config path is: ${CMAKE_INSTALL_PREFIX}/etc/ascii-chat/config.toml
#
# Override with: cmake -DCMAKE_INSTALL_PREFIX=/custom/prefix
# =============================================================================

# System config is installed to: ${CMAKE_INSTALL_PREFIX}/etc/ascii-chat/config.toml
# Example: /usr/local/etc/ascii-chat/config.toml (Linux/macOS)
#          /opt/homebrew/etc/ascii-chat/config.toml (Homebrew)
#          C:\Program Files\ascii-chat\etc\ascii-chat\config.toml (Windows)

# Full path to system config file (for documentation)
set(ASCIICHAT_SYSTEM_CONFIG_PATH "${CMAKE_INSTALL_PREFIX}/etc/ascii-chat/config.toml")

# Status message with color
message(STATUS "System config path: ${BoldBlue}${ASCIICHAT_SYSTEM_CONFIG_PATH}${ColorReset}")

# Install example system config file
# Admins can copy config.toml.example to config.toml to enable system-wide defaults
if(EXISTS "${CMAKE_SOURCE_DIR}/share/examples/config.toml")
    install(FILES "${CMAKE_SOURCE_DIR}/share/examples/config.toml"
        DESTINATION "etc/ascii-chat"
        RENAME "config.toml.example"
        COMPONENT Runtime
    )
    message(STATUS "${BoldGreen}Configured${ColorReset} system config example: ${BoldBlue}config.toml.example${ColorReset} → ${BoldYellow}${CMAKE_INSTALL_PREFIX}/etc/ascii-chat/${ColorReset}")
endif()
