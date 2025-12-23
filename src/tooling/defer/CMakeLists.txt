cmake_minimum_required(VERSION 3.20)

# On Windows, prevent CMake from running compiler tests that would add problematic flags
if(WIN32)
    set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
    set(CMAKE_C_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
endif()

# Note: On macOS, the parent build (cmake/tooling/Defer.cmake) sets CMAKE_CXX_COMPILER
# to Apple's system clang (/usr/bin/clang++) BEFORE invoking this ExternalProject.
# This is necessary because Homebrew LLVM's bundled libc++ headers are incompatible
# with manual header path configuration (-nostdinc/-nostdinc++). We use Apple's clang
# for compilation but link against Homebrew's LLVM/Clang libraries.

project(ascii-instr-defer CXX)

# Override CMake's Windows-Clang platform settings AFTER project() to prevent -nostartfiles -nostdlib
# This is the same approach used by the main ascii-chat build
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clear the CMake rules that add -nostartfiles -nostdlib
    set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_CXX_COMPILER> <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
    set(CMAKE_CXX_CREATE_SHARED_MODULE "${CMAKE_CXX_CREATE_SHARED_LIBRARY}")
    set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

    # Disable MSVC runtime library selection
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")
    set(CMAKE_CXX_STANDARD_LIBRARIES "")
    set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "")
    set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")

    message(STATUS "Overrode CMake Windows-Clang linking rules for defer tool to prevent -nostartfiles -nostdlib")
endif()

# This is a standalone project for the defer() transformation tool
# It must NOT inherit flags from the main ascii-chat build (musl, static-pie, etc.)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Get LLVM/Clang configuration using centralized detection module
# This handles Windows (no llvm-config) vs Unix (uses llvm-config) automatically
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/utils/DetectLLVMPaths.cmake")

# Create the executable
add_executable(ascii-instr-defer tool.cpp)

# Platform-specific include and compile setup
if(APPLE)
    # macOS: Use Homebrew LLVM's clang for compilation to match the libclang/libclang-cpp
    # we're linking against. This ensures compatible resource-dir and include paths.
    message(STATUS "macOS: Using Homebrew LLVM clang for compilation and Homebrew LLVM libraries")
    message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "  LLVM libraries: ${LLVM_LIBRARY_DIRS}")
    message(STATUS "  LLVM headers: ${LLVM_INCLUDE_DIRS}")

    # macOS: Homebrew LLVM's clang can find libc++ headers automatically
    target_include_directories(ascii-instr-defer SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
    )

    target_compile_options(ascii-instr-defer PRIVATE
        -O2
        -fno-rtti
        -fexceptions
        -fno-lto
    )

    # Link against Homebrew LLVM libraries with proper rpath
    # Note: -fno-lto disables LTO to avoid bitcode compatibility issues when
    # linking system clang's output against Homebrew LLVM libraries
    target_link_directories(ascii-instr-defer PRIVATE
        ${LLVM_LIBRARY_DIRS}
    )
    target_link_options(ascii-instr-defer PRIVATE
        -fno-lto
        "-Wl,-rpath,${LLVM_LIBRARY_DIRS}"
    )
else()
    # Non-macOS (Linux, Windows)
    # Standard compile flags for the defer tool
    #
    # NOTE: If you have a different LLVM version installed in /usr/local/include,
    # clang will find those headers first, causing ABI mismatches with system LLVM.
    # Solution: Use ASCIICHAT_LLVM_CONFIG_EXECUTABLE to point to your custom LLVM,
    # or remove conflicting headers from /usr/local/include.

    # Add LLVM include directories (needed for clang/AST/*.h headers)
    target_include_directories(ascii-instr-defer SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
    )

    target_compile_options(ascii-instr-defer PRIVATE
        -O2
        -fno-rtti
        -fexceptions
    )

    # Linux: Set rpath to ensure we use the LLVM libs we built against,
    # not any custom LLVM in /usr/local that might be in LD_LIBRARY_PATH
    if(NOT WIN32)
        target_link_options(ascii-instr-defer PRIVATE
            "-Wl,-rpath,${LLVM_LIBRARY_DIRS}"
        )
    endif()
