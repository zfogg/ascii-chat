cmake_minimum_required(VERSION 3.20)

# On Windows, prevent CMake from running compiler tests that would add problematic flags
if(WIN32)
    set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
    set(CMAKE_C_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
endif()

project(ascii-instr-panic CXX)

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

    message(STATUS "Overrode CMake Windows-Clang linking rules for panic tool to prevent -nostartfiles -nostdlib")
endif()

# This is a standalone project for the panic instrumentation tool
# It must NOT inherit flags from the main ascii-chat build (musl, static-pie, etc.)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use llvm-config to get LLVM/Clang configuration
# On macOS with Homebrew, llvm-config is typically not in PATH
find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config llvm-config.exe
    HINTS
        /opt/homebrew/opt/llvm/bin    # Homebrew on Apple Silicon
        /usr/local/opt/llvm/bin        # Homebrew on Intel Mac
        /usr/lib/llvm-21/bin           # Debian/Ubuntu LLVM 21
        /usr/lib/llvm-20/bin           # Debian/Ubuntu LLVM 20
        /usr/lib/llvm-19/bin           # Debian/Ubuntu LLVM 19
        /usr/lib/llvm-18/bin           # Debian/Ubuntu LLVM 18
    DOC "Path to llvm-config"
    REQUIRED
)

execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
    OUTPUT_VARIABLE LLVM_INCLUDE_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
    OUTPUT_VARIABLE LLVM_LIBRARY_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
    OUTPUT_VARIABLE LLVM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)

message(STATUS "LLVM version: ${LLVM_VERSION}")
message(STATUS "LLVM include: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM lib: ${LLVM_LIBRARY_DIRS}")

# Create the executable
add_executable(ascii-instr-panic tool.cpp)

# Platform-specific include and compile setup
if(APPLE)
    # On macOS with Homebrew LLVM, we need special handling for include paths.
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(MACOS_SDK_PATH)
        message(STATUS "Using macOS SDK: ${MACOS_SDK_PATH}")

        # Get Clang's resource directory (contains compiler builtins like stddef.h)
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
            OUTPUT_VARIABLE CLANG_RESOURCE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        # Check if LLVM has bundled libc++ (common in Homebrew LLVM 18+)
        set(LLVM_LIBCXX_INCLUDE "${LLVM_LIBRARY_DIRS}/../include/c++/v1")
        get_filename_component(LLVM_LIBCXX_INCLUDE "${LLVM_LIBCXX_INCLUDE}" ABSOLUTE)
        if(NOT EXISTS "${LLVM_LIBCXX_INCLUDE}")
            set(LLVM_LIBCXX_INCLUDE "${LLVM_INCLUDE_DIRS}/c++/v1")
        endif()

        if(EXISTS "${LLVM_LIBCXX_INCLUDE}")
            message(STATUS "Found LLVM libc++ headers: ${LLVM_LIBCXX_INCLUDE}")
            set(HAS_LLVM_LIBCXX TRUE)
        else()
            message(STATUS "LLVM libc++ headers not found, using system libc++")
            set(HAS_LLVM_LIBCXX FALSE)
        endif()

        # Compile options for macOS with Homebrew LLVM
        # The challenge: Homebrew LLVM 21+ bundles its own libc++ which uses
        # #include_next to find C library headers from the SDK. SDK headers
        # expect size_t/ptrdiff_t to be defined from clang's stddef.h.
        #
        # Root cause: When libc++ does #include_next <assert.h>, it finds SDK's
        # assert.h which includes arm/_types.h expecting size_t/ptrdiff_t to
        # already be defined. These types come from clang's stddef.h which must
        # be included BEFORE SDK headers.
        #
        # Attempt 13: Add clang resource dir BEFORE -isysroot to ensure
        # compiler builtins (stddef.h with size_t/ptrdiff_t) are found first.
        # Use -nostdinc++ and -cxx-isystem for C++ paths.
        if(HAS_LLVM_LIBCXX)
            message(STATUS "Using Homebrew LLVM with bundled libc++ (Attempt 13: clang builtins first)")
            message(STATUS "  1. Clang builtins (-isystem): ${CLANG_RESOURCE_DIR}/include")
            message(STATUS "  2. SDK via -isysroot: ${MACOS_SDK_PATH}")
            message(STATUS "  3. C++ (via -cxx-isystem): libc++ and LLVM tooling")

            target_compile_options(ascii-instr-panic PRIVATE
                -O2
                -fno-rtti
                -fexceptions
                # CRITICAL: Clang resource dir FIRST with -isystem (highest priority)
                # This ensures stddef.h with size_t/ptrdiff_t is found before SDK headers
                "-isystem${CLANG_RESOURCE_DIR}/include"
                # SDK for C headers (after clang builtins)
                "-isysroot${MACOS_SDK_PATH}"
                # Disable C++ standard library search
                -nostdinc++
                # Add C++ includes explicitly
                "-cxx-isystem${LLVM_LIBCXX_INCLUDE}"
                "-cxx-isystem${LLVM_INCLUDE_DIRS}"
            )
        else()
            # Without LLVM libc++, add LLVM headers as regular includes
            target_include_directories(ascii-instr-panic PRIVATE
                ${LLVM_INCLUDE_DIRS}
            )
            # Without LLVM libc++, use standard flags
            target_compile_options(ascii-instr-panic PRIVATE
                -O2
                -fno-rtti
                -fexceptions
                -isysroot ${MACOS_SDK_PATH}
                -stdlib=libc++
            )

            # Add clang resource dir if available
            if(CLANG_RESOURCE_DIR AND EXISTS "${CLANG_RESOURCE_DIR}/include")
                message(STATUS "Using Clang resource dir: ${CLANG_RESOURCE_DIR}/include")
                target_compile_options(ascii-instr-panic PRIVATE
                    "-isystem${CLANG_RESOURCE_DIR}/include"
                )
            endif()
        endif()

        # Link options for macOS
        target_link_directories(ascii-instr-panic PRIVATE
            ${LLVM_LIBRARY_DIRS}/c++
            ${LLVM_LIBRARY_DIRS}/unwind
        )
        target_link_options(ascii-instr-panic PRIVATE
            -isysroot ${MACOS_SDK_PATH}
            -stdlib=libc++
            "-L${LLVM_LIBRARY_DIRS}/c++"
            "-L${LLVM_LIBRARY_DIRS}/unwind"
            "-Wl,-rpath,${LLVM_LIBRARY_DIRS}/c++"
            "-Wl,-rpath,${LLVM_LIBRARY_DIRS}/unwind"
        )
        target_link_libraries(ascii-instr-panic PRIVATE c++ c++abi)
    else()
        message(WARNING "Could not detect macOS SDK path - panic tool may fail to build")
        # Fallback: use SYSTEM includes
        target_include_directories(ascii-instr-panic SYSTEM PRIVATE
            ${LLVM_INCLUDE_DIRS}
        )
        target_compile_options(ascii-instr-panic PRIVATE
            -O2
            -fno-rtti
            -fexceptions
        )
    endif()
