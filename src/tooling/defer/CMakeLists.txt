cmake_minimum_required(VERSION 3.20)

# On Windows, prevent CMake from running compiler tests that would add problematic flags
if(WIN32)
    set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
    set(CMAKE_C_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
endif()

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

# Use llvm-config to get LLVM/Clang configuration
find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config llvm-config.exe
    DOC "Path to llvm-config"
    REQUIRED
)

execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
    OUTPUT_VARIABLE LLVM_INCLUDE_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
    OUTPUT_VARIABLE LLVM_LIBRARY_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --cxxflags
    OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)

# Remove flags we don't want from llvm-config output
string(REPLACE "-std=c++17" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
string(REPLACE "-std=c++14" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")

# Create the executable
add_executable(ascii-instr-defer tool.cpp)

# Add LLVM/Clang include directories
target_include_directories(ascii-instr-defer SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

# Compile options - keep it simple, no sanitizers or LTO for build tools
target_compile_options(ascii-instr-defer PRIVATE
    -O2
    -fno-rtti
    -fexceptions
)

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
    # Linux/macOS: Use shared Clang/LLVM libraries
    find_library(CLANG_CPP_SHARED_LIB
        NAMES clang-cpp
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_library(LLVM_SHARED_LIB
        NAMES LLVM LLVM-18
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
        REQUIRED
    )

    target_link_libraries(ascii-instr-defer PRIVATE
        ${CLANG_CPP_SHARED_LIB}
        ${LLVM_SHARED_LIB}
    )

    # Add ncurses/tinfo which LLVM needs
    find_library(TINFO_LIBRARY NAMES tinfo ncurses)
    if(TINFO_LIBRARY)
        target_link_libraries(ascii-instr-defer PRIVATE ${TINFO_LIBRARY})
    endif()

    # Add standard library support (needed when overriding musl flags)
    target_link_libraries(ascii-instr-defer PRIVATE
        stdc++
        stdc++fs
        gcc_s
    )
endif()

# Set output directory to match parent build
if(DEFINED OUTPUT_DIR)
    set_target_properties(ascii-instr-defer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    )
endif()

install(TARGETS ascii-instr-defer
    RUNTIME DESTINATION bin
)
