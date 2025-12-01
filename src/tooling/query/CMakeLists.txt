cmake_minimum_required(VERSION 3.20)

# On Windows, prevent CMake from running compiler tests that would add problematic flags
if(WIN32)
    set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
    set(CMAKE_C_COMPILER_WORKS TRUE CACHE BOOL "Skip compiler test" FORCE)
endif()

# Note: On macOS, the parent build (cmake/tooling/Query.cmake) sets CMAKE_CXX_COMPILER
# to Apple's system clang (/usr/bin/clang++) BEFORE invoking this ExternalProject.
# This is necessary because Homebrew LLVM's bundled libc++ headers are incompatible
# with manual header path configuration (-nostdinc/-nostdinc++). We use Apple's clang
# for compilation but link against LLDB libraries from Homebrew LLVM.

project(ascii-query-server CXX)

# Override CMake's Windows-Clang platform settings AFTER project() to prevent -nostartfiles -nostdlib
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_CXX_COMPILER> <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
    set(CMAKE_CXX_CREATE_SHARED_MODULE "${CMAKE_CXX_CREATE_SHARED_LIBRARY}")
    set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

    set(CMAKE_MSVC_RUNTIME_LIBRARY "")
    set(CMAKE_CXX_STANDARD_LIBRARIES "")
    set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "")
    set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")

    message(STATUS "Overrode CMake Windows-Clang linking rules for query tool to prevent -nostartfiles -nostdlib")
endif()

# This is a standalone project for the query debug tool
# It must NOT inherit flags from the main ascii-chat build (musl, static-pie, etc.)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use llvm-config to find LLDB (LLDB is part of the LLVM project)
find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config llvm-config.exe
    HINTS
        /opt/homebrew/opt/llvm/bin      # Homebrew on Apple Silicon
        /usr/local/opt/llvm/bin         # Homebrew on Intel Mac
        /usr/lib/llvm-21/bin            # Debian/Ubuntu LLVM 21
        /usr/lib/llvm-20/bin            # Debian/Ubuntu LLVM 20
        /usr/lib/llvm-19/bin            # Debian/Ubuntu LLVM 19
        /usr/lib/llvm-18/bin            # Debian/Ubuntu LLVM 18
        /usr/lib/llvm-17/bin            # Debian/Ubuntu LLVM 17
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
add_executable(ascii-query-server
    main.cpp
    lldb_controller.cpp
    http_server.cpp
)

# Platform-specific include and compile setup
if(APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(MACOS_SDK_PATH)
        message(STATUS "macOS SDK: ${MACOS_SDK_PATH}")
        message(STATUS "macOS: Using Apple system clang for compilation, Homebrew LLVM/LLDB for libraries")
        message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER}")
        message(STATUS "  LLVM libraries: ${LLVM_LIBRARY_DIRS}")
        message(STATUS "  LLVM headers: ${LLVM_INCLUDE_DIRS}")

        target_include_directories(ascii-query-server SYSTEM PRIVATE
            ${LLVM_INCLUDE_DIRS}
        )

        target_compile_options(ascii-query-server PRIVATE
            -O2
            -fno-rtti
            -fexceptions
            -isysroot ${MACOS_SDK_PATH}
            -stdlib=libc++
        )

        target_link_directories(ascii-query-server PRIVATE
            ${LLVM_LIBRARY_DIRS}
        )
        target_link_options(ascii-query-server PRIVATE
            -isysroot ${MACOS_SDK_PATH}
            -stdlib=libc++
            "-L${LLVM_LIBRARY_DIRS}"
            "-Wl,-rpath,${LLVM_LIBRARY_DIRS}"
        )
    else()
        message(WARNING "Could not detect macOS SDK path - query tool may fail to build")
        target_include_directories(ascii-query-server SYSTEM PRIVATE
            ${LLVM_INCLUDE_DIRS}
        )
        target_compile_options(ascii-query-server PRIVATE
            -O2
            -fno-rtti
            -fexceptions
        )
    endif()
else()
    # Non-macOS: use SYSTEM includes (Linux, Windows)
    target_include_directories(ascii-query-server SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
    )

    target_compile_options(ascii-query-server PRIVATE
        -O2
        -fno-rtti
        -fexceptions
    )
endif()