endif()

# On Windows, use dynamic runtime (/MD) to match Clang libraries
if(WIN32)
    target_compile_options(ascii-instr-defer PRIVATE
        $<$<CONFIG:Debug>:-D_DLL -D_MT>
        $<$<CONFIG:Release>:-D_DLL -D_MT>
    )
    target_link_options(ascii-instr-defer PRIVATE
        $<$<CONFIG:Debug>:-Xclang --dependent-lib=msvcrtd>
        $<$<CONFIG:Release>:-Xclang --dependent-lib=msvcrt>
    )
endif()

# Platform-specific linking
if(WIN32)
    # Windows: Link static Clang libraries
    set(CLANG_LIBS
        clangTooling clangFrontend clangDriver clangCodeGen clangOptions
        clangAST clangASTMatchers clangBasic
        clangRewrite clangRewriteFrontend clangLex clangSerialization
        clangParse clangSema clangEdit clangAnalysis
        clangAPINotes clangSupport
    )

    target_compile_definitions(ascii-instr-defer PRIVATE
        LLVM_BUILD_STATIC
        CLANG_BUILD_STATIC
    )

    target_link_libraries(ascii-instr-defer PRIVATE
        ${CLANG_LIBS}
        ${LLVM_LIBRARY_DIRS}/LLVMSupport.lib
        ${LLVM_LIBRARY_DIRS}/LLVMCore.lib
        ${LLVM_LIBRARY_DIRS}/LLVMBinaryFormat.lib
        ${LLVM_LIBRARY_DIRS}/LLVMRemarks.lib
        ${LLVM_LIBRARY_DIRS}/LLVMBitstreamReader.lib
        ${LLVM_LIBRARY_DIRS}/LLVMOption.lib
        ${LLVM_LIBRARY_DIRS}/LLVMProfileData.lib
        ${LLVM_LIBRARY_DIRS}/LLVMFrontendOpenMP.lib
        ${LLVM_LIBRARY_DIRS}/LLVMFrontendOffloading.lib
        ${LLVM_LIBRARY_DIRS}/LLVMFrontendAtomic.lib
        ${LLVM_LIBRARY_DIRS}/LLVMDemangle.lib
        ${LLVM_LIBRARY_DIRS}/LLVMTargetParser.lib
        ${LLVM_LIBRARY_DIRS}/LLVMMC.lib
        ${LLVM_LIBRARY_DIRS}/LLVMMCParser.lib
        ${LLVM_LIBRARY_DIRS}/LLVMDebugInfoDWARF.lib
        ${LLVM_LIBRARY_DIRS}/LLVMAnalysis.lib
        ${LLVM_LIBRARY_DIRS}/LLVMTransformUtils.lib
        ${LLVM_LIBRARY_DIRS}/LLVMIRReader.lib
        ${LLVM_LIBRARY_DIRS}/LLVMBitReader.lib
        ${LLVM_LIBRARY_DIRS}/LLVMAsmParser.lib
        ${LLVM_LIBRARY_DIRS}/LLVMObject.lib
        ${LLVM_LIBRARY_DIRS}/LLVMObjectYAML.lib
        ${LLVM_LIBRARY_DIRS}/LLVMTextAPI.lib
        ${LLVM_LIBRARY_DIRS}/LLVMDebugInfoDWARFLowLevel.lib
        ${LLVM_LIBRARY_DIRS}/LLVMWindowsDriver.lib
        ${LLVM_LIBRARY_DIRS}/LLVMCodeGen.lib
        ${LLVM_LIBRARY_DIRS}/LLVMAsmPrinter.lib
        ${LLVM_LIBRARY_DIRS}/LLVMSelectionDAG.lib
        ${LLVM_LIBRARY_DIRS}/LLVMGlobalISel.lib
        ${LLVM_LIBRARY_DIRS}/LLVMScalarOpts.lib
        ${LLVM_LIBRARY_DIRS}/LLVMInstCombine.lib
        ${LLVM_LIBRARY_DIRS}/LLVMInstrumentation.lib
        ${LLVM_LIBRARY_DIRS}/LLVMTarget.lib
        ${LLVM_LIBRARY_DIRS}/LLVMFrontendHLSL.lib
        ${LLVM_LIBRARY_DIRS}/LLVMFrontendDirective.lib
        ${LLVM_LIBRARY_DIRS}/clangAnalysisLifetimeSafety.lib
    )

    # Find Windows SDK libraries using CMake's find_library
    # Search in common Windows SDK locations
    set(_win_sdk_search_paths
        "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/x64"
        "C:/Program Files (x86)/Windows Kits/10/Lib/*/ucrt/x64"
    )

    # Find kernel32 as a test to locate the SDK
    find_library(KERNEL32_LIB kernel32 PATHS ${_win_sdk_search_paths} NO_DEFAULT_PATH)

    if(KERNEL32_LIB)
        # Found Windows SDK, get the directory
        get_filename_component(_sdk_lib_dir "${KERNEL32_LIB}" DIRECTORY)
        get_filename_component(_sdk_base_dir "${_sdk_lib_dir}/../.." ABSOLUTE)

        message(STATUS "Found Windows SDK libraries at: ${_sdk_lib_dir}")

        # Add library search paths using LINKER: prefix for proper handling by CMake
        target_link_options(ascii-instr-defer PRIVATE
            "LINKER:/LIBPATH:${_sdk_lib_dir}"
            "LINKER:/LIBPATH:${_sdk_base_dir}/ucrt/x64"
        )

        # Find MSVC runtime libraries (required for C++ runtime with /MD)
        set(_msvc_base_paths
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
        )

        set(_msvc_lib_dir "")
        foreach(_msvc_base IN LISTS _msvc_base_paths)
            if(EXISTS "${_msvc_base}")
                file(GLOB _msvc_versions "${_msvc_base}/*")
                if(_msvc_versions)
                    list(SORT _msvc_versions COMPARE NATURAL ORDER DESCENDING)
                    list(GET _msvc_versions 0 _msvc_latest)
                    set(_msvc_lib_candidate "${_msvc_latest}/lib/x64")
                    if(EXISTS "${_msvc_lib_candidate}")
                        set(_msvc_lib_dir "${_msvc_lib_candidate}")
                        get_filename_component(_msvc_version "${_msvc_latest}" NAME)
                        message(STATUS "Found MSVC runtime at: ${_msvc_lib_dir}")
                        break()
                    endif()
                endif()
            endif()
        endforeach()

        if(_msvc_lib_dir)
            target_link_options(ascii-instr-defer PRIVATE "LINKER:/LIBPATH:${_msvc_lib_dir}")
        else()
            message(WARNING "Could not find MSVC runtime libraries - defer tool linking may fail")
        endif()
    else()
        message(WARNING "Could not find Windows SDK libraries - defer tool may fail to link")
    endif()

    # Now add Windows system libraries
    target_link_libraries(ascii-instr-defer PRIVATE
        ntdll.lib
        version.lib
    )

    # Find ZLIB and zstd dependencies
    # If VCPKG_INSTALLED_DIR is provided (external project build), use it to find packages
    # Otherwise use normal CMake find_package (with vcpkg toolchain)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        # Manual vcpkg package finding without toolchain file
        set(CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        find_package(ZLIB REQUIRED)
        find_package(zstd CONFIG REQUIRED)
    else()
        # Normal package finding (with or without vcpkg toolchain)
        find_package(ZLIB REQUIRED)
        find_package(zstd CONFIG REQUIRED)
    endif()

    # vcpkg provides zstd::libzstd or zstd::libzstd_static
    if(TARGET zstd::libzstd_static)
        set(_zstd_target zstd::libzstd_static)
    elseif(TARGET zstd::libzstd)
        set(_zstd_target zstd::libzstd)
    else()
        message(FATAL_ERROR "Could not find zstd target")
    endif()
    target_link_libraries(ascii-instr-defer PRIVATE
        ZLIB::ZLIB
        ${_zstd_target}
    )
else()
    # Get LLVM libraries from llvm-config
    if(LLVM_CONFIG_EXECUTABLE AND EXISTS "${LLVM_CONFIG_EXECUTABLE}")
        execute_process(
            COMMAND "${LLVM_CONFIG_EXECUTABLE}" --libs
            OUTPUT_VARIABLE LLVM_LIBS_FLAGS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        message(STATUS "LLVM libraries from llvm-config: ${LLVM_LIBS_FLAGS}")
    endif()

    # Get LLVM library directory
    if(LLVM_CONFIG_EXECUTABLE AND EXISTS "${LLVM_CONFIG_EXECUTABLE}")
        execute_process(
            COMMAND "${LLVM_CONFIG_EXECUTABLE}" --libdir
            OUTPUT_VARIABLE LLVM_LIBDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    # Add LLVM library directory
    if(LLVM_LIBDIR)
        target_link_directories(ascii-instr-defer PRIVATE ${LLVM_LIBDIR})
    endif()

    # Find all required Clang static libraries
    # NOTE: On macOS, find_library may prefer .dylib over .a - we explicitly search for static libraries
    # by looking for .a extension directly
    find_library(CLANG_TOOLING_LIB NAMES clangTooling.a clangTooling PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_FRONTEND_LIB NAMES clangFrontend.a clangFrontend PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_DRIVER_LIB NAMES clangDriver.a clangDriver PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_SEMA_LIB NAMES clangSema.a clangSema PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_CODEGEN_LIB NAMES clangCodeGen.a clangCodeGen PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_PARSE_LIB NAMES clangParse.a clangParse PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_AST_LIB NAMES clangAST.a clangAST PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_BASIC_LIB NAMES clangBasic.a clangBasic PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_LEX_LIB NAMES clangLex.a clangLex PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_REWRITE_LIB NAMES clangRewrite.a clangRewrite PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_REWRITEFRONTEND_LIB NAMES clangRewriteFrontend.a clangRewriteFrontend PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_SERIALIZATION_LIB NAMES clangSerialization.a clangSerialization PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_EDIT_LIB NAMES clangEdit.a clangEdit PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_ANALYSIS_LIB NAMES clangAnalysis.a clangAnalysis PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_ANALYSIS_LIFETIME_SAFETY_LIB NAMES clangAnalysisLifetimeSafety.a clangAnalysisLifetimeSafety PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH QUIET)
    find_library(CLANG_ASTMATCHERS_LIB NAMES clangASTMatchers.a clangASTMatchers PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_APINOTES_LIB NAMES clangAPINotes.a clangAPINotes PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_SUPPORT_LIB NAMES clangSupport.a clangSupport PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(CLANG_DAEMON_LIB NAMES clangDaemon.a clangDaemon PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH QUIET)
    find_library(CLANG_DAEMON_TWEAKS_LIB NAMES clangDaemonTweaks.a clangDaemonTweaks PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH QUIET)
    find_library(CLANG_OPTIONS_LIB NAMES clangOptions.a clangOptions PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH QUIET)
    find_library(CLANG_TOOLING_CORE_LIB NAMES clangToolingCore.a clangToolingCore PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)

    if(NOT CLANG_TOOLING_LIB)
        message(FATAL_ERROR "Could not find Clang libraries. Please install clang-dev or llvm-dev package.")
    endif()

    # Build list of clang libraries to link, including only those that were found
    set(_clang_libs
        ${CLANG_TOOLING_LIB}
        ${CLANG_TOOLING_CORE_LIB}
        ${CLANG_FRONTEND_LIB}
        ${CLANG_DRIVER_LIB}
        ${CLANG_SERIALIZATION_LIB}
        ${CLANG_CODEGEN_LIB}
        ${CLANG_REWRITEFRONTEND_LIB}
        ${CLANG_REWRITE_LIB}
        ${CLANG_PARSE_LIB}
        ${CLANG_SEMA_LIB}
        ${CLANG_ANALYSIS_LIB}
        ${CLANG_ASTMATCHERS_LIB}
        ${CLANG_AST_LIB}
        ${CLANG_EDIT_LIB}
        ${CLANG_LEX_LIB}
        ${CLANG_BASIC_LIB}
        ${CLANG_APINOTES_LIB}
        ${CLANG_SUPPORT_LIB}
        ${LLVM_LIBS_FLAGS}
    )
    # Add optional libraries only if they were found
    if(CLANG_ANALYSIS_LIFETIME_SAFETY_LIB)
        list(APPEND _clang_libs ${CLANG_ANALYSIS_LIFETIME_SAFETY_LIB})
    endif()
    if(CLANG_OPTIONS_LIB)
        list(APPEND _clang_libs ${CLANG_OPTIONS_LIB})
    endif()
    if(CLANG_DAEMON_LIB)
        list(APPEND _clang_libs ${CLANG_DAEMON_LIB})
    endif()
    if(CLANG_DAEMON_TWEAKS_LIB)
        list(APPEND _clang_libs ${CLANG_DAEMON_TWEAKS_LIB})
    endif()

    # Use linker group to handle complex interdependencies between Clang static libraries
    # This is necessary because libraries like clangAnalysis depend on clangASTMatchers,
    # clangParse depends on clangSema, and clangAPINotes depends on clangFrontend
    # The group allows the linker to iterate until all symbols are resolved
    # Note: We embed --start-group/--end-group directly in target_link_libraries
    # because target_link_options places flags before libraries, not around them
    if(NOT APPLE)
        # Linux: Use linker group to handle circular dependencies between Clang and LLVM libraries
        target_link_libraries(ascii-instr-defer PRIVATE
            -Wl,--start-group
            ${_clang_libs}
            -Wl,--end-group
        )
    else()
        # macOS: ld64 handles circular dependencies automatically
        # Link Clang libraries + LLVM libraries by name (will be found via target_link_directories)
        target_link_libraries(ascii-instr-defer PRIVATE
            ${_clang_libs}
        )
    endif()

    # Find and link required system libraries
    find_library(TINFO_LIBRARY NAMES tinfo ncurses)
    if(TINFO_LIBRARY)
        target_link_libraries(ascii-instr-defer PRIVATE ${TINFO_LIBRARY})
    endif()

    # Find zlib (required by LLVM for compression functions: compress2, uncompress, crc32)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_link_libraries(ascii-instr-defer PRIVATE ZLIB::ZLIB)
    else()
        # Fallback: try to find zlib directly
        find_library(ZLIB_LIBRARY NAMES z zlib QUIET)
        if(ZLIB_LIBRARY)
            target_link_libraries(ascii-instr-defer PRIVATE ${ZLIB_LIBRARY})
        else()
            message(STATUS "zlib not found - may not be needed for this LLVM version")
        endif()
    endif()

    # Find zstd library (required by LLVM for compression support)
    find_package(zstd QUIET)
    if(zstd_FOUND AND TARGET zstd::libzstd)
        target_link_libraries(ascii-instr-defer PRIVATE zstd::libzstd)
    else()
        # Fallback: try to find zstd library directly
        find_library(ZSTD_LIBRARY NAMES zstd QUIET)
        if(ZSTD_LIBRARY)
            target_link_libraries(ascii-instr-defer PRIVATE ${ZSTD_LIBRARY})
        else()
            message(STATUS "zstd library not found - LLVM compression may not work, but linking will attempt to proceed")
        endif()
    endif()

    # Add standard library support (needed when overriding musl flags)
    # Only on Linux - macOS uses libc++ which is linked via -stdlib=libc++
    if(NOT APPLE)
        target_link_libraries(ascii-instr-defer PRIVATE
            stdc++
            stdc++fs
            gcc_s
        )
    endif()
endif()

# Set output directory to match parent build
if(DEFINED OUTPUT_DIR)
    set_target_properties(ascii-instr-defer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    )
endif()

# NOTE: No install() command - this is a build-time tool only, not distributed
