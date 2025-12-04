# =============================================================================
# Installation Configuration Module
# =============================================================================
# This module configures installation rules for ascii-chat
#
# Prerequisites:
#   - Target 'ascii-chat' must exist
#   - Must run after project() and target creation
#
# Outputs:
#   - Install rules for binary, documentation, and optional files
# =============================================================================

# =============================================================================
# Install Required System Libraries
# =============================================================================
# Automatically install runtime dependencies (MSVC runtime on Windows, etc.)
# Only relevant for Windows and macOS. Linux Release builds should be musl static.
include(InstallRequiredSystemLibraries)

# Warn if trying to use this on Linux Release builds (should be musl static instead)
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND PLATFORM_LINUX AND NOT USE_MUSL)
    message(WARNING "${BoldYellow}Release build on Linux without USE_MUSL${ColorReset}")
    message(WARNING "  Linux releases should use musl static builds for portability")
    message(WARNING "  Use: ${BoldCyan}cmake -B build -DUSE_MUSL=ON -DCMAKE_BUILD_TYPE=Release${ColorReset}")
endif()

# Install binary
install(TARGETS ascii-chat
    EXPORT ascii-chat-targets
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# =============================================================================
# Start Menu Shortcuts (Windows WiX installer)
# =============================================================================
# Create Start Menu shortcuts for documentation
# This uses the CPACK_START_MENU_SHORTCUTS install property which CPack WiX reads
# to generate Start Menu entries during MSI installation
#
# Note: ascii-chat is a terminal-only CLI program, so we don't create a shortcut
# to the executable (it would just flash a console window and exit). Users should
# run it from their terminal of choice (PowerShell, cmd, Windows Terminal, etc.)
# Create Start Menu shortcut for the documentation (opens in browser)
# Only if Doxygen is available to generate the docs
if(WIN32 AND ASCIICHAT_DOXYGEN_EXECUTABLE)
    set_property(INSTALL "doc/html/index.html"
        PROPERTY CPACK_START_MENU_SHORTCUTS "ascii-chat Documentation"
    )
    message(STATUS "Configured Start Menu shortcut: ${BoldBlue}ascii-chat Documentation${ColorReset}")
endif()

# Build exportable targets list
# For musl builds, exclude the shared library (it's not built by default and can't link against musl)
set(_ascii_chat_exportable_targets ascii-chat-static ascii-chat-static-lib)
if(NOT USE_MUSL)
    list(PREPEND _ascii_chat_exportable_targets ascii-chat-shared)
endif()

foreach(_ascii_target IN LISTS _ascii_chat_exportable_targets)
    if(TARGET ${_ascii_target})
        # Skip targets with EXCLUDE_FROM_ALL - these aren't built by default
        # and shouldn't be installed unless explicitly built
        get_target_property(_exclude_from_all ${_ascii_target} EXCLUDE_FROM_ALL)
        if(_exclude_from_all)
            continue()
        endif()
        install(TARGETS ${_ascii_target}
            EXPORT ascii-chat-targets
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib
            INCLUDES DESTINATION include/ascii-chat
            COMPONENT Development
        )
    endif()
endforeach()

# Install uninstall script (Unix only - not needed on Windows with uninstaller)
if(UNIX)
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/install/uninstall.sh.in"
        "${CMAKE_BINARY_DIR}/ascii-chat-uninstall.sh"
        @ONLY
    )
    install(PROGRAMS "${CMAKE_BINARY_DIR}/ascii-chat-uninstall.sh"
        DESTINATION bin
        COMPONENT Runtime
    )
endif()

# =============================================================================
# Install shared library and static libraries
# =============================================================================
# Library installation strategy:
#   - Static libraries (.a/.lib): Always install to lib/ (Development component)
#   - Shared libraries (DLL/.so/.dylib): Always install to lib/ (Development component)
#   - Dynamic builds (Debug/Dev on Windows): ALSO install DLLs to bin/ (Runtime component)
#
# Windows library structure:
#   - Release (static): libasciichat.a in lib/ only
#   - Debug/Dev (dynamic): asciichat.dll in lib/ (Development) AND bin/ (Runtime)
#
# Note: We create a dummy export target so CMake doesn't complain about missing EXPORT
# =============================================================================

if(WIN32)
    # =============================================================================
    # Windows Library Installation
    # =============================================================================

    # Install static library (.a) to lib/ - always available for linking
    install(FILES
        "${CMAKE_BINARY_DIR}/lib/libasciichat.a"
        DESTINATION lib
        COMPONENT Development
        OPTIONAL
    )

    # Install DLL to lib/ for development (all build types)
    # Developers linking against libasciichat.dll need it in lib/
    install(FILES
        "${CMAKE_BINARY_DIR}/bin/asciichat.dll"
        DESTINATION lib
        COMPONENT Development
        OPTIONAL
    )

    if(CMAKE_BUILD_TYPE MATCHES "Release")
        message(STATUS "Configured library installation: ${BoldBlue}libasciichat.a${ColorReset} and ${BoldBlue}asciichat.dll${ColorReset} → lib/ ${Magenta}(static build)${ColorReset}")
    else()
        # For dynamic builds (Debug/Dev), ALSO install asciichat.dll to bin/ for runtime
        # The executable needs to find asciichat.dll at runtime
        install(FILES
            "${CMAKE_BINARY_DIR}/bin/asciichat.dll"
            DESTINATION bin
            COMPONENT Runtime
            OPTIONAL
        )
        message(STATUS "Configured library installation: ${BoldBlue}asciichat.dll${ColorReset} → lib/ (dev) and bin/ (runtime), ${BoldBlue}libasciichat.a${ColorReset} → lib/ ${Magenta}(dynamic build)${ColorReset}")
    endif()
else()
    # =============================================================================
    # Unix/macOS Library Installation
    # =============================================================================

    # Always install static library if it was generated
    install(FILES
        "${CMAKE_BINARY_DIR}/lib/libasciichat.a"
        DESTINATION lib
        COMPONENT Development
        OPTIONAL
    )

    # Install platform-specific shared library (if present)
    if(APPLE)
        set(_ascii_chat_shared_lib "${CMAKE_BINARY_DIR}/lib/libasciichat.dylib")
        set(_ascii_chat_shared_label "libasciichat.dylib")
    else()
        set(_ascii_chat_shared_lib "${CMAKE_BINARY_DIR}/lib/libasciichat.so")
        set(_ascii_chat_shared_label "libasciichat.so")
    endif()

    install(FILES
        "${_ascii_chat_shared_lib}"
        DESTINATION lib
        COMPONENT Development
        OPTIONAL
    )

    message(STATUS "${BoldGreen}Configured${ColorReset} library installation: ${BoldBlue}libasciichat.a${ColorReset} (optional) and ${BoldBlue}${_ascii_chat_shared_label}${ColorReset} (optional) → ${BoldYellow}lib/${ColorReset}")