# On Windows, use dynamic runtime (/MD) to match LLVM libraries
if(WIN32)
    target_compile_options(ascii-query-server PRIVATE
        $<$<CONFIG:Debug>:-D_DLL -D_MT>
        $<$<CONFIG:Release>:-D_DLL -D_MT>
    )
    target_link_options(ascii-query-server PRIVATE
        $<$<CONFIG:Debug>:-Xclang --dependent-lib=msvcrtd>
        $<$<CONFIG:Release>:-Xclang --dependent-lib=msvcrt>
    )
endif()

# Platform-specific linking
if(WIN32)
    # Windows: Link LLDB static library
    # LLDB on Windows typically provides liblldb.lib
    find_library(LLDB_LIB
        NAMES lldb liblldb
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )

    if(NOT LLDB_LIB)
        message(FATAL_ERROR "Could not find LLDB library in ${LLVM_LIBRARY_DIRS}")
    endif()

    target_compile_definitions(ascii-query-server PRIVATE
        LLVM_BUILD_STATIC
    )

    # LLVMSupport is required for llvm::cl:: command-line parsing
    target_link_libraries(ascii-query-server PRIVATE
        ${LLDB_LIB}
        ${LLVM_LIBRARY_DIRS}/LLVMSupport.lib
        ${LLVM_LIBRARY_DIRS}/LLVMCore.lib
        ${LLVM_LIBRARY_DIRS}/LLVMDemangle.lib
        ${LLVM_LIBRARY_DIRS}/LLVMTargetParser.lib
    )

    # Windows SDK libraries
    set(_win_sdk_search_paths
        "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/x64"
        "C:/Program Files (x86)/Windows Kits/10/Lib/*/ucrt/x64"
    )

    find_library(KERNEL32_LIB kernel32 PATHS ${_win_sdk_search_paths} NO_DEFAULT_PATH)

    if(KERNEL32_LIB)
        get_filename_component(_sdk_lib_dir "${KERNEL32_LIB}" DIRECTORY)
        get_filename_component(_sdk_base_dir "${_sdk_lib_dir}/../.." ABSOLUTE)

        message(STATUS "Found Windows SDK libraries at: ${_sdk_lib_dir}")

        target_link_options(ascii-query-server PRIVATE
            "LINKER:/LIBPATH:${_sdk_lib_dir}"
            "LINKER:/LIBPATH:${_sdk_base_dir}/ucrt/x64"
        )

        # Find MSVC runtime
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
            target_link_options(ascii-query-server PRIVATE "LINKER:/LIBPATH:${_msvc_lib_dir}")
        else()
            message(WARNING "Could not find MSVC runtime libraries - query tool linking may fail")
        endif()
    else()
        message(WARNING "Could not find Windows SDK libraries - query tool may fail to link")
    endif()

    target_link_libraries(ascii-query-server PRIVATE
        ntdll.lib
        version.lib
        ws2_32.lib  # For HTTP server socket support
    )

else()
    # Linux/macOS: Use LLDB shared library
    find_library(LLDB_SHARED_LIB
        NAMES lldb liblldb
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )

    find_library(LLVM_SHARED_LIB
        NAMES LLVM LLVM-18 LLVM-19 LLVM-20 LLVM-21 LLVM-22
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )

    if(NOT LLDB_SHARED_LIB)
        message(FATAL_ERROR "Could not find LLDB library. Please install lldb-dev or lldb package.")
    endif()

    message(STATUS "Found LLDB library: ${LLDB_SHARED_LIB}")

    target_link_libraries(ascii-query-server PRIVATE
        ${LLDB_SHARED_LIB}
    )

    # LLVM shared library if found (may not be needed if LLDB links it)
    if(LLVM_SHARED_LIB)
        target_link_libraries(ascii-query-server PRIVATE
            ${LLVM_SHARED_LIB}
        )
    endif()

    # Add ncurses/tinfo which LLVM needs
    find_library(TINFO_LIBRARY NAMES tinfo ncurses)
    if(TINFO_LIBRARY)
        target_link_libraries(ascii-query-server PRIVATE ${TINFO_LIBRARY})
    endif()

    # Standard library support on Linux
    if(NOT APPLE)
        target_link_libraries(ascii-query-server PRIVATE
            stdc++
            pthread
        )
    endif()
endif()

# Set output directory to match parent build
if(DEFINED OUTPUT_DIR)
    set_target_properties(ascii-query-server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    )
endif()

install(TARGETS ascii-query-server
    RUNTIME DESTINATION bin
)
