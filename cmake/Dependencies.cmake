# =============================================================================
# Dependency Finding and Configuration
# =============================================================================
# Finds and configures all external dependencies for ascii-chat
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg for package management
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Linux (musl): Dependencies built from source (see MuslDependencies.cmake)
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT: (Windows only) vcpkg installation path
#
# Outputs (variables set by this file):
#   Platform-specific dependency variables for linking
# =============================================================================

# Find Dependencies (matching Makefile pkg-config approach)
# =============================================================================

# Platform-specific package management and library linking
if(WIN32)
    # Native Windows with Clang - use vcpkg
    # Setup vcpkg paths if available
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT $ENV{VCPKG_ROOT})
        message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    endif()

    # Find packages using vcpkg (toolchain already configured at top of file)
    # Use static libraries for Release builds, dynamic (DLL) libraries for Debug/Dev builds
    # This avoids shipping DLLs with Release builds while enabling easier debugging

    # Determine triplet based on build type
    if(CMAKE_BUILD_TYPE MATCHES "Release")
        set(VCPKG_TRIPLET "x64-windows-static")
        set(VCPKG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/lib")
        set(VCPKG_DEBUG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/debug/lib")
        set(VCPKG_INCLUDE_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/include")
        message(STATUS "Using static libraries for Release build (triplet: ${VCPKG_TRIPLET})")
    else()
        set(VCPKG_TRIPLET "x64-windows")
        set(VCPKG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/lib")
        set(VCPKG_DEBUG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/debug/lib")
        set(VCPKG_INCLUDE_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/include")
        message(STATUS "Using dynamic libraries for Debug/Dev build (triplet: ${VCPKG_TRIPLET})")
    endif()

    # Set CMake paths to use the selected triplet
    set(CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}" ${CMAKE_PREFIX_PATH})
    include_directories("${VCPKG_INCLUDE_PATH}")
    link_directories("${VCPKG_LIB_PATH}")

    # Find zstd
    find_library(ZSTD_LIBRARY_RELEASE NAMES zstd zstd_static PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
    find_library(ZSTD_LIBRARY_DEBUG NAMES zstdd zstd zstd_static PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

    if(ZSTD_LIBRARY_RELEASE OR ZSTD_LIBRARY_DEBUG)
        set(ZSTD_LIBRARIES optimized ${ZSTD_LIBRARY_RELEASE} debug ${ZSTD_LIBRARY_DEBUG})
        set(ZSTD_INCLUDE_DIRS ${ZSTD_INCLUDE_DIR})
        set(ZSTD_FOUND TRUE)
        message(STATUS "Found ZSTD: ${ZSTD_LIBRARY_RELEASE}")

        # Define ZSTD_STATIC for static builds to prevent dllimport
        if(CMAKE_BUILD_TYPE MATCHES "Release")
            add_compile_definitions(ZSTD_STATIC)
        endif()
    else()
        message(FATAL_ERROR "Could not find zstd - required dependency")
    endif()

    # Use vcpkg for all builds (MSVC, Clang, and GCC with compatibility stubs)
    find_library(LIBSODIUM_LIBRARY_RELEASE NAMES libsodium sodium PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
    find_library(LIBSODIUM_LIBRARY_DEBUG NAMES libsodium sodium PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
    find_path(LIBSODIUM_INCLUDE_DIR NAMES sodium.h PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

        if(LIBSODIUM_LIBRARY_RELEASE OR LIBSODIUM_LIBRARY_DEBUG)
            set(LIBSODIUM_LIBRARIES optimized ${LIBSODIUM_LIBRARY_RELEASE} debug ${LIBSODIUM_LIBRARY_DEBUG})
            set(LIBSODIUM_INCLUDE_DIRS ${LIBSODIUM_INCLUDE_DIR})
            set(LIBSODIUM_FOUND TRUE)
            message(STATUS "Found libsodium: ${LIBSODIUM_LIBRARY_RELEASE}")

            # Define SODIUM_STATIC for static builds to prevent dllimport
            if(CMAKE_BUILD_TYPE MATCHES "Release")
                add_compile_definitions(SODIUM_STATIC)
            endif()
        else()
            message(WARNING "Could not find libsodium - will continue without encryption")
            set(LIBSODIUM_FOUND FALSE)
            set(LIBSODIUM_LIBRARIES "")
            set(LIBSODIUM_INCLUDE_DIRS "")
        endif()

    # Try to find BearSSL (system install from Docker or pkg-config)
    # First check for system-installed version (e.g., from Docker image)
    find_library(BEARSSL_SYSTEM_LIB NAMES bearssl libbearssl bearssls
                 PATHS /usr/local/lib /usr/lib
                 NO_DEFAULT_PATH)
    find_path(BEARSSL_SYSTEM_INC NAMES bearssl.h
              PATHS /usr/local/include /usr/include
              NO_DEFAULT_PATH)

    if(BEARSSL_SYSTEM_LIB AND BEARSSL_SYSTEM_INC)
        # Use system-installed BearSSL (from Docker)
        add_library(bearssl_static STATIC IMPORTED)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_SYSTEM_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "Using system BearSSL library: ${BEARSSL_SYSTEM_LIB}")
    # Fall back to building from submodule
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/bearssl")
        set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")
        # Build to cache directory to keep platform-specific builds separate
        set(BEARSSL_BUILD_DIR "${DEPS_CACHE_DIR}/${CMAKE_BUILD_TYPE}/bearssl")
        # Windows uses bearssls.lib, Unix uses libbearssl.a
        set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/bearssls.lib")

        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Only build BearSSL if the cached library doesn't exist
        if(NOT EXISTS "${BEARSSL_LIB}")
            message(STATUS "BearSSL library not found in cache, will build from source: ${BEARSSL_LIB}")

            # Apply Windows+Clang patch to BearSSL (fixes header conflicts with clang-cl)
            # This needs to happen at configure time to ensure the source is patched
            set(BEARSSL_PATCH_FILE "${CMAKE_SOURCE_DIR}/cmake/bearssl-windows-clang.patch")
            execute_process(
                COMMAND git -C "${BEARSSL_SOURCE_DIR}" diff --quiet src/inner.h
                RESULT_VARIABLE BEARSSL_PATCH_NEEDED
                OUTPUT_QUIET ERROR_QUIET
            )
            if(NOT BEARSSL_PATCH_NEEDED EQUAL 0)
                message(STATUS "Applying BearSSL patch for Windows+Clang compatibility")
                execute_process(
                    COMMAND git -C "${BEARSSL_SOURCE_DIR}" apply --ignore-whitespace "${BEARSSL_PATCH_FILE}"
                    RESULT_VARIABLE BEARSSL_PATCH_RESULT
                    OUTPUT_QUIET ERROR_QUIET
                )
                if(NOT BEARSSL_PATCH_RESULT EQUAL 0)
                    message(WARNING "Failed to apply BearSSL patch (may already be applied)")
                endif()
            endif()

            # Find build tools
            find_program(NMAKE_EXECUTABLE nmake REQUIRED)
            find_program(CLANG_CL_EXECUTABLE clang-cl REQUIRED)
            find_program(LLVM_LIB_EXECUTABLE llvm-lib REQUIRED)

            # Add custom command to build BearSSL if library is missing
            # This creates a build rule that Ninja/Make can use to rebuild the library
            add_custom_command(
                OUTPUT "${BEARSSL_LIB}"
                COMMAND ${CMAKE_COMMAND} -E echo "Building BearSSL library with nmake..."
                COMMAND "${NMAKE_EXECUTABLE}" "CC=${CLANG_CL_EXECUTABLE}" "AR=${LLVM_LIB_EXECUTABLE}" lib
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEARSSL_SOURCE_DIR}/build/bearssls.lib" "${BEARSSL_LIB}"
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                COMMENT "Building BearSSL library to cache: ${BEARSSL_BUILD_DIR}"
                VERBATIM
            )

            # Add custom target that depends on the library
            add_custom_target(bearssl_build DEPENDS "${BEARSSL_LIB}")
        else()
            message(STATUS "BearSSL library found in cache: ${BEARSSL_LIB}")
            # Create a dummy target so dependencies work
            add_custom_target(bearssl_build)
        endif()

        # Create an imported library that links to the custom command output
        add_library(bearssl_static STATIC IMPORTED GLOBAL)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE
            "${BEARSSL_SOURCE_DIR}/inc"
        )
        # Make sure the library is built before anything tries to link against it
        add_dependencies(bearssl_static bearssl_build)

        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "BearSSL configured: ${BEARSSL_LIB}")
    else()
        message(WARNING "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
        set(BEARSSL_FOUND FALSE)
        set(BEARSSL_LIBRARIES "")
        set(BEARSSL_INCLUDE_DIRS "")
    endif()
else()
    # Unix/Linux/macOS use pkg-config (matches Makefile)
    find_package(PkgConfig REQUIRED)

    # Core dependencies (matching PKG_CONFIG_LIBS in Makefile)
    # On macOS, prefer Homebrew zstd over system zstd for consistency
    if(APPLE)
        # Check for Homebrew zstd first
        if(EXISTS "/usr/local/opt/zstd/lib/pkgconfig/libzstd.pc")
            set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/zstd/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
            message(STATUS "Using Homebrew zstd from /usr/local/opt/zstd")
        elseif(EXISTS "/opt/homebrew/opt/zstd/lib/pkgconfig/libzstd.pc")
            set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/zstd/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
            message(STATUS "Using Homebrew zstd from /opt/homebrew/opt/zstd")
        endif()
    endif()

    # Skip pkg-config when using musl - dependencies are built from source
    if(NOT USE_MUSL)
        pkg_check_modules(ZSTD REQUIRED libzstd)
        pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
        pkg_check_modules(LIBSODIUM REQUIRED libsodium)
    endif()

    # Try to find BearSSL (system install from Docker or pkg-config)
    # First check for system-installed version (e.g., from Docker image)
    find_library(BEARSSL_SYSTEM_LIB NAMES bearssl libbearssl
                 PATHS /usr/local/lib /usr/lib
                 NO_DEFAULT_PATH)
    find_path(BEARSSL_SYSTEM_INC NAMES bearssl.h
              PATHS /usr/local/include /usr/include
              NO_DEFAULT_PATH)

    if(BEARSSL_SYSTEM_LIB AND BEARSSL_SYSTEM_INC)
        # Use system-installed BearSSL (from Docker)
        add_library(bearssl_static STATIC IMPORTED)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_SYSTEM_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "Using system BearSSL library: ${BEARSSL_SYSTEM_LIB}")
    # Fall back to building from submodule
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/bearssl")
        set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")
        # Build to cache directory (DEPS_CACHE_DIR already includes build type)
        set(BEARSSL_BUILD_DIR "${DEPS_CACHE_DIR}/bearssl")
        set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/libbearssl.a")

        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Only add build command if library doesn't exist
        if(NOT EXISTS "${BEARSSL_LIB}")
            message(STATUS "BearSSL library not found in cache, will build from source: ${BEARSSL_LIB}")

            # For musl builds: disable getentropy() (not in musl), force /dev/urandom, disable fortification
            # Always add -fPIC for shared library support
            if(USE_MUSL)
                set(BEARSSL_EXTRA_CFLAGS "-fPIC -DBR_USE_GETENTROPY=0 -DBR_USE_URANDOM=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector")
                set(BEARSSL_CC "/usr/bin/musl-gcc")
            else()
                set(BEARSSL_EXTRA_CFLAGS "-fPIC")
                set(BEARSSL_CC "${CMAKE_C_COMPILER}")
            endif()

            # Clean BearSSL build directory before initial build
            message(STATUS "  Cleaning BearSSL build directory before initial build...")
            execute_process(
                COMMAND make clean
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )

            add_custom_command(
                OUTPUT "${BEARSSL_LIB}"
                COMMAND ${CMAKE_COMMAND} -E echo "Building BearSSL library (static only) with make lib..."
                COMMAND make lib CC=${BEARSSL_CC} AR=${CMAKE_AR} "CFLAGS=${BEARSSL_EXTRA_CFLAGS}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEARSSL_SOURCE_DIR}/build/libbearssl.a" "${BEARSSL_LIB}"
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                COMMENT "Building BearSSL static library to cache: ${BEARSSL_BUILD_DIR}"
                VERBATIM
            )

            # Add custom target that depends on the library
            add_custom_target(bearssl_build DEPENDS "${BEARSSL_LIB}")
        else()
            message(STATUS "BearSSL library found in cache: ${BEARSSL_LIB}")
            # Create a dummy target so dependencies work
            add_custom_target(bearssl_build)
        endif()

        # Create an imported library that links to the cached library
        add_library(bearssl_static STATIC IMPORTED GLOBAL)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE
            "${BEARSSL_SOURCE_DIR}/inc"
        )
        # Make sure the library is built before anything tries to link against it
        add_dependencies(bearssl_static bearssl_build)

        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "BearSSL configured: ${BEARSSL_LIB}")
    else()
        message(WARNING "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
        set(BEARSSL_FOUND FALSE)
        set(BEARSSL_LIBRARIES "")
        set(BEARSSL_INCLUDE_DIRS "")
    endif()
endif()

# Test dependencies (matching TEST_PKG_CONFIG_LIBS in Makefile)
# Criterion detection is outside the USE_MUSL block so tests work with musl
# Disable tests for musl builds - Criterion test framework requires glibc
# Tests can be run with standard glibc builds instead
if(USE_MUSL)
    set(BUILD_TESTS OFF)
    message(STATUS "Tests disabled for musl builds (Criterion requires glibc)")
endif()

# Windows doesn't use pkg-config, so skip Criterion detection on Windows
# Tests are primarily Unix-based (Criterion requires pkg-config)
if(BUILD_TESTS AND NOT WIN32)
    pkg_check_modules(CRITERION criterion)
endif()

# Windows SDK detection for native Clang builds
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Setting up native Windows build with Clang")

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
                message(STATUS "Selected latest available SDK version: ${WINDOWS_SDK_VERSION}")
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
                                message(STATUS "Found MSVC runtime libraries: ${MSVC_VERSION_DIR}")
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

                message(STATUS "Found Windows SDK ${WINDOWS_SDK_VERSION}")

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
                        message(STATUS "Windows SDK version ${WINDOWS_SDK_VERSION} detected - sufficient for ascii-chat")
                    endif()
                else()
                    message(WARNING "Could not parse Windows SDK version: ${WINDOWS_SDK_VERSION}")
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
# Find Windows-specific libraries
if(WIN32)
    # Standard Windows system libraries for native builds
    set(WS2_32_LIB ws2_32)
    set(USER32_LIB user32)
    set(ADVAPI32_LIB advapi32)
    set(DBGHELP_LIB dbghelp)
    set(MF_LIB mf)
    set(MFPLAT_LIB mfplat)
    set(MFREADWRITE_LIB mfreadwrite)
    set(MFUUID_LIB mfuuid)
    set(OLE32_LIB ole32)

    # Find PortAudio
    find_library(PORTAUDIO_LIBRARY_RELEASE NAMES portaudio PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
    find_library(PORTAUDIO_LIBRARY_DEBUG NAMES portaudio PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
    find_path(PORTAUDIO_INCLUDE_DIR NAMES portaudio.h PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

    if(PORTAUDIO_LIBRARY_RELEASE OR PORTAUDIO_LIBRARY_DEBUG)
        set(PORTAUDIO_LIBRARIES optimized ${PORTAUDIO_LIBRARY_RELEASE} debug ${PORTAUDIO_LIBRARY_DEBUG})
        set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIR})
        set(PORTAUDIO_FOUND TRUE)
        message(STATUS "Found PortAudio: ${PORTAUDIO_LIBRARY_RELEASE}")
    else()
        message(FATAL_ERROR "Could not find portaudio - required dependency")
    endif()
else()
    # Platform-specific libraries (matching Makefile logic)
    if(PLATFORM_DARWIN)
        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
        find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
        find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    elseif(PLATFORM_LINUX)
        find_package(Threads REQUIRED)

        # Linux library search paths (matches Makefile)
        link_directories(/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu)

        # Additional Linux test dependencies (matching Makefile complex setup)
        if(CRITERION_FOUND)
            # Protobuf-C
            pkg_check_modules(PROTOBUF_C libprotobuf-c)
            if(NOT PROTOBUF_C_FOUND)
                find_library(PROTOBUF_C_LIBRARIES protobuf-c)
            endif()

            # Nanopb
            pkg_check_modules(NANOPB nanopb)
            if(NOT NANOPB_FOUND)
                find_library(NANOPB_LIBRARIES protobuf-nanopb PATHS /usr/lib/x86_64-linux-gnu)
            endif()

            # Boxfort (sandboxing for criterion)
            pkg_check_modules(BOXFORT boxfort)
            if(NOT BOXFORT_FOUND)
                find_library(BOXFORT_LIBRARIES boxfort)
            endif()

            # Optional: nanomsg, libgit2
            pkg_check_modules(NANOMSG nanomsg)
            pkg_check_modules(LIBGIT2 libgit2)

            # GSSAPI/Kerberos support
            pkg_check_modules(KRB5_GSSAPI krb5-gssapi)
            if(NOT KRB5_GSSAPI_FOUND)
                pkg_check_modules(KRB5_GSSAPI mit-krb5-gssapi)
            endif()
            if(NOT KRB5_GSSAPI_FOUND)
                pkg_check_modules(LIBSSH2 libssh2)
            endif()
        endif()
    endif()
endif()