endif()

# Install public API headers
# These headers are useful for developers who want to build tools/extensions for ascii-chat
if(WIN32)
    # Windows: Install all headers except POSIX-specific ones
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/"
        DESTINATION include/ascii-chat
        COMPONENT Development
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*/internal/*" EXCLUDE
        PATTERN "*/private/*" EXCLUDE
        PATTERN "*/posix/*" EXCLUDE
    )
else()
    # Unix/macOS: Install all headers except Windows-specific ones
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/"
        DESTINATION include/ascii-chat
        COMPONENT Development
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*/internal/*" EXCLUDE
        PATTERN "*/private/*" EXCLUDE
        PATTERN "*/windows/*" EXCLUDE
    )
endif()

# Install pkg-config file (only when shared library is built)
if(PLATFORM_LINUX AND CMAKE_BUILD_TYPE STREQUAL "Release" AND TARGET ascii-chat-shared)
    # Set mimalloc requirement conditionally
    if(USE_MIMALLOC)
        set(MIMALLOC_REQUIREMENT "mimalloc, ")
    else()
        set(MIMALLOC_REQUIREMENT "")
    endif()

    # Configure the pkgconfig file with current settings
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/install/ascii-chat.pc.in"
        "${CMAKE_BINARY_DIR}/ascii-chat.pc"
        @ONLY
    )

    install(FILES "${CMAKE_BINARY_DIR}/ascii-chat.pc"
        DESTINATION lib/pkgconfig
        COMPONENT Development
    )
    message(STATUS "${BoldGreen}Configured${ColorReset} ${BoldBlue}pkg-config${ColorReset} file installation")
endif()

# Install CMake package config files (for find_package support)
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND TARGET ascii-chat-shared)
    include(CMakePackageConfigHelpers)

    # Generate the config file that includes the exports
    configure_package_config_file(
        "${CMAKE_SOURCE_DIR}/cmake/install/ascii-chat-config.cmake.in"
        "${CMAKE_BINARY_DIR}/ascii-chat-config.cmake"
        INSTALL_DESTINATION lib/cmake/ascii-chat
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    # Generate the version file for the config file
    write_basic_package_version_file(
        "${CMAKE_BINARY_DIR}/ascii-chat-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Install the config files
    install(FILES
        "${CMAKE_BINARY_DIR}/ascii-chat-config.cmake"
        "${CMAKE_BINARY_DIR}/ascii-chat-config-version.cmake"
        DESTINATION lib/cmake/ascii-chat
        COMPONENT Development
    )

    # Export targets to a script
    install(EXPORT ascii-chat-targets
        FILE ascii-chat-targets.cmake
        NAMESPACE ascii-chat::
        DESTINATION lib/cmake/ascii-chat
        COMPONENT Development
    )

    export(EXPORT ascii-chat-targets
        FILE ${CMAKE_BINARY_DIR}/ascii-chat-targets.cmake
        NAMESPACE ascii-chat::)

    message(STATUS "${BoldGreen}Configured${ColorReset} ${BoldBlue}CMake${ColorReset} package config installation")
endif()

# Remove mimalloc files from installation - it's statically linked so not needed
# This CODE block runs during "cmake --install" after mimalloc is installed
install(CODE "
    message(STATUS \"Removing mimalloc files from installation...\")
    message(STATUS \"  Install prefix: \${CMAKE_INSTALL_PREFIX}\")

    # Check what exists before removal
    if(EXISTS \"\${CMAKE_INSTALL_PREFIX}/include/mimalloc-2.1\")
        message(STATUS \"  Found mimalloc headers at: \${CMAKE_INSTALL_PREFIX}/include/mimalloc-2.1\")
        file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/include/mimalloc-2.1\")
    else()
        message(STATUS \"  No mimalloc headers found\")
    endif()

    if(EXISTS \"\${CMAKE_INSTALL_PREFIX}/lib/mimalloc-2.1\")
        message(STATUS \"  Found mimalloc libs at: \${CMAKE_INSTALL_PREFIX}/lib/mimalloc-2.1\")
        file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/lib/mimalloc-2.1\")
    else()
        message(STATUS \"  No mimalloc libs found\")
    endif()

    if(EXISTS \"\${CMAKE_INSTALL_PREFIX}/lib/cmake/mimalloc-2.1\")
        message(STATUS \"  Found mimalloc cmake at: \${CMAKE_INSTALL_PREFIX}/lib/cmake/mimalloc-2.1\")
        file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/lib/cmake/mimalloc-2.1\")
    else()
        message(STATUS \"  No mimalloc cmake found\")
    endif()

    if(EXISTS \"\${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/mimalloc.pc\")
        message(STATUS \"  Found mimalloc pkgconfig at: \${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/mimalloc.pc\")
        file(REMOVE \"\${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/mimalloc.pc\")
    else()
        message(STATUS \"  No mimalloc pkgconfig found\")
    endif()

    message(STATUS \"Finished removing mimalloc files from installation\")
")

# =============================================================================
# Install dependency DLLs on Windows (for dynamic builds only)
# =============================================================================
# Dynamic builds (Debug/Dev) require dependency DLLs in bin/ directory:
#   - zstd.dll, libsodium.dll, portaudio.dll, etc.
#   - asciichat.dll (already handled above, but also matched here)
#
# These DLLs are copied to build/bin by vcpkg's applocal.ps1 during linking.
# We install them to bin/ so the executable can find them at runtime.
#
# Release builds use static libraries, so no dependency DLLs are needed.
# =============================================================================
if(WIN32 AND NOT CMAKE_BUILD_TYPE MATCHES "Release")
    # Install all DLLs from build/bin directory to bin/ (Runtime component)
    # This includes both dependency DLLs (zstd, libsodium, etc.) AND asciichat.dll
    # Note: asciichat.dll is already installed to lib/ above (Development component)
    # but we also need it in bin/ for the executable to run
    install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/"
        DESTINATION bin
        COMPONENT Runtime
        FILES_MATCHING
        PATTERN "*.dll"
        PATTERN "*.exe" EXCLUDE  # Exclude exe (already installed by install(TARGETS))
    )
    message(STATUS "Configured dependency DLL installation: all DLLs → bin/ (runtime, dynamic build)")
endif()

# Platform-specific installation paths (must be set before using INSTALL_DOC_DIR)
# Windows uses doc/ instead of share/doc/ to avoid creating share/ directory
# Unix/macOS follow FHS and use share/doc/ascii-chat
if(UNIX AND NOT APPLE)
    # Linux: Follow FHS (Filesystem Hierarchy Standard)
    # Binary goes to /usr/local/bin (or user-specified prefix)
    # Documentation goes to /usr/local/share/doc/ascii-chat
    set(INSTALL_DOC_DIR "share/doc/ascii-chat" CACHE PATH "Documentation install directory")
elseif(APPLE)
    # macOS: Follow macOS conventions
    # Binary goes to /usr/local/bin
    # Documentation goes to /usr/local/share/doc/ascii-chat
    set(INSTALL_DOC_DIR "share/doc/ascii-chat" CACHE PATH "Documentation install directory")
elseif(WIN32)
    # Windows: Follow Windows conventions
    # Binary goes to Program Files/ascii-chat/bin
    # Documentation goes to Program Files/ascii-chat/doc (not share/doc/ascii-chat)
    set(INSTALL_DOC_DIR "doc" CACHE PATH "Documentation install directory")
endif()

# =============================================================================
# Main Manual Page Installation (Unix only)
# =============================================================================
# Install the main ascii-chat(1) manpage - the primary documentation users get
# when they run "man ascii-chat". This is separate from the Doxygen-generated
# API manpages in man3/ which document individual functions.
#
# The manpage source is in docs/ascii-chat.1 and uses @PROJECT_VERSION@ for
# version substitution via configure_file().
if(UNIX)
    # Configure manpage with version substitution
    configure_file(
        "${CMAKE_SOURCE_DIR}/docs/ascii-chat.1.in"
        "${CMAKE_BINARY_DIR}/docs/ascii-chat.1"
        @ONLY
    )

    # Install to share/man/man1/ (section 1 = user commands)
    install(FILES "${CMAKE_BINARY_DIR}/docs/ascii-chat.1"
        DESTINATION share/man/man1
        COMPONENT Manpages
    )
    message(STATUS "${BoldGreen}Configured${ColorReset} manpage installation: ${BoldBlue}ascii-chat.1${ColorReset} → ${BoldYellow}share/man/man1/${ColorReset}")
endif()

# =============================================================================
# Shell Completion Scripts Installation (Unix only)
# =============================================================================
# Install tab-completion scripts for bash, zsh, and fish shells
if(UNIX)
    # Bash completions
    # Standard location: /usr/share/bash-completion/completions/
    # User location: ~/.local/share/bash-completion/completions/
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.bash")
        install(FILES "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.bash"
            DESTINATION share/bash-completion/completions
            RENAME ascii-chat
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} bash completion: ${BoldBlue}ascii-chat.bash${ColorReset} → ${BoldYellow}share/bash-completion/completions/ascii-chat${ColorReset}")
    endif()

    # Zsh completions
    # Standard location: /usr/share/zsh/site-functions/
    # User location: ~/.local/share/zsh/site-functions/ or a directory in $fpath
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/completions/_ascii-chat")
        install(FILES "${CMAKE_SOURCE_DIR}/share/completions/_ascii-chat"
            DESTINATION share/zsh/site-functions
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} zsh completion: ${BoldBlue}_ascii-chat${ColorReset} → ${BoldYellow}share/zsh/site-functions/${ColorReset}")
    endif()

    # Fish completions
    # Standard location: /usr/share/fish/vendor_completions.d/
    # User location: ~/.config/fish/completions/
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.fish")
        install(FILES "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.fish"
            DESTINATION share/fish/vendor_completions.d
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} fish completion: ${BoldBlue}ascii-chat.fish${ColorReset} → ${BoldYellow}share/fish/vendor_completions.d/${ColorReset}")
    endif()

    # PowerShell completions (cross-platform)
    # Standard location: /usr/local/share/powershell/Completions/
    # User location: ~/.local/share/powershell/Completions/
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.ps1")
        install(FILES "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.ps1"
            DESTINATION share/powershell/Completions
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} powershell completion: ${BoldBlue}ascii-chat.ps1${ColorReset} → ${BoldYellow}share/powershell/Completions/${ColorReset}")
    endif()
endif()

# =============================================================================
# Shell Completion Scripts Installation (Windows)
# =============================================================================
# Install PowerShell completion script on Windows
if(WIN32)
    # PowerShell completions
    # Standard location varies, but doc/completions is accessible
    # User can source from: $HOME\Documents\PowerShell\Completions\
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.ps1")
        install(FILES "${CMAKE_SOURCE_DIR}/share/completions/ascii-chat.ps1"
            DESTINATION doc/completions
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} powershell completion: ${BoldBlue}ascii-chat.ps1${ColorReset} → ${BoldYellow}doc/completions/${ColorReset}")
    endif()
endif()

# =============================================================================
# Desktop Entry Installation (Linux only)
# =============================================================================
# Install .desktop file for application launchers and menus
# Note: ascii-chat is a terminal application, so Terminal=true is set
if(UNIX AND NOT APPLE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/applications/ascii-chat.desktop")
        install(FILES "${CMAKE_SOURCE_DIR}/share/applications/ascii-chat.desktop"
            DESTINATION share/applications
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} desktop entry: ${BoldBlue}ascii-chat.desktop${ColorReset} → ${BoldYellow}share/applications/${ColorReset}")
    endif()
endif()

# =============================================================================
# Application Icon Installation (Linux only)
# =============================================================================
# Install hicolor theme icons for desktop environments and software centers
# Icons are installed to share/icons/hicolor/{size}/apps/ascii-chat.png
if(UNIX AND NOT APPLE)
    # Standard icon sizes for hicolor theme
    set(_ICON_SIZES 16 24 32 48 64 128 256 512)
    foreach(_SIZE ${_ICON_SIZES})
        set(_ICON_PATH "${CMAKE_SOURCE_DIR}/share/icons/hicolor/${_SIZE}x${_SIZE}/apps/ascii-chat.png")
        if(EXISTS "${_ICON_PATH}")
            install(FILES "${_ICON_PATH}"
                DESTINATION share/icons/hicolor/${_SIZE}x${_SIZE}/apps
                COMPONENT Runtime
            )
        endif()
    endforeach()
    message(STATUS "${BoldGreen}Configured${ColorReset} application icons: ${BoldBlue}ascii-chat.png${ColorReset} → ${BoldYellow}share/icons/hicolor/*/apps/${ColorReset}")
