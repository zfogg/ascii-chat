# =============================================================================
# Windows SDK Detection and Configuration (Windows + Clang Only)
# =============================================================================
# Detects and configures Windows SDK for native Clang builds on Windows
#
# This module:
#   1. Finds Windows Kits directory
#   2. Selects appropriate SDK version (prefers VS preference, fallback to latest)
#   3. Validates SDK has required components (ucrt, um, shared headers)
#   4. Configures include and library paths
#   5. Finds MSVC runtime libraries (required for Clang linking)
#   6. Overrides CMake's incorrect Windows-Clang platform settings
#
# Prerequisites (must be set before including this file):
#   - WIN32: Platform detection variable
#   - CMAKE_C_COMPILER_ID: Compiler identification
#
# Outputs (variables/settings configured by this file):
#   - WINDOWS_SDK_VERSION: Detected SDK version
#   - WIN_ARCH: Architecture (x64 or x86)
#   - include_directories(): Configured with SDK headers
#   - link_directories(): Configured with SDK and MSVC libraries
#   - CMAKE_C_LINK_EXECUTABLE: Modified to fix Clang linking
# =============================================================================

if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Setting up native ${BoldCyan}Windows${ColorReset} build with ${BoldGreen}Clang${ColorReset}")

    # Find Windows SDK - check Visual Studio preferences first, then standard locations
    set(WINDOWS_KIT_PATHS)

    # Check if Visual Studio has a preferred SDK version via MSBuild
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set(PREFERRED_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
        message(STATUS "Visual Studio preferred SDK version: ${PREFERRED_SDK_VERSION}")
    endif()

    # Check standard Windows Kits locations
    list(APPEND WINDOWS_KIT_PATHS
        "C:/Program Files (x86)/Windows Kits/10"
        "C:/Program Files/Windows Kits/10"
        "$ENV{WindowsSdkDir}"
    )

    foreach(KIT_PATH IN LISTS WINDOWS_KIT_PATHS)
        if(EXISTS "${KIT_PATH}" AND NOT WINDOWS_KITS_DIR)
            set(WINDOWS_KITS_DIR "${KIT_PATH}")
            break()
        endif()
    endforeach()

    if(WINDOWS_KITS_DIR)
        # Find SDK version - prioritize Visual Studio preferred version if available
        file(GLOB SDK_VERSIONS "${WINDOWS_KITS_DIR}/Lib/10.*")
        if(SDK_VERSIONS)
            # Check if Visual Studio has a preferred SDK version and it's available
            if(PREFERRED_SDK_VERSION)
                set(PREFERRED_SDK_PATH "${WINDOWS_KITS_DIR}/Lib/${PREFERRED_SDK_VERSION}")
                if(EXISTS "${PREFERRED_SDK_PATH}")
                    set(WINDOWS_SDK_VERSION ${PREFERRED_SDK_VERSION})
                    message(STATUS "Using Visual Studio preferred SDK version: ${WINDOWS_SDK_VERSION}")
                else()
                    message(STATUS "Visual Studio preferred SDK version ${PREFERRED_SDK_VERSION} not found, selecting latest available")
                endif()
            endif()

            # If no preferred version or preferred version not found, select latest
            if(NOT WINDOWS_SDK_VERSION)
                # Sort SDK versions properly by parsing version numbers
                set(SORTED_SDK_VERSIONS)
                foreach(sdk_path ${SDK_VERSIONS})
                    get_filename_component(sdk_version ${sdk_path} NAME)
                    # Extract version components (e.g., "10.0.22621.0" -> 10, 0, 22621, 0)
                    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)" version_match "${sdk_version}")
                    if(version_match)
                        set(major ${CMAKE_MATCH_1})
                        set(minor ${CMAKE_MATCH_2})
                        set(build ${CMAKE_MATCH_3})
                        set(revision ${CMAKE_MATCH_4})
                        # Create sortable version string (pad with zeros for proper sorting)
                        string(LENGTH ${build} build_len)
                        string(LENGTH ${revision} revision_len)
                        if(build_len LESS 5)
                            string(REPEAT "0" 5 build_pad)
                            math(EXPR pad_len "5 - ${build_len}")
                            string(SUBSTRING ${build_pad} 0 ${pad_len} build_pad)
                            set(build "${build_pad}${build}")
                        endif()
                        if(revision_len LESS 3)
                            string(REPEAT "0" 3 revision_pad)
                            math(EXPR rev_pad_len "3 - ${revision_len}")
                            string(SUBSTRING ${revision_pad} 0 ${rev_pad_len} revision_pad)
                            set(revision "${revision_pad}${revision}")
                        endif()
                        set(sort_key "${major}.${minor}.${build}.${revision}")
                        list(APPEND SORTED_SDK_VERSIONS "${sort_key}|${sdk_version}")
                    endif()
                endforeach()

                # Sort by the sortable key
                list(SORT SORTED_SDK_VERSIONS COMPARE NATURAL ORDER DESCENDING)
                list(GET SORTED_SDK_VERSIONS 0 LATEST_SDK_ENTRY)
                string(REGEX REPLACE "^[^|]*\\|" "" WINDOWS_SDK_VERSION "${LATEST_SDK_ENTRY}")
                message(STATUS "Selected latest available SDK version: ${BoldCyan}${WINDOWS_SDK_VERSION}${ColorReset}")
            endif()

            # Validate SDK has required components
            if(EXISTS "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/ucrt" AND
               EXISTS "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/ucrt/x64")

                # Architecture detection
                if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                    set(WIN_ARCH x64)
                else()
                    set(WIN_ARCH x86)
                endif()

                # Add Windows SDK paths
                include_directories(
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/ucrt"
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/um"
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/shared"
                )

                link_directories(
                    "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/ucrt/${WIN_ARCH}"
                    "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/um/${WIN_ARCH}"
                )

                # Find MSVC runtime libraries (required for Clang with lld-link)
                set(MSVC_BASE_PATHS
                    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
                )

                set(MSVC_LIB_DIR)
                foreach(MSVC_BASE IN LISTS MSVC_BASE_PATHS)
                    if(EXISTS "${MSVC_BASE}")
                        file(GLOB MSVC_VERSIONS "${MSVC_BASE}/*")
                        if(MSVC_VERSIONS)
                            # Sort to get latest version
                            list(SORT MSVC_VERSIONS COMPARE NATURAL ORDER DESCENDING)
                            list(GET MSVC_VERSIONS 0 MSVC_LATEST)
                            set(MSVC_LIB_CANDIDATE "${MSVC_LATEST}/lib/${WIN_ARCH}")
                            if(EXISTS "${MSVC_LIB_CANDIDATE}")
                                set(MSVC_LIB_DIR "${MSVC_LIB_CANDIDATE}")
                                get_filename_component(MSVC_VERSION_DIR "${MSVC_LATEST}" NAME)
                                message(STATUS "Found ${BoldGreen}MSVC${ColorReset} runtime libraries: ${BoldCyan}${MSVC_VERSION_DIR}${ColorReset}")
                                break()
                            endif()
                        endif()
                    endif()
                endforeach()

                if(MSVC_LIB_DIR)
                    link_directories("${MSVC_LIB_DIR}")
                else()
                    message(WARNING "\n"
                        "================================================================================\n"
                        "WARNING: MSVC Runtime Libraries Not Found\n"
                        "================================================================================\n"
                        "Clang requires MSVC runtime libraries (libcmt.lib, oldnames.lib) for linking.\n"
                        "Release builds may fail without these libraries.\n"
                        "\n"
                        "To install MSVC runtime libraries:\n"
                        "  1. Open Visual Studio Installer\n"
                        "  2. Click 'Modify' on your Visual Studio installation\n"
                        "  3. Go to 'Individual Components' tab\n"
                        "  4. Search for 'MSVC v143 - VS 2022 C++ x64/x86 build tools'\n"
                        "  5. Select the component and click 'Modify' to install\n"
                        "================================================================================")
                endif()

                # Validate Windows SDK version
                string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" SDK_VERSION_MATCH "${WINDOWS_SDK_VERSION}")
                if(SDK_VERSION_MATCH)
                    set(SDK_MAJOR ${CMAKE_MATCH_1})
                    set(SDK_MINOR ${CMAKE_MATCH_2})

                    # Check for minimum version 10.0
                    if(SDK_MAJOR LESS 10)
                        message(FATAL_ERROR "\n"
                            "================================================================================\n"
                            "ERROR: Insufficient Windows SDK Version\n"
                            "================================================================================\n"
                            "ascii-chat requires Windows SDK 10.0 or higher for Media Foundation APIs.\n"
                            "Current version detected: ${WINDOWS_SDK_VERSION}\n"
                            "\n"
                            "To install Windows SDK 10.0 or higher:\n"
                            "\n"
                            "METHOD 1 - Visual Studio Installer (Recommended):\n"
                            "  1. Open Visual Studio Installer\n"
                            "  2. Click 'Modify' on your Visual Studio installation\n"
                            "  3. Go to 'Individual Components' tab\n"
                            "  4. Search for 'Windows 10 SDK' or 'Windows 11 SDK'\n"
                            "  5. Select the latest version (10.0.22621.0 or newer)\n"
                            "  6. Click 'Modify' to install\n"
                            "\n"
                            "METHOD 2 - Standalone Installer:\n"
                            "  1. Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
                            "  2. Run the installer and select 'Windows SDK for Desktop C++ Development'\n"
                            "\n"
                            "METHOD 3 - Command Line (winget):\n"
                            "  winget install Microsoft.WindowsSDK\n"
                            "\n"
                            "After installation, restart your command prompt and try building again.\n"
                            "================================================================================")
                    else()
                        # Verbose message - summary table will show SDK version
                        if(CMAKE_FIND_DEBUG_MODE)
                            message(STATUS "${BoldGreen}Windows SDK${ColorReset} version ${BoldCyan}${WINDOWS_SDK_VERSION}${ColorReset} detected - ${BoldGreen}sufficient${ColorReset} for ascii-chat")
                        endif()
                    endif()
                else()
                    message(WARNING "Could not parse ${BoldYellow}Windows SDK${ColorReset} version: ${WINDOWS_SDK_VERSION}")
                endif()
            else()
                message(WARNING "Windows SDK ${WINDOWS_SDK_VERSION} found but missing required components (ucrt)")
            endif()
        else()
            message(WARNING "\n"
                "================================================================================\n"
                "WARNING: Could not detect Windows SDK version\n"
                "================================================================================\n"
                "ascii-chat requires Windows SDK 10.0 or higher for Media Foundation APIs.\n"
                "If you encounter build errors related to missing headers (mfapi.h, mfidl.h),\n"
                "please install Windows SDK 10.0 or higher using one of these methods:\n"
                "\n"
                "METHOD 1 - Visual Studio Installer (Recommended):\n"
                "  1. Open Visual Studio Installer\n"
                "  2. Click 'Modify' on your Visual Studio installation\n"
                "  3. Go to 'Individual Components' tab\n"
                "  4. Search for 'Windows 10 SDK' or 'Windows 11 SDK'\n"
                "  5. Select the latest version (10.0.22621.0 or newer)\n"
                "  6. Click 'Modify' to install\n"
                "\n"
                "METHOD 2 - Standalone Installer:\n"
                "  Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
                "\n"
                "METHOD 3 - Command Line (winget):\n"
                "  winget install Microsoft.WindowsSDK\n"
                "================================================================================")
        endif()
    else()
        message(WARNING "\n"
            "================================================================================\n"
            "WARNING: Could not find Windows Kits directory\n"
            "================================================================================\n"
            "ascii-chat requires Windows SDK 10.0 or higher for Media Foundation APIs.\n"
            "Please install Windows SDK 10.0 or higher using one of these methods:\n"
            "\n"
            "METHOD 1 - Visual Studio Installer (Recommended):\n"
            "  1. Open Visual Studio Installer\n"
            "  2. Click 'Modify' on your Visual Studio installation\n"
            "  3. Go to 'Individual Components' tab\n"
            "  4. Search for 'Windows 10 SDK' or 'Windows 11 SDK'\n"
            "  5. Select the latest version (10.0.22621.0 or newer)\n"
            "  6. Click 'Modify' to install\n"
            "\n"
            "METHOD 2 - Standalone Installer:\n"
            "  Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
            "\n"
            "METHOD 3 - Command Line (winget):\n"
            "  winget install Microsoft.WindowsSDK\n"
            "================================================================================")
    endif()

    # Override CMake's Windows-Clang platform settings that add -nostartfiles -nostdlib
    # These flags prevent linking to oldnames.lib and cause link errors
    # CMake automatically adds these in the Windows-Clang toolchain but they're incorrect for our use case
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Clear the CMake flags that cause -nostartfiles -nostdlib to be added
        set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_C_COMPILER> <CMAKE_SHARED_LIBRARY_C_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
        set(CMAKE_C_CREATE_SHARED_MODULE "${CMAKE_C_CREATE_SHARED_LIBRARY}")
        set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

        # Disable runtime library selection that adds libcmt.lib and oldnames.lib
        # These are legacy compatibility libraries not needed for modern C code with Clang
        set(CMAKE_MSVC_RUNTIME_LIBRARY "")
        set(CMAKE_C_STANDARD_LIBRARIES "")

        # Also clear any runtime library flags that might be set by CMake for Windows
        set(CMAKE_C_STANDARD_LIBRARIES_INIT "")
        set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "")

        # Don't add any additional runtime libraries - let Clang use its defaults
        # The key fix was removing -nostartfiles -nostdlib, now standard linking should work

        message(STATUS "Overrode CMake Windows-Clang linking rules to prevent -nostartfiles -nostdlib")
        message(STATUS "Removed oldnames.lib from standard libraries (legacy compatibility library not needed)")
    endif()

endif()
