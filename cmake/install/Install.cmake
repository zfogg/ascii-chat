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

# Install binary
install(TARGETS ascii-chat
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

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

# Create a dummy interface target to hold the export for find_package support
add_library(ascii-chat-dummy INTERFACE)
install(TARGETS ascii-chat-dummy
    EXPORT ascii-chat-targets
)

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

    # Shared library (.so/.dylib) goes to lib/
    install(FILES
        "${CMAKE_BINARY_DIR}/lib/libasciichat.so"
        DESTINATION lib
        COMPONENT Development
        OPTIONAL
    )
    message(STATUS "${BoldGreen}Configured${ColorReset} library installation: ${BoldBlue}libasciichat.so${ColorReset} → ${BoldYellow}lib/${ColorReset}")
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
# FHS-compliant: Unix/macOS install to share/man/man3, Windows to doc/man/man3
if(WIN32)
    # Windows: Install manpages to doc/man/man3 (no standard location on Windows)
    # This is the ONLY install rule for manpages on Windows - no share/ directory
    install(DIRECTORY "${CMAKE_BINARY_DIR}/docs/man/man3/"
        DESTINATION ${INSTALL_DOC_DIR}/man/man3
        COMPONENT Manpages
        FILES_MATCHING
        PATTERN "ascii-chat-*.3"  # Only install prefixed manpages (section 3)
        PATTERN "ascii-chat-_*.3" EXCLUDE  # Exclude path-based manpages (e.g. _home_user_...)
        PATTERN "*.3.gz" EXCLUDE  # Exclude compressed manpages if any
        PATTERN "*.3.bz2" EXCLUDE
        PATTERN "*.3.xz" EXCLUDE
    )
elseif(UNIX AND NOT APPLE)
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
#   cmake --build build --target package_source
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
            set(CPACK_INSTALL_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/install/CPackRemoveMimalloc.cmake" CACHE FILEPATH "Post-install cleanup script" FORCE)
        endif()
        include(CPack)
    endif()
endif()

if(USE_CPACK)

    # =========================================================================
    # Basic CPack Configuration
    # =========================================================================
    set(CPACK_PACKAGE_NAME "ascii-chat")
    set(CPACK_PACKAGE_VENDOR "zfogg")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Real-time terminal-based video chat with ASCII art conversion")
    set(CPACK_PACKAGE_DESCRIPTION "ascii-chat is a terminal-based video chat application that converts webcam video to ASCII art in real-time. It supports multiple clients connecting to a single server, with video mixing and audio streaming capabilities.")

    # Version is already set before include(CPack) above
    # Re-set with CACHE FORCE to ensure it's not overridden
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR} CACHE STRING "Package version major" FORCE)
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR} CACHE STRING "Package version minor" FORCE)
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH} CACHE STRING "Package version patch" FORCE)
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Package version" FORCE)

    # Log version being used for packages
    message(STATUS "${Yellow}CPack:${ColorReset} Using version ${BoldGreen}${CPACK_PACKAGE_VERSION}${ColorReset} for packages (from PROJECT_VERSION=${BoldGreen}${PROJECT_VERSION}${ColorReset})")

    # Package metadata
    set(CPACK_PACKAGE_CONTACT "https://github.com/zfogg/ascii-chat")
    if(DEFINED PROJECT_HOMEPAGE_URL)
        set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    endif()

    # License file is already set before include(CPack) above
    # Re-set with CACHE FORCE to ensure CPack doesn't override it
    if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE.txt")
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt" CACHE FILEPATH "License file for installers" FORCE)
        message(STATUS "${Yellow}CPack:${ColorReset} Using ${BoldBlue}LICENSE.txt${ColorReset} for installers")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE" CACHE FILEPATH "License file for installers" FORCE)
        message(STATUS "${Yellow}CPack:${ColorReset} Using ${BoldBlue}LICENSE${ColorReset} for installers")
    endif()

    # Package file name format (use PROJECT_VERSION directly to ensure git version is used)
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

    # Output directory
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

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
    set(CPACK_COMPONENT_GROUP_DEVELOPMENTGROUP_DESCRIPTION "All of the tools you'll need to develop software with libasciichat")
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
        set(CPACK_COMPONENT_MANPAGES_DESCRIPTION "Unix-style manual pages (man pages) generated with Doxygen")
        set(CPACK_COMPONENT_MANPAGES_DISABLED OFF)
    endif()

    # =========================================================================
    # Platform-Specific Package Generators
    # =========================================================================

    if(UNIX AND NOT APPLE)
        # Linux: STGZ (self-extracting .sh), TGZ (always available), DEB, RPM (if tools found)
        set(CPACK_GENERATOR "STGZ;TGZ")

        # CPACK_STGZ_HEADER_FILE is set before include(CPack) above
        # CPACK_SET_DESTDIR must be OFF for STGZ to use prefix-based installation
        set(CPACK_SET_DESTDIR OFF)

        # This sets the default installation directory shown in the STGZ installer
        # Users can override with: ./installer.sh --prefix=/custom/path
        set(CPACK_INSTALL_PREFIX "/usr/local")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

        # Check for package tools and enable generators if available
        find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)
        find_program(RPMBUILD_EXECUTABLE rpmbuild)

        if(DPKG_BUILDPACKAGE_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "DEB")
            message(STATUS "${Yellow}CPack:${ColorReset} DEB generator enabled (${BoldBlue}dpkg-buildpackage${ColorReset} found)")
        else()
            message(STATUS "${Red}CPack:${ColorReset} DEB generator disabled (${BoldBlue}dpkg-buildpackage${ColorReset} not found)")
        endif()

        if(RPMBUILD_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "RPM")
            message(STATUS "${Yellow}CPack:${ColorReset} RPM generator enabled (${BoldBlue}rpmbuild${ColorReset} found)")
        else()
            message(STATUS "${Red}CPack:${ColorReset} RPM generator disabled (${BoldBlue}rpmbuild${ColorReset} not found)")
        endif()

        # DEB package configuration
        if("DEB" IN_LIST CPACK_GENERATOR)
            set(CPACK_DEBIAN_PACKAGE_NAME "ascii-chat")
            set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
            set(CPACK_DEBIAN_PACKAGE_SECTION "net")
            set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
            set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
            endif()
            # Dependencies for shared library component (libasciichat.so)
            # The static executable has no runtime dependencies
            # These are only needed if the Development component is installed
            set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
            set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS "libportaudio2, libasound2, libzstd1, libsodium23, libmimalloc2.0 | libmimalloc-dev")
            set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
            # Maintainer info (optional - can be set via environment)
            if(DEFINED ENV{DEBEMAIL})
                set(CPACK_DEBIAN_PACKAGE_MAINTAINER "$ENV{DEBEMAIL}")
            else()
                set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ascii-chat <${CPACK_PACKAGE_CONTACT}>")
            endif()
        endif()

        # RPM package configuration
        if("RPM" IN_LIST CPACK_GENERATOR)
            set(CPACK_RPM_PACKAGE_NAME "ascii-chat")
            set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
            set(CPACK_RPM_PACKAGE_GROUP "Applications/Networking")
            set(CPACK_RPM_PACKAGE_LICENSE "MIT")
            set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
            endif()
            # Dependencies for shared library component (libasciichat.so)
            # The static executable has no runtime dependencies
            # These are only needed if the Development component is installed
            set(CPACK_RPM_PACKAGE_REQUIRES "")
            set(CPACK_RPM_DEVELOPMENT_PACKAGE_REQUIRES "portaudio, alsa-lib, zstd, libsodium, mimalloc")
            set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
            # Vendor info (optional)
            if(DEFINED ENV{RPM_VENDOR})
                set(CPACK_RPM_PACKAGE_VENDOR "$ENV{RPM_VENDOR}")
            else()
                set(CPACK_RPM_PACKAGE_VENDOR "${CPACK_PACKAGE_VENDOR}")
            endif()
        endif()

    elseif(APPLE)
        # macOS: STGZ (self-extracting .sh), TGZ (always available), DragNDrop/DMG (if hdiutil available)
        set(CPACK_GENERATOR "STGZ;TGZ")

        # CPACK_STGZ_HEADER_FILE is set before include(CPack) above
        # CPACK_SET_DESTDIR must be OFF for STGZ to use prefix-based installation
        set(CPACK_SET_DESTDIR OFF)

        # This sets the default installation directory shown in the STGZ installer
        # Users can override with: ./installer.sh --prefix=/custom/path
        set(CPACK_INSTALL_PREFIX "/usr/local")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

        # Check for hdiutil (macOS DMG creation tool - usually always available on macOS)
        find_program(HDIUTIL_EXECUTABLE hdiutil)
        if(HDIUTIL_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "DragNDrop")
            message(STATUS "${Yellow}CPack:${ColorReset} DMG generator enabled (${BoldBlue}hdiutil${ColorReset} found)")
        else()
            message(STATUS "${Red}CPack:${ColorReset} DMG generator disabled (${BoldBlue}hdiutil${ColorReset} not found)")
        endif()

        # DMG package configuration
        if("DragNDrop" IN_LIST CPACK_GENERATOR)
            set(CPACK_DMG_FORMAT "UDZO")  # Compressed UDIF (read-only)
            set(CPACK_DMG_VOLUME_NAME "${CPACK_PACKAGE_NAME}")
            set(CPACK_DMG_BACKGROUND_IMAGE "")  # Optional: path to background image
            set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "")  # Optional: custom setup script
            set(CPACK_DMG_SLA_DIR "")  # Optional: Software License Agreement directory
        endif()

    elseif(WIN32)
        # Windows: ZIP (always available), NSIS/EXE (if makensis found)
        set(CPACK_GENERATOR "ZIP")

        # Check for NSIS (Nullsoft Scriptable Install System)
        find_program(NSIS_EXECUTABLE makensis)
        if(NOT NSIS_EXECUTABLE)
            # Try alternative NSIS locations (common Windows install paths)
            # Handle ProgramFiles(x86) separately due to CMake variable parsing
            if(DEFINED ENV{ProgramFiles})
                set(PROGRAM_FILES_PATH "$ENV{ProgramFiles}")
            else()
                set(PROGRAM_FILES_PATH "C:/Program Files")
            endif()

            if(DEFINED ENV{ProgramFiles\(x86\)})
                set(PROGRAM_FILES_X86_PATH "$ENV{ProgramFiles\(x86\)}")
            else()
                set(PROGRAM_FILES_X86_PATH "C:/Program Files (x86)")
            endif()

            find_program(NSIS_EXECUTABLE
                NAMES makensis
                PATHS
                    "${PROGRAM_FILES_PATH}/NSIS"
                    "${PROGRAM_FILES_X86_PATH}/NSIS"
                    "C:/Program Files/NSIS"
                    "C:/Program Files (x86)/NSIS"
                PATH_SUFFIXES bin
                NO_DEFAULT_PATH
            )
        endif()

        if(NSIS_EXECUTABLE)
            list(APPEND CPACK_GENERATOR "NSIS")
            message(STATUS "${Yellow}CPack:${ColorReset} NSIS generator enabled (${BoldBlue}makensis${ColorReset} found at ${BoldBlue}${NSIS_EXECUTABLE}${ColorReset})")
        else()
            message(STATUS "${Red}CPack:${ColorReset} NSIS generator disabled (${BoldBlue}makensis${ColorReset} not found - install NSIS to create EXE installers)")
        endif()

        # NSIS installer configuration
        if("NSIS" IN_LIST CPACK_GENERATOR)
            set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
            # Display name without version (just "ascii-chat")
            set(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_NAME}")
            # CPACK_PACKAGE_INSTALL_DIRECTORY is set above and controls the install directory
            # This ensures NSIS installs to "ascii-chat" instead of "ascii-chat 0.3.2"
            # Note: CPACK_PACKAGE_INSTALL_DIRECTORY is already set to "${CPACK_PACKAGE_NAME}" above
            set(CPACK_NSIS_HELP_LINK "${CPACK_PACKAGE_HOMEPAGE_URL}")
            set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
            set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")

            # Add bin directory to PATH using custom NSIS code
            # CPACK_NSIS_MODIFY_PATH only adds $INSTDIR, but we need $INSTDIR\bin
            # This ensures 'ascii-chat' can be run from any directory
            # The installer will add to system PATH if running with admin privileges,
            # or user PATH if running without admin (per-user installation)
            set(CPACK_NSIS_MODIFY_PATH ON)  # We'll also handle PATH manually

            # Custom NSIS code to add bin directory to PATH
            set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
                ; Add bin directory to PATH
                EnVar::SetHKCU
                EnVar::AddValue \\\"PATH\\\" \\\"$INSTDIR\\\\bin\\\"
                Pop \\\$0
            ")

            set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
                ; Remove bin directory from PATH
                EnVar::SetHKCU
                EnVar::DeleteValue \\\"PATH\\\" \\\"$INSTDIR\\\\bin\\\"
                Pop \\\$0
            ")

            # Enable uninstall before install (clean upgrade)
            set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

            # Enable component-based installation
            # This allows users to select which components to install
            set(CPACK_NSIS_COMPONENT_INSTALL ON)

            # Optional: Custom installer icon
            # set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_SOURCE_DIR}/resources/icon.ico")

            # =====================================================================
            # Start Menu Shortcuts
            # =====================================================================
            # Create shortcuts with command-line arguments using custom NSIS code
            # NSIS CreateShortCut syntax: CreateShortCut "link.lnk" "target.exe" "parameters" "icon.file" icon_index start_options
            # SW_SHOWMINIMIZED (7) shows the console window minimized by default
            set(CPACK_NSIS_CREATE_ICONS_EXTRA "
  CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\ascii-chat Client.lnk\\\" \\\"$INSTDIR\\\\bin\\\\ascii-chat.exe\\\" \\\"client\\\"
  CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\ascii-chat Server.lnk\\\" \\\"$INSTDIR\\\\bin\\\\ascii-chat.exe\\\" \\\"server\\\"
")

            # Remove custom shortcuts on uninstall
            set(CPACK_NSIS_DELETE_ICONS_EXTRA "
  Delete \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\ascii-chat Client.lnk\\\"
  Delete \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\ascii-chat Server.lnk\\\"
")

            # Add documentation link to Start Menu
            # CPACK_NSIS_MENU_LINKS uses pairs of (source_file, display_name)
            # The source_file path is relative to the installation directory
            set(CPACK_NSIS_MENU_LINKS
                "doc/html/index.html" "Documentation"
            )
        endif()

    else()
        # Unknown platform: fallback to TGZ
        set(CPACK_GENERATOR "TGZ")
        message(STATUS "${Yellow}CPack:${ColorReset} Using fallback generator ${Magenta}TGZ${ColorReset} for unknown platform")
    endif()

    # =========================================================================
    # Source Package Configuration (optional)
    # =========================================================================
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
else()
    message(STATUS "${Red}CPack:${ColorReset} Package generation disabled (set USE_CPACK=ON to enable)")
endif()