endif()

# =============================================================================
# AppStream Metainfo Installation (Linux only)
# =============================================================================
# Install AppStream metadata for software centers (GNOME Software, KDE Discover, etc.)
if(UNIX AND NOT APPLE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/metainfo/gg.zfo.ascii-chat.metainfo.xml")
        install(FILES "${CMAKE_SOURCE_DIR}/share/metainfo/gg.zfo.ascii-chat.metainfo.xml"
            DESTINATION share/metainfo
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} AppStream metainfo: ${BoldBlue}gg.zfo.ascii-chat.metainfo.xml${ColorReset} → ${BoldYellow}share/metainfo/${ColorReset}")
    endif()
endif()

# =============================================================================
# Systemd Service File Installation (Linux only)
# =============================================================================
# Install systemd service file for running ascii-chat server as a daemon
# Users must enable with: systemctl --user enable ascii-chat-server
# Or system-wide: sudo systemctl enable ascii-chat-server
if(UNIX AND NOT APPLE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/share/systemd/ascii-chat-server.service")
        # Install to user systemd directory (lib/systemd/user/)
        # System-wide would be lib/systemd/system/
        install(FILES "${CMAKE_SOURCE_DIR}/share/systemd/ascii-chat-server.service"
            DESTINATION lib/systemd/user
            COMPONENT Runtime
        )
        message(STATUS "${BoldGreen}Configured${ColorReset} systemd service: ${BoldBlue}ascii-chat-server.service${ColorReset} → ${BoldYellow}lib/systemd/user/${ColorReset}")
    endif()
endif()

# =============================================================================
# Example Configuration File Installation
# =============================================================================
# Install example config.toml with documented options
# Users can copy this to their config directory to customize
if(EXISTS "${CMAKE_SOURCE_DIR}/share/examples/config.toml")
    install(FILES "${CMAKE_SOURCE_DIR}/share/examples/config.toml"
        DESTINATION ${INSTALL_DOC_DIR}/examples
        COMPONENT Documentation
    )
    message(STATUS "${BoldGreen}Configured${ColorReset} example config: ${BoldBlue}config.toml${ColorReset} → ${BoldYellow}${INSTALL_DOC_DIR}/examples/${ColorReset}")
endif()

# Install documentation at root of installation directory
# README.md and LICENSE.txt should be at the root for easy access
if(EXISTS "${CMAKE_SOURCE_DIR}/README.md")
    install(FILES README.md
        DESTINATION .
        COMPONENT Runtime
        OPTIONAL
    )
endif()

# Install license (if exists)
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE.txt")
    install(FILES LICENSE.txt
        DESTINATION .
        COMPONENT Runtime
        OPTIONAL
    )
endif()

# Install Doxygen HTML documentation (if generated)
# Doxygen HTML docs are generated in ${CMAKE_BINARY_DIR}/docs/html/
# Similar to DLLs, we need to explicitly install from build directory
# Platform-specific: Windows uses doc/html, Unix/macOS uses share/doc/ascii-chat/html
install(DIRECTORY "${CMAKE_BINARY_DIR}/docs/html/"
    DESTINATION ${INSTALL_DOC_DIR}/html
    COMPONENT Documentation
    FILES_MATCHING
    PATTERN "*.html"
    PATTERN "*.css"
    PATTERN "*.js"
    PATTERN "*.png"
    PATTERN "*.jpg"
    PATTERN "*.gif"
    PATTERN "*.svg"
)

# Install manpages generated by Doxygen (if they exist)
# Manpages are generated in ${CMAKE_BINARY_DIR}/docs/man/man3/ with ascii-chat- prefix
# The install will only work if the docs target has been built first
# Note: OPTIONAL cannot be used with FILES_MATCHING, but FILES_MATCHING handles no matches gracefully
# FHS-compliant: Unix/macOS install to share/man/man3
if(UNIX AND NOT APPLE)
    # Unix/Linux: FHS-compliant installation to share/man/man3
    install(DIRECTORY "${CMAKE_BINARY_DIR}/docs/man/man3/"
        DESTINATION share/man/man3
        COMPONENT Manpages
        FILES_MATCHING
        PATTERN "ascii-chat-*.3"  # Only install prefixed manpages (section 3)
        PATTERN "ascii-chat-_*.3" EXCLUDE  # Exclude path-based manpages (e.g. _home_user_...)
        PATTERN "*.3.gz" EXCLUDE  # Exclude compressed manpages if any
        PATTERN "*.3.bz2" EXCLUDE
        PATTERN "*.3.xz" EXCLUDE
    )
elseif(APPLE)
    # macOS: FHS-compliant installation to share/man/man3
    install(DIRECTORY "${CMAKE_BINARY_DIR}/docs/man/man3/"
        DESTINATION share/man/man3
        COMPONENT Manpages
        FILES_MATCHING
        PATTERN "ascii-chat-*.3"  # Only install prefixed manpages (section 3)
        PATTERN "ascii-chat-_*.3" EXCLUDE  # Exclude path-based manpages (e.g. _home_user_...)
        PATTERN "*.3.gz" EXCLUDE  # Exclude compressed manpages if any
        PATTERN "*.3.bz2" EXCLUDE
        PATTERN "*.3.xz" EXCLUDE
    )
endif()

# Platform-specific installation paths are set earlier in the file (before using INSTALL_DOC_DIR)

# Option to install example config files (if any exist in the future)
# if(EXISTS "${CMAKE_SOURCE_DIR}/examples")
#     install(DIRECTORY examples/
#         DESTINATION share/doc/ascii-chat/examples
#         COMPONENT Documentation
#         FILES_MATCHING
#         PATTERN "*.conf"
#         PATTERN "*.toml"
#     )
# endif()

# =============================================================================
# CPack Configuration (Optional Package Generation)
# =============================================================================
# Configure CPack for generating platform-specific packages
# Generates: DEB, RPM (Linux), DMG (macOS), ZIP, NSIS/EXE (Windows)
#
# Usage:
#   cmake --build build --target package
#
# Disable with: -DUSE_CPACK=OFF
# =============================================================================

option(USE_CPACK "Enable CPack package generation" ON)

if(USE_CPACK)
    # Check if CPack is available
    if(CMAKE_VERSION VERSION_LESS "3.16")
        message(WARNING "CPack requires CMake 3.16+ (current: ${CMAKE_VERSION}). Disabling CPack.")
        set(USE_CPACK OFF)
    else()
        set(CPACK_THREADS ${CMAKE_BUILD_PARALLEL_LEVEL})

        # Set CPack variables BEFORE including CPack to prevent defaults
        # This ensures CPack uses the correct version and install directory
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "ascii-chat" CACHE STRING "Installation directory (without version)" FORCE)
        # Set version from PROJECT_VERSION (should be overridden by Version.cmake if git is available)
        set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR} CACHE STRING "Package version major" FORCE)
        set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR} CACHE STRING "Package version minor" FORCE)
        set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH} CACHE STRING "Package version patch" FORCE)
        set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Package version" FORCE)
        # Set license file BEFORE include(CPack) to prevent CPack from using default
        if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE.txt")
            set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt" CACHE FILEPATH "License file for installers" FORCE)
        endif()

        # macOS: Create custom Welcome and ReadMe files for productbuild installer
        if(APPLE)
            file(WRITE "${CMAKE_BINARY_DIR}/ProductbuildWelcome.txt"
"Welcome to ascii-chat Installer

This installer will guide you through installing ascii-chat - the first command line video chat program.

ascii-chat converts webcam video into ASCII art in real-time, enabling video chat right in your terminal.")

            file(WRITE "${CMAKE_BINARY_DIR}/ProductbuildReadMe.txt"
"ascii-chat - 💻📸 video chat in your terminal 🔡💬
https://github.com/zfogg/ascii-chat

Probably the first command line video chat program. ascii-chat converts webcam video into ASCII art in real-time, enabling video chat right in your terminal - whether you're using rxvt-unicode in OpenBox, iTerm on macOS, or even a remote SSH session via PuTTY.

FEATURES
========
• Multi-party video chat with automatic grid layout (like Zoom/Google Hangouts)
• Real-time video to ASCII conversion with color support
• Audio streaming with custom mixer (compression, ducking, noise gating, filtering)
• End-to-end encryption with SSH key authentication
• Cross-platform: Linux, macOS, Windows
• Works in any terminal that supports ANSI escape sequences

WHAT'S INCLUDED
===============
This package includes:
• ascii-chat: The main executable for running video chat sessions
• libasciichat: Shared library (.dylib) and static library (.a) for developers
• C header files for integrating ascii-chat into your own projects
• CMake and pkg-config files for easy build system integration
• Developer documentation (HTML and man pages)

USAGE
=====
After installation, run:
  ascii-chat server     # Start a video chat server
  ascii-chat client     # Connect to a server
  ascii-chat --help     # Show all options")

            set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_BINARY_DIR}/ProductbuildWelcome.txt" CACHE FILEPATH "Welcome file for productbuild" FORCE)
            set(CPACK_RESOURCE_FILE_README "${CMAKE_BINARY_DIR}/ProductbuildReadMe.txt" CACHE FILEPATH "ReadMe file for productbuild" FORCE)
        endif()

        # Use custom STGZ header with FHS-compliant defaults (/usr/local, no subdirectory)
        # CMake's default header uses current directory, not /usr/local
        # CPack will substitute @CPACK_*@ variables and calculate header length automatically
        if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/install/CPackSTGZHeader.sh.in")
            set(CPACK_STGZ_HEADER_FILE "${CMAKE_SOURCE_DIR}/cmake/install/CPackSTGZHeader.sh.in" CACHE FILEPATH "Custom STGZ header with /usr/local default" FORCE)
        endif()

        # On Windows, explicitly prevent CPack from creating share/ directory
        # CPack should only install what we explicitly tell it to via install() commands
        # All our install() commands use doc/ (not share/doc/) on Windows, so CPack shouldn't create share/
        # Explicitly set CPack to not create empty directories or use share/ structure
        if(WIN32)
            # Detect WiX BEFORE including CPack (required for CPACK_WIX_VERSION to work)
            include("${CMAKE_SOURCE_DIR}/cmake/install/Wix.cmake")

            # Set default generator to WIX (must be before include(CPack))
            # Save the desired generator before include(CPack) overwrites it
            if(WIX_FOUND)
                set(_DESIRED_CPACK_GENERATOR "WIX")
            else()
                set(_DESIRED_CPACK_GENERATOR "ZIP")
            endif()
            set(CPACK_GENERATOR "${_DESIRED_CPACK_GENERATOR}")

            # Ensure CPack only installs explicitly defined components
            # This prevents CPack from creating empty directories based on CMAKE_INSTALL_PREFIX structure
            # CPack will only install files/directories we explicitly install via install() commands
            # Since all install() commands use doc/ (not share/doc/), no share/ directory should be created
            # Note: CPACK_COMPONENTS_ALL is set later in the component configuration section,
            # but we set it here early to ensure CPack respects it
            # Manpages component is Unix-only, so exclude it on Windows
            set(CPACK_COMPONENTS_ALL Runtime Development Documentation CACHE STRING "CPack components to install" FORCE)
            # Explicitly set packaging install prefix to empty to prevent CPack from creating share/ structure
            # CPack will use CMAKE_INSTALL_PREFIX directly without adding share/ subdirectories
            set(CPACK_PACKAGING_INSTALL_PREFIX "" CACHE STRING "Installation prefix for CPack (empty = use CMAKE_INSTALL_PREFIX directly)" FORCE)
            # Ensure CPack installs only what we explicitly define
            set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};ALL;/" CACHE STRING "CPack install projects" FORCE)
            # Run cleanup script to remove mimalloc files after install but before packaging
            if (NOT WIN32)
                set(CPACK_INSTALL_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/install/CPackRemoveMimalloc.cmake" CACHE FILEPATH "Post-install cleanup script" FORCE)
            endif()
        else()
            # On macOS/Unix, explicitly set which components to package
            # This prevents CPack from creating an "Unspecified" component for unassigned files
            # Manpages component is Unix/macOS only
            set(CPACK_COMPONENTS_ALL Runtime Development Documentation Manpages CACHE STRING "CPack components to install" FORCE)

            # On macOS, don't use CPACK_PACKAGE_INSTALL_DIRECTORY (it causes /Applications/ prefix)
            # This variable is for Windows installers (NSIS/WIX) and productbuild doesn't use it correctly
            if(APPLE)
                set(CPACK_PACKAGE_INSTALL_DIRECTORY "" CACHE STRING "Installation directory (empty for macOS)" FORCE)
                # Note: CPACK_PACKAGING_INSTALL_PREFIX is set per-generator in CPackProjectConfig.cmake
                # This allows different prefixes for productbuild (/usr/local) vs STGZ/TGZ (/)

                # Set CMP0161 policy to suppress warning about CPACK_PRODUCTBUILD_DOMAINS
                # NEW behavior: CPACK_PRODUCTBUILD_DOMAINS defaults to true
                if(POLICY CMP0161)
                    cmake_policy(SET CMP0161 NEW)
                endif()
            endif()
        endif()

        # Set DEB/RPM dependencies and metadata BEFORE include(CPack)
        # Runtime dependencies for the shared library (libasciichat.so)
        if(UNIX AND NOT APPLE)
            set(CPACK_DEBIAN_PACKAGE_DEPENDS "libportaudio2, libzstd1, libsodium23")
            set(CPACK_RPM_PACKAGE_REQUIRES "portaudio, libzstd, libsodium")
            if(USE_MIMALLOC)
                set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libmimalloc2.0 | libmimalloc-dev")
                set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, mimalloc")
            endif()

            # RPM-specific metadata (must be set BEFORE include(CPack))
            # Use CACHE FORCE to ensure these values persist through CPack's initialization
            set(CPACK_RPM_PACKAGE_LICENSE "MIT" CACHE STRING "RPM package license" FORCE)
            set(CPACK_RPM_PACKAGE_GROUP "Applications/Networking" CACHE STRING "RPM package group" FORCE)
            set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}" CACHE STRING "RPM package description" FORCE)

            # DEB-specific metadata (must be set BEFORE include(CPack))
            # Debian packages need proper section, priority, and homepage
            set(CPACK_DEBIAN_PACKAGE_SECTION "net" CACHE STRING "DEB package section" FORCE)
            set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional" CACHE STRING "DEB package priority" FORCE)
            set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}" CACHE STRING "DEB package homepage" FORCE)
        endif()

        # Set package file name BEFORE include(CPack)
        # Determine OS name (use "macOS" instead of "Darwin")
        if(APPLE)
            set(_PACKAGE_OS "macOS")
        elseif(WIN32)
            set(_PACKAGE_OS "Windows")
        else()
            set(_PACKAGE_OS "${CMAKE_SYSTEM_NAME}")
        endif()

        # Normalize architecture name to lowercase amd64/arm64
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
            set(_PACKAGE_ARCH "arm64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
            set(_PACKAGE_ARCH "amd64")
        else()
            set(_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
        endif()

        set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${_PACKAGE_OS}-${_PACKAGE_ARCH}")

        # Set CPack project config file for per-generator settings (e.g., install prefix)
        # This file is processed at cpack runtime, allowing different prefixes per generator:
        #   - STGZ/TGZ: "/" for relative paths (Homebrew compatibility)
        #   - productbuild: "/usr/local" for standard macOS .pkg location
        #   - DEB/RPM: "/usr" for Linux FHS compliance
        set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/install/CPackProjectConfig.cmake")

        # =========================================================================
        # Platform-Specific Package Generators (MUST be before include(CPack))
        # =========================================================================
        # Each generator is configured in its own module file in cmake/install/
        # This keeps Install.cmake clean and makes it easy to maintain each format
        #
        # IMPORTANT: These includes MUST happen BEFORE include(CPack) because:
        # 1. include(CPack) generates CPackConfig.cmake with the current CPACK_GENERATOR value
        # 2. Any generators added AFTER include(CPack) won't appear in CPackConfig.cmake
        # 3. CPack reads CPACK_GENERATOR from CPackConfig.cmake at package time

        # Initialize generator list (will be appended by each module)
        # On Windows, preserve the WIX generator that was set earlier in this file
        if(NOT WIN32)
            set(CPACK_GENERATOR "")
        endif()

        if(UNIX AND NOT APPLE)
            # Linux: STGZ, TGZ, DEB, RPM
            include("${CMAKE_SOURCE_DIR}/cmake/install/Stgz.cmake")
            include("${CMAKE_SOURCE_DIR}/cmake/install/Archive.cmake")
            include("${CMAKE_SOURCE_DIR}/cmake/install/Deb.cmake")
            include("${CMAKE_SOURCE_DIR}/cmake/install/Rpm.cmake")

        elseif(APPLE)
            # macOS: STGZ, TGZ, productbuild
            include("${CMAKE_SOURCE_DIR}/cmake/install/Stgz.cmake")
            include("${CMAKE_SOURCE_DIR}/cmake/install/Archive.cmake")
            include("${CMAKE_SOURCE_DIR}/cmake/install/Productbuild.cmake")

        elseif(WIN32)
            # Windows: WIX, NSIS, ZIP
            # WiX was already included earlier for CPACK_WIX_VERSION to work
            # NSIS and Archive modules append to generator list
            include("${CMAKE_SOURCE_DIR}/cmake/install/Archive.cmake")
            include("${CMAKE_SOURCE_DIR}/cmake/install/Nsis.cmake")

            # Set default message for Windows
            if(WIX_FOUND)
                message(STATUS "${Yellow}CPack:${ColorReset} Default generator: ${Magenta}WIX${ColorReset} (MSI installer)")
            else()
                message(STATUS "${Yellow}CPack:${ColorReset} Default generator: ${Magenta}ZIP${ColorReset} (${BoldBlue}WiX${ColorReset} not found)")
            endif()

        else()
            # Unknown platform: fallback to archive formats
            include("${CMAKE_SOURCE_DIR}/cmake/install/Archive.cmake")
        endif()

        message(STATUS "${Yellow}CPack:${ColorReset} Generators: ${Magenta}${CPACK_GENERATOR}${ColorReset}")

        include(CPack)

        # After include(CPack), enable binary generators for desired package types
        # CPack sets all CPACK_BINARY_* to OFF by default, we need to enable the ones we want
        # These control which generators actually run when "make package" is called
        if(APPLE)
            set(CPACK_BINARY_STGZ ON CACHE BOOL "Enable STGZ generator" FORCE)
            set(CPACK_BINARY_TGZ ON CACHE BOOL "Enable TGZ generator" FORCE)
            set(CPACK_BINARY_PRODUCTBUILD ON CACHE BOOL "Enable productbuild generator" FORCE)
        elseif(UNIX)
            set(CPACK_BINARY_STGZ ON CACHE BOOL "Enable STGZ generator" FORCE)
            set(CPACK_BINARY_TGZ ON CACHE BOOL "Enable TGZ generator" FORCE)
            set(CPACK_BINARY_DEB ON CACHE BOOL "Enable DEB generator" FORCE)
            set(CPACK_BINARY_RPM ON CACHE BOOL "Enable RPM generator" FORCE)
        elseif(WIN32)
            # Windows generators are handled separately (WIX/NSIS/ZIP)
            set(CPACK_BINARY_ZIP ON CACHE BOOL "Enable ZIP generator" FORCE)
            if(WIX_FOUND)
                set(CPACK_BINARY_WIX ON CACHE BOOL "Enable WIX generator" FORCE)
            endif()
        endif()

        # After include(CPack), restore CPACK_GENERATOR with our desired default
        # CPack auto-detects generators and overwrites our setting
        if(WIN32 AND DEFINED _DESIRED_CPACK_GENERATOR)
            # Start with the desired default generator
            set(CPACK_GENERATOR "${_DESIRED_CPACK_GENERATOR}")
        endif()
    endif()

    # =========================================================================
    # Basic CPack Configuration
    # =========================================================================
    # Note: CPACK_PACKAGE_NAME, CPACK_PACKAGE_VENDOR, CPACK_PACKAGE_DESCRIPTION_SUMMARY,
    # CPACK_PACKAGE_DESCRIPTION, CPACK_PACKAGE_CONTACT, and CPACK_PACKAGE_HOMEPAGE_URL
    # are all set in cmake/ProjectConstants.cmake

    # Version is already set before include(CPack) above
    # Re-set with CACHE FORCE to ensure it's not overridden
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR} CACHE STRING "Package version major" FORCE)
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR} CACHE STRING "Package version minor" FORCE)
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH} CACHE STRING "Package version patch" FORCE)
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Package version" FORCE)

    # Log version being used for packages
    message(STATUS "${Yellow}CPack:${ColorReset} Using version ${BoldGreen}${CPACK_PACKAGE_VERSION}${ColorReset} for packages (from PROJECT_VERSION=${BoldGreen}${PROJECT_VERSION}${ColorReset})")

    # License file is already set before include(CPack) above
    # Re-set with CACHE FORCE to ensure CPack doesn't override it
    if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE.txt")
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt" CACHE FILEPATH "License file for installers" FORCE)
        message(STATUS "${Yellow}CPack:${ColorReset} Using ${BoldBlue}LICENSE.txt${ColorReset} for installers")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE" CACHE FILEPATH "License file for installers" FORCE)
        message(STATUS "${Yellow}CPack:${ColorReset} Using ${BoldBlue}LICENSE${ColorReset} for installers")
    endif()

    # Output directory
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

    # =========================================================================
    # Binary Stripping Configuration
    # =========================================================================
    # Strip binaries and libraries to reduce package size
    # This removes debugging symbols and other unnecessary data
    # CPACK_STRIP_FILES is a list of files (relative to install prefix) that CPack will strip
    # When this list is populated, CPack automatically strips the files during packaging

    # On Unix platforms, explicitly list files to strip
    # CPack uses the 'strip' command (or CMAKE_STRIP if set)
    if(UNIX AND NOT APPLE)
        # List of files to strip (relative to install prefix)
        set(CPACK_STRIP_FILES
            "bin/ascii-chat"                    # Main executable
            "lib/libasciichat.so"               # Shared library (Linux)
        )
        message(STATUS "${Yellow}CPack:${ColorReset} Binary stripping ${BoldGreen}enabled${ColorReset} for ${BoldBlue}${CPACK_STRIP_FILES}")
    elseif(APPLE)
        set(CPACK_STRIP_FILES
            "bin/ascii-chat"                    # Main executable
            "lib/libasciichat.dylib"            # Shared library (macOS)
        )
        message(STATUS "${Yellow}CPack:${ColorReset} Binary stripping ${BoldGreen}enabled${ColorReset} for ${BoldBlue}${CPACK_STRIP_FILES}")
    elseif(WIN32)
        # Windows: Use llvm-strip if available (for Clang builds)
        # Note: MSVC uses different tools, but this project uses Clang
        # Use centralized ASCIICHAT_LLVM_STRIP_EXECUTABLE from FindPrograms.cmake
        if(ASCIICHAT_LLVM_STRIP_EXECUTABLE)
            set(CMAKE_STRIP "${ASCIICHAT_LLVM_STRIP_EXECUTABLE}" CACHE FILEPATH "Strip tool for CPack" FORCE)
            set(CPACK_STRIP_FILES
                "bin/ascii-chat.exe"            # Main executable
                "bin/asciichat.dll"             # Shared library (if dynamic build)
                "lib/asciichat.dll"             # Shared library (development copy)
            )
            message(STATUS "${BoldYellow}CPack:${ColorReset} Binary stripping ${BoldGreen}enabled${ColorReset} using ${BoldBlue}llvm-strip${ColorReset} for ${BoldBlue}${CPACK_STRIP_FILES}")
        else()
            message(STATUS "${BoldRed}CPack:${ColorReset} Binary stripping ${Yellow}disabled${ColorReset} ${BoldRed}(ASCIICHAT_LLVM_STRIP_EXECUTABLE not found)${ColorReset}")
        endif()
    endif()

    # Installation directory (without version for NSIS)
    # Set again after include(CPack) to ensure it overrides CPack's default
    # CPack's default is "${CPACK_PACKAGE_NAME} ${CPACK_PACKAGE_VERSION}" which we don't want
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}" CACHE STRING "Installation directory (without version)" FORCE)

    # =========================================================================
    # Component Groups Configuration (Global - applies to all generators)
    # =========================================================================
    # Note: Using "RuntimeGroup" and "DevelopmentGroup" as internal group IDs
    # to avoid NSIS naming conflicts with component names "Runtime" and "Development"
    # Component Groups (legacy API - also set via cpack_add_component_group below)
    set(CPACK_COMPONENT_RUNTIME_GROUP "RUNTIMEGROUP")
    set(CPACK_COMPONENT_DEVELOPMENT_GROUP "DEVELOPMENTGROUP")
    set(CPACK_COMPONENT_DOCUMENTATION_GROUP "RUNTIMEGROUP")
    if(NOT WIN32)
        set(CPACK_COMPONENT_MANPAGES_GROUP "MANPAGESGROUP")
    endif()

    # Runtime Group
    set(CPACK_COMPONENT_GROUP_RUNTIMEGROUP_DISPLAY_NAME "Runtime")
    set(CPACK_COMPONENT_GROUP_RUNTIMEGROUP_DESCRIPTION "The ascii-chat executable")
    set(CPACK_COMPONENT_GROUP_RUNTIMEGROUP_EXPANDED ON)

    # Development Group
    set(CPACK_COMPONENT_GROUP_DEVELOPMENTGROUP_DISPLAY_NAME "Development")
    set(CPACK_COMPONENT_GROUP_DEVELOPMENTGROUP_DESCRIPTION "Code with libasciichat")
    set(CPACK_COMPONENT_GROUP_DEVELOPMENTGROUP_EXPANDED OFF)

    # Component descriptions
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Application")
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "The main ascii-chat executable for video chat")
    set(CPACK_COMPONENT_RUNTIME_REQUIRED ON)

    set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "Libraries and Headers")
    set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "ascii-chat's shared library and C header Files for development")
    set(CPACK_COMPONENT_DEVELOPMENT_DISABLED OFF)

    set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Developer Documentation")
    set(CPACK_COMPONENT_DOCUMENTATION_DESCRIPTION "Developer HTML documentation generated with Doxygen")
    set(CPACK_COMPONENT_DOCUMENTATION_DISABLED OFF)

    # Manpages component - only on Unix platforms
    if(NOT WIN32)
        set(CPACK_COMPONENT_MANPAGES_DISPLAY_NAME "Manual Pages")
        set(CPACK_COMPONENT_MANPAGES_DESCRIPTION "Unix man pages generated with Doxygen")
        set(CPACK_COMPONENT_MANPAGES_DISABLED OFF)
    endif()

    # =========================================================================
    # Component Configuration
    # =========================================================================
    # Configure components for installation organized into Runtime and Development groups
    # Using CPack module functions for proper component configuration

    # Load CPack component helper functions
    include(CPackComponent)

    # -------------------------------------------------------------------------
    # Component Group Definitions
    # -------------------------------------------------------------------------
    # Note: Using "RuntimeGroup" and "DevelopmentGroup" as internal IDs
    # to avoid NSIS naming conflicts with component names "Runtime" and "Development"
    # Display names are still "Runtime" and "Development" for the user

    # Runtime group - components needed to run ascii-chat
    cpack_add_component_group(RuntimeGroup
        DISPLAY_NAME "Runtime"
        DESCRIPTION "Core files needed to run ascii-chat, including the executable and documentation"
        EXPANDED
    )

    # Development group - components needed to develop with ascii-chat
    cpack_add_component_group(DevelopmentGroup
        DISPLAY_NAME "Development"
        DESCRIPTION "All of the tools you'll ever need to develop software using ascii-chat libraries"
    )

    # -------------------------------------------------------------------------
    # Installation Types (NSIS only)
    # -------------------------------------------------------------------------
    # Full: Everything (default)
    cpack_add_install_type(Full
        DISPLAY_NAME "Full"
    )

    # User: Just the runtime application (for end users)
    cpack_add_install_type(User
        DISPLAY_NAME "User"
    )

    # Developer: Runtime + libraries + documentation (for developers)
    cpack_add_install_type(Developer
        DISPLAY_NAME "Developer"
    )

    # -------------------------------------------------------------------------
    # Runtime Components
    # -------------------------------------------------------------------------
    cpack_add_component(Runtime
        DISPLAY_NAME "Application"
        DESCRIPTION "ascii-chat executable - the main application binary"
        GROUP RuntimeGroup
        INSTALL_TYPES Full User
    )

    # -------------------------------------------------------------------------
    # Development Components
    # -------------------------------------------------------------------------
    # Manpages component - only on Unix platforms (not meaningful on Windows)
    if(NOT WIN32)
        cpack_add_component(Manpages
            DISPLAY_NAME "Man Pages"
            DESCRIPTION "Unix manual pages (man pages) for API documentation and library functions"
            GROUP DevelopmentGroup
            DEPENDS Runtime
            INSTALL_TYPES Full Developer
        )
    endif()

    cpack_add_component(Development
        DISPLAY_NAME "Headers and Libraries"
        DESCRIPTION "C header files and libraries for writing code that uses libasciichat"
        GROUP DevelopmentGroup
        INSTALL_TYPES Full Developer
    )

    cpack_add_component(Documentation
        DISPLAY_NAME "Documentation"
        DESCRIPTION "Developer documentation including README, LICENSE, HTML docs"
        GROUP DevelopmentGroup
        INSTALL_TYPES Full Developer
    )

    message(STATUS "${Yellow}CPack:${ColorReset} Package generation enabled")
    message(STATUS "${Yellow}CPack:${ColorReset} Package will be created in: ${BoldBlue}${CPACK_PACKAGE_DIRECTORY}${ColorReset}")
    message(STATUS "${Yellow}CPack:${ColorReset} Generators: ${Magenta}${CPACK_GENERATOR}${ColorReset}")

    # =========================================================================
    # Custom Package Generator Targets
    # =========================================================================
    # Create individual targets for each package type
    # Usage: cmake --build build --target package-wix
    #        cmake --build build --target package-nsis
    #        etc.

    # All supported CPack generators
    set(ALL_CPACK_GENERATORS
        ZIP TGZ TBZ2 TXZ        # Archive formats (cross-platform)
        STGZ                     # Self-extracting shell script (Unix)
        DEB RPM                  # Linux package managers
        WIX NSIS                 # Windows installers
        productbuild             # macOS installers
        7Z                       # 7-Zip archive
        IFW                      # Qt Installer Framework
        FREEBSD                  # FreeBSD pkg
    )

    # Create a target for each generator
    foreach(GENERATOR ${ALL_CPACK_GENERATORS})
        string(TOLOWER "${GENERATOR}" generator_lower)
        add_custom_target(package-${generator_lower}
            COMMAND ${CMAKE_CPACK_COMMAND} -G ${GENERATOR}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Creating ${GENERATOR} package..."
            VERBATIM
        )
    endforeach()

    # Main package target - builds everything then creates packages
    # Builds: executable, static lib, shared lib, docs, then all enabled packages
    # Note: CMake reserves "package" target name, so we use "package-all"
    add_custom_target(package-all
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ascii-chat
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ascii-chat-shared
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target docs
        COMMAND ${CMAKE_CPACK_COMMAND}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building all targets and creating packages..."
        VERBATIM
    )

    message(STATUS "${Yellow}CPack:${ColorReset} Package targets: ${BoldBlue}package${ColorReset} (default), ${BoldBlue}package-all${ColorReset} (builds all first), ${BoldBlue}package-wix${ColorReset}, ${BoldBlue}package-nsis${ColorReset}, etc.")

else()
    message(STATUS "${Red}CPack:${ColorReset} Package generation disabled (set USE_CPACK=ON to enable)")
endif()