else()
    # Non-macOS: use SYSTEM includes (Linux, Windows)
    target_include_directories(ascii-instr-panic SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
    )

    # Compile options - keep it simple, no sanitizers or LTO for build tools
    target_compile_options(ascii-instr-panic PRIVATE
        -O2
        -fno-rtti
        -fexceptions
    )
endif()

# On Windows, use dynamic runtime (/MD) to match Clang libraries
if(WIN32)
    target_compile_options(ascii-instr-panic PRIVATE
        $<$<CONFIG:Debug>:-D_DLL -D_MT>
        $<$<CONFIG:Release>:-D_DLL -D_MT>
    )
    target_link_options(ascii-instr-panic PRIVATE
        $<$<CONFIG:Debug>:-Xclang --dependent-lib=msvcrtd>
        $<$<CONFIG:Release>:-Xclang --dependent-lib=msvcrt>
    )
endif()

# Platform-specific linking
if(WIN32)
    # Windows: Link static Clang libraries (no clangCodeGen - not needed for AST rewriting)
    set(CLANG_LIBS
        clangTooling clangFrontend clangDriver clangOptions
        clangAST clangASTMatchers clangBasic
        clangRewrite clangRewriteFrontend clangLex clangSerialization
        clangParse clangSema clangEdit clangAnalysis
        clangAnalysisLifetimeSafety
        clangAPINotes clangSupport
    )

    target_compile_definitions(ascii-instr-panic PRIVATE
        LLVM_BUILD_STATIC
        CLANG_BUILD_STATIC
    )

    target_link_libraries(ascii-instr-panic PRIVATE
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
        ${LLVM_LIBRARY_DIRS}/LLVMFrontendDirective.lib
        ${LLVM_LIBRARY_DIRS}/LLVMFrontendHLSL.lib
        ${LLVM_LIBRARY_DIRS}/LLVMScalarOpts.lib
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
    )

    # Find Windows SDK libraries
    set(_win_sdk_search_paths
        "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/x64"
        "C:/Program Files (x86)/Windows Kits/10/Lib/*/ucrt/x64"
    )

    find_library(KERNEL32_LIB kernel32 PATHS ${_win_sdk_search_paths} NO_DEFAULT_PATH)

    if(KERNEL32_LIB)
        get_filename_component(_sdk_lib_dir "${KERNEL32_LIB}" DIRECTORY)
        get_filename_component(_sdk_base_dir "${_sdk_lib_dir}/../.." ABSOLUTE)

        message(STATUS "Found Windows SDK libraries at: ${_sdk_lib_dir}")

        target_link_options(ascii-instr-panic PRIVATE
            "LINKER:/LIBPATH:${_sdk_lib_dir}"
            "LINKER:/LIBPATH:${_sdk_base_dir}/ucrt/x64"
        )

        # Find MSVC runtime libraries
        set(_msvc_base_paths
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
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
                        message(STATUS "Found MSVC runtime at: ${_msvc_lib_dir}")
                        break()
                    endif()
                endif()
            endif()
        endforeach()

        if(_msvc_lib_dir)
            target_link_options(ascii-instr-panic PRIVATE "LINKER:/LIBPATH:${_msvc_lib_dir}")
        else()
            message(WARNING "Could not find MSVC runtime libraries - panic tool linking may fail")
        endif()
    else()
        message(WARNING "Could not find Windows SDK libraries - panic tool may fail to link")
    endif()

    # Windows system libraries
    target_link_libraries(ascii-instr-panic PRIVATE
        ntdll.lib
        version.lib
    )

    # Find ZLIB and zstd dependencies
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        find_package(ZLIB REQUIRED)
        find_package(zstd CONFIG REQUIRED)
    else()
        find_package(ZLIB REQUIRED)
        find_package(zstd CONFIG REQUIRED)
    endif()

    if(TARGET zstd::libzstd_static)
        set(_zstd_target zstd::libzstd_static)
    elseif(TARGET zstd::libzstd)
        set(_zstd_target zstd::libzstd)
    else()
        message(FATAL_ERROR "Could not find zstd target")
    endif()
    target_link_libraries(ascii-instr-panic PRIVATE
        ZLIB::ZLIB
        ${_zstd_target}
    )
else()
    # Linux/macOS: Use shared Clang/LLVM libraries
    find_library(CLANG_CPP_SHARED_LIB
        NAMES clang-cpp libclang-cpp
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )

    find_library(LLVM_SHARED_LIB
        NAMES LLVM LLVM-18 LLVM-19 LLVM-20 LLVM-21 LLVM-22
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )

    if(NOT CLANG_CPP_SHARED_LIB)
        message(STATUS "clang-cpp not found, trying individual Clang libraries")

        find_library(CLANG_TOOLING_LIB NAMES clangTooling PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_FRONTEND_LIB NAMES clangFrontend PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_DRIVER_LIB NAMES clangDriver PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_SEMA_LIB NAMES clangSema PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_CODEGEN_LIB NAMES clangCodeGen PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_PARSE_LIB NAMES clangParse PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_AST_LIB NAMES clangAST PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_BASIC_LIB NAMES clangBasic PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_LEX_LIB NAMES clangLex PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_REWRITE_LIB NAMES clangRewrite PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_REWRITEFRONTEND_LIB NAMES clangRewriteFrontend PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_SERIALIZATION_LIB NAMES clangSerialization PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_EDIT_LIB NAMES clangEdit PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_ANALYSIS_LIB NAMES clangAnalysis PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_ASTMATCHERS_LIB NAMES clangASTMatchers PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_APINOTES_LIB NAMES clangAPINotes PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)
        find_library(CLANG_SUPPORT_LIB NAMES clangSupport PATHS ${LLVM_LIBRARY_DIRS} NO_DEFAULT_PATH)

        if(NOT CLANG_TOOLING_LIB OR NOT LLVM_SHARED_LIB)
            message(FATAL_ERROR "Could not find Clang libraries. Please install clang-dev or llvm-dev package.")
        endif()

        if(NOT APPLE)
            target_link_libraries(ascii-instr-panic PRIVATE
                -Wl,--start-group
                ${CLANG_TOOLING_LIB}
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
                -Wl,--end-group
                ${LLVM_SHARED_LIB}
            )
        else()
            # macOS: ld64 handles circular dependencies automatically
            target_link_libraries(ascii-instr-panic PRIVATE
                ${CLANG_TOOLING_LIB}
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
                ${LLVM_SHARED_LIB}
            )
        endif()
    else()
        target_link_libraries(ascii-instr-panic PRIVATE
            ${CLANG_CPP_SHARED_LIB}
            ${LLVM_SHARED_LIB}
        )
    endif()

    # Add ncurses/tinfo which LLVM needs
    find_library(TINFO_LIBRARY NAMES tinfo ncurses)
    if(TINFO_LIBRARY)
        target_link_libraries(ascii-instr-panic PRIVATE ${TINFO_LIBRARY})
    endif()

    # Add standard library support (needed when overriding musl flags)
    if(NOT APPLE)
        target_link_libraries(ascii-instr-panic PRIVATE
            stdc++
            stdc++fs
            gcc_s
        )
    endif()
endif()

# Set output directory to match parent build
if(DEFINED OUTPUT_DIR)
    set_target_properties(ascii-instr-panic PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    )
endif()

install(TARGETS ascii-instr-panic
    RUNTIME DESTINATION bin
)
