# Guard against multiple inclusions
if(DEFINED ASCII_DEBUG_TARGETS_CMAKE_INCLUDED)
    return()
endif()
set(ASCII_DEBUG_TARGETS_CMAKE_INCLUDED TRUE)

function(ascii_add_tooling_targets)
    # Guard against multiple calls to this function
    if(TARGET ascii-debug-runtime)
        return()
    endif()

    # IMPORTANT: Tooling executables run on BUILD system, not TARGET system
    # They must NOT inherit musl/cross-compile flags

    # Save global CMAKE flags to restore later
    set(_SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(_SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(_SAVED_CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

    # Temporarily clear musl flags for tooling target creation
    # NOTE: These get baked into targets at creation time
    if(USE_MUSL)
        set(CMAKE_C_FLAGS "")
        set(CMAKE_CXX_FLAGS "")
        set(CMAKE_EXE_LINKER_FLAGS "")
    endif()

    if(NOT CMAKE_CXX_COMPILER_LOADED)
        if(NOT CMAKE_CXX_COMPILER)
            get_filename_component(_clang_dir "${CMAKE_C_COMPILER}" DIRECTORY)
            find_program(_clangxx NAMES clang++ PATHS "${_clang_dir}" NO_DEFAULT_PATH)
            if(NOT _clangxx)
                find_program(_clangxx NAMES clang++)
            endif()
            if(_clangxx)
                set(CMAKE_CXX_COMPILER "${_clangxx}" CACHE FILEPATH "C++ compiler for source_print instrumentation" FORCE)
            endif()
        endif()
        enable_language(CXX)
    endif()

    # Use llvm-config to get includes and libraries directly
    # This bypasses LLVM's broken CMake config that requires all dependency targets
    find_program(LLVM_CONFIG_EXECUTABLE
        NAMES llvm-config llvm-config.exe
        DOC "Path to llvm-config"
    )

    if(NOT LLVM_CONFIG_EXECUTABLE)
        message(FATAL_ERROR "llvm-config not found")
    endif()

    # Get LLVM configuration from llvm-config
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
        OUTPUT_VARIABLE LLVM_INCLUDE_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
        OUTPUT_VARIABLE LLVM_LIBRARY_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --cxxflags
        OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
        OUTPUT_VARIABLE LLVM_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --system-libs
        OUTPUT_VARIABLE LLVM_SYSTEM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
        OUTPUT_VARIABLE LLVM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs
        OUTPUT_VARIABLE LLVM_CONFIG_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)

    # Convert to list and extract library names
    string(REPLACE " " ";" LLVM_LIB_LIST "${LLVM_CONFIG_LIBS}")

    message(STATUS "LLVM version: ${LLVM_VERSION}")
    message(STATUS "LLVM include dir: ${LLVM_INCLUDE_DIRS}")
    message(STATUS "LLVM library dir: ${LLVM_LIBRARY_DIRS}")

    # Find Clang includes (next to LLVM)
    set(CLANG_INCLUDE_DIRS "${LLVM_INCLUDE_DIRS}")

    # Set LLVM_CONFIG for compatibility with existing code
    set(LLVM_CONFIG "${LLVM_CONFIG_EXECUTABLE}")

    find_package(Threads REQUIRED)

    # Find system libraries that LLVM/Clang depend on
    find_library(ZLIB_LIBRARY NAMES z zlib)
    if(NOT ZLIB_LIBRARY)
        message(FATAL_ERROR "zlib not found - required for LLVM compression support")
    endif()

    find_library(ZSTD_LIBRARY NAMES zstd libzstd)
    if(NOT ZSTD_LIBRARY)
        message(FATAL_ERROR "zstd not found - required for LLVM compression support")
    endif()

    # Find ncurses/tinfo for terminal support (Unix only, optional)
    find_library(NCURSES_LIBRARY NAMES ncurses tinfo)

    # Find Clang libraries manually (avoid broken CMake config)
    set(CLANG_LIBS
        clangTooling clangFrontend clangAST clangASTMatchers clangBasic
        clangRewrite clangRewriteFrontend clangLex clangSerialization
        clangDriver clangParse clangSema clangEdit clangAnalysis
        clangAPINotes clangSupport
    )

    foreach(lib ${CLANG_LIBS})
        find_library(${lib}_LIBRARY
            NAMES ${lib}
            PATHS ${LLVM_LIBRARY_DIRS}
            NO_DEFAULT_PATH
        )
        if(${lib}_LIBRARY)
            add_library(${lib} UNKNOWN IMPORTED)
            set_target_properties(${lib} PROPERTIES
                IMPORTED_LOCATION "${${lib}_LIBRARY}"
            )
        else()
            message(WARNING "Clang library not found: ${lib}")
        endif()
    endforeach()

    # Find clang-cpp monolithic library if available
    find_library(clang-cpp_LIBRARY
        NAMES clang-cpp
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )
    if(clang-cpp_LIBRARY)
        add_library(clang-cpp UNKNOWN IMPORTED)
        set_target_properties(clang-cpp PROPERTIES
            IMPORTED_LOCATION "${clang-cpp_LIBRARY}"
        )
    endif()

    # Create LLVMSupport target
    find_library(LLVMSupport_LIBRARY
        NAMES LLVMSupport
        PATHS ${LLVM_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )
    if(LLVMSupport_LIBRARY)
        add_library(LLVMSupport UNKNOWN IMPORTED)
        set_target_properties(LLVMSupport PROPERTIES
            IMPORTED_LOCATION "${LLVMSupport_LIBRARY}"
        )
    endif()

    if(NOT TARGET ascii-instr-source-print)
        add_executable(ascii-instr-source-print EXCLUDE_FROM_ALL
            src/tooling/source_print/tool.cpp
        )
    endif()

    # Use SYSTEM to suppress warnings from LLVM/Clang headers
    target_include_directories(ascii-instr-source-print SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
    )

    target_compile_features(ascii-instr-source-print PRIVATE cxx_std_20)

    # Build ascii-instr-source-print without sanitizers and with Release runtime to match LLVM libraries
    # LLVM is built with -fno-rtti, so we must match that
    # Use -O0 to prevent optimizer from stripping static initializers for command line options
    # Also disable LTO since LLVM libraries are not built with LTO
    target_compile_options(ascii-instr-source-print PRIVATE
        -O0
        -g
        -D_ITERATOR_DEBUG_LEVEL=0
        -fno-sanitize=all
        -fno-rtti
        -fexceptions
        -fno-lto
    )

    # Remove debug/sanitizer linker flags and LTO
    target_link_options(ascii-instr-source-print PRIVATE
        -fno-sanitize=all
        -fno-lto
    )

    # Note: C++ standard library is linked automatically by clang++
    # On macOS with Homebrew LLVM, we may need explicit libc++
    # On Linux, the system libstdc++ is used by default
    if(APPLE)
        if(EXISTS "/usr/local/lib/libc++.a" AND EXISTS "/usr/local/lib/libc++abi.a")
            target_link_libraries(ascii-instr-source-print PRIVATE "/usr/local/lib/libc++.a" "/usr/local/lib/libc++abi.a")
        elseif(EXISTS "/opt/homebrew/opt/llvm/lib/c++/libc++.a")
            target_link_libraries(ascii-instr-source-print PRIVATE
                "/opt/homebrew/opt/llvm/lib/c++/libc++.a"
                "/opt/homebrew/opt/llvm/lib/c++/libc++abi.a"
            )
        endif()
    endif()

    # Override global source print runtime settings for this target specifically
    # Use MultiThreadedDLL (MD) instead of MultiThreadedDebugDLL (MDd) to match LLVM build
    # Disable IPO/LTO since LLVM libraries are not built with LTO
    set_target_properties(ascii-instr-source-print PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
        INTERPROCEDURAL_OPTIMIZATION OFF
        # Override CMAKE_EXE_LINKER_FLAGS that has musl flags
        LINK_FLAGS ""
    )

    # =========================================================================
    # Defer Transformation Tool
    # =========================================================================
    if(NOT TARGET ascii-instr-defer)
        add_executable(ascii-instr-defer
            src/tooling/defer/tool.cpp
        )
    endif()

    # Use SYSTEM to suppress warnings from LLVM/Clang headers
    target_include_directories(ascii-instr-defer SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
    )

    target_compile_features(ascii-instr-defer PRIVATE cxx_std_20)

    # Build ascii-instr-defer for the BUILD system (not musl/target system)
    # Tooling runs during build, so it needs native system libraries
    target_compile_options(ascii-instr-defer PRIVATE
        -O0
        -g
        -D_ITERATOR_DEBUG_LEVEL=0
        -fno-sanitize=all
        -fno-rtti
        -fexceptions
        -fno-lto
    )

    target_link_options(ascii-instr-defer PRIVATE
        -fno-sanitize=all
        -fno-lto
    )

    # Note: C++ standard library is linked automatically by clang++
    # On macOS with Homebrew LLVM, we may need explicit libc++
    # On Linux, the system libstdc++ is used by default
    if(APPLE)
        if(EXISTS "/usr/local/lib/libc++.a" AND EXISTS "/usr/local/lib/libc++abi.a")
            target_link_libraries(ascii-instr-defer PRIVATE "/usr/local/lib/libc++.a" "/usr/local/lib/libc++abi.a")
        elseif(EXISTS "/opt/homebrew/opt/llvm/lib/c++/libc++.a")
            target_link_libraries(ascii-instr-defer PRIVATE
                "/opt/homebrew/opt/llvm/lib/c++/libc++.a"
                "/opt/homebrew/opt/llvm/lib/c++/libc++abi.a"
            )
        endif()
    endif()

    set_target_properties(ascii-instr-defer PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
        INTERPROCEDURAL_OPTIMIZATION OFF
        # Override CMAKE_EXE_LINKER_FLAGS that has musl flags
        LINK_FLAGS ""
    )

    if(DEFINED LLVM_DEFINITIONS)
        separate_arguments(_llvm_defs_defer NATIVE_COMMAND "${LLVM_DEFINITIONS}")
        if(_llvm_defs_defer)
            target_compile_definitions(ascii-instr-defer PRIVATE ${_llvm_defs_defer})
        endif()
    endif()

    if(TARGET clang-cpp)
        target_link_libraries(ascii-instr-defer PRIVATE clang-cpp)
    endif()

    target_link_libraries(ascii-instr-defer PRIVATE LLVMSupport)

    target_link_libraries(ascii-instr-defer PRIVATE
        clangTooling
        clangFrontend
        clangAST
        clangASTMatchers
        clangBasic
        clangRewrite
        clangRewriteFrontend
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangEdit
        clangAnalysis
        clangAPINotes
        clangSupport
        ${LLVM_LIB_LIST}
    )

    # Link required system libraries that LLVM/Clang depend on
    target_link_libraries(ascii-instr-defer PRIVATE
        ${ZLIB_LIBRARY}
        ${ZSTD_LIBRARY}
    )

    # Link ncurses if available (Unix only, optional)
    if(NCURSES_LIBRARY)
        target_link_libraries(ascii-instr-defer PRIVATE ${NCURSES_LIBRARY})
    endif()

    set_target_properties(ascii-instr-defer PROPERTIES
        OUTPUT_NAME "ascii-instr-defer"
    )

    # Only use libc++ on macOS where it's the default
    # On Linux, use the system libstdc++ (default)
    if(APPLE AND CMAKE_CXX_COMPILER)
        get_filename_component(_cxx_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_cxx_root "${_cxx_compiler_dir}/.." ABSOLUTE)
        set(_libcxx_include "${_cxx_root}/include/c++/v1")
        if(EXISTS "${_libcxx_include}")
            target_include_directories(ascii-instr-defer BEFORE PRIVATE "${_libcxx_include}")
            target_compile_options(ascii-instr-defer PRIVATE
                "--no-default-config"
                "-isystem"
                "${_libcxx_include}"
                "-stdlib=libc++"
            )
            target_link_options(ascii-instr-defer PRIVATE
                "--no-default-config"
                "-stdlib=libc++"
            )
        endif()
    endif()

    # =========================================================================
    # Tooling Runtime Library Strategy
    # =========================================================================
    # To avoid rebuilding tooling when application code changes, the runtime
    # can use an installed ascii-chat library instead of building from source.
    #
    # Three modes:
    # 1. Development mode (default): Build runtime from source (rebuilds on changes)
    # 2. Installed library: Use system-installed ascii-chat library
    # 3. Custom library: Use user-specified library file (for bootstrap builds)
    #
    # Bootstrap workflow:
    #   cmake -B build
    #   cmake --build build --target ascii-chat-static  # Build library first
    #   cmake -B build -DASCII_TOOLING_LIBRARY_PATH=build/lib/libasciichat.a
    #   cmake --build build  # Now builds tooling with pre-built library
    # =========================================================================

    option(ASCII_TOOLING_USE_INSTALLED_LIBS
           "Use installed ascii-chat library for tooling runtime (avoids rebuilds on app changes)"
           OFF)

    set(ASCII_TOOLING_LIBRARY_PATH "" CACHE FILEPATH
        "Path to ascii-chat library for tooling runtime (overrides find_library)")

    if(ASCII_TOOLING_LIBRARY_PATH)
        # Mode 3: User specified library path
        if(NOT EXISTS "${ASCII_TOOLING_LIBRARY_PATH}")
            message(FATAL_ERROR "ASCII_TOOLING_LIBRARY_PATH does not exist: ${ASCII_TOOLING_LIBRARY_PATH}")
        endif()

        message(STATUS "Tooling runtime using custom library: ${ASCII_TOOLING_LIBRARY_PATH}")

        # Create imported target
        add_library(ascii-tooling-runtime-lib STATIC IMPORTED)
        set_target_properties(ascii-tooling-runtime-lib PROPERTIES
            IMPORTED_LOCATION "${ASCII_TOOLING_LIBRARY_PATH}"
        )

        # Create runtime interface that links to imported library
        add_library(ascii-debug-runtime INTERFACE)
        target_link_libraries(ascii-debug-runtime INTERFACE ascii-tooling-runtime-lib)
        target_include_directories(ascii-debug-runtime INTERFACE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_BINARY_DIR}/generated
        )

    elseif(ASCII_TOOLING_USE_INSTALLED_LIBS)
        # Mode 2: Find installed library
        if(WIN32)
            # Windows installation paths
            find_library(ASCII_INSTALLED_LIB
                NAMES asciichat libasciichat
                PATHS
                    "C:/Program Files/ascii-chat/lib"
                    "$ENV{ProgramFiles}/ascii-chat/lib"
                    "$ENV{ProgramW6432}/ascii-chat/lib"
                    "$ENV{LOCALAPPDATA}/ascii-chat/lib"
                    ${CMAKE_INSTALL_PREFIX}/lib
                DOC "Installed ascii-chat library"
            )
        else()
            # Unix/macOS installation paths
            find_library(ASCII_INSTALLED_LIB
                NAMES asciichat libasciichat
                PATHS
                    /usr/local/lib
                    /usr/lib
                    /opt/ascii-chat/lib
                    ${CMAKE_INSTALL_PREFIX}/lib
                DOC "Installed ascii-chat library"
            )
        endif()

        if(NOT ASCII_INSTALLED_LIB)
            message(FATAL_ERROR
                "ASCII_TOOLING_USE_INSTALLED_LIBS=ON but library not found.\n"
                "Install ascii-chat or use ASCII_TOOLING_LIBRARY_PATH to specify library location.")
        endif()

        message(STATUS "Tooling runtime using installed library: ${ASCII_INSTALLED_LIB}")

        # Create imported target
        add_library(ascii-tooling-runtime-lib STATIC IMPORTED)
        set_target_properties(ascii-tooling-runtime-lib PROPERTIES
            IMPORTED_LOCATION "${ASCII_INSTALLED_LIB}"
        )

        # Create runtime interface
        add_library(ascii-debug-runtime INTERFACE)
        target_link_libraries(ascii-debug-runtime INTERFACE ascii-tooling-runtime-lib)
        target_include_directories(ascii-debug-runtime INTERFACE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_BINARY_DIR}/generated
        )

    else()
        # Mode 1: Build runtime from source (development mode - rebuilds on changes)
        if(TARGET ascii-chat-debug)
            add_library(ascii-debug-runtime INTERFACE)
            target_link_libraries(ascii-debug-runtime INTERFACE ascii-chat-debug)
        else()
            add_library(ascii-debug-runtime STATIC ${TOOLING_SOURCE_PRINT_SRCS})
            target_include_directories(ascii-debug-runtime
                PUBLIC
                    ${CMAKE_SOURCE_DIR}/lib
                    ${CMAKE_BINARY_DIR}/generated
            )
            if(USE_MIMALLOC)
                set(_ascii_debug_mimalloc_includes "")
                if(TARGET mimalloc-static)
                    get_target_property(_ascii_mimalloc_iface mimalloc-static INTERFACE_INCLUDE_DIRECTORIES)
                    if(_ascii_mimalloc_iface)
                        list(APPEND _ascii_debug_mimalloc_includes ${_ascii_mimalloc_iface})
                    endif()
                endif()
                if(DEFINED MIMALLOC_INCLUDE_DIR AND MIMALLOC_INCLUDE_DIR)
                    list(APPEND _ascii_debug_mimalloc_includes "${MIMALLOC_INCLUDE_DIR}")
                endif()
                if(DEFINED MIMALLOC_SOURCE_DIR AND EXISTS "${MIMALLOC_SOURCE_DIR}/include")
                    list(APPEND _ascii_debug_mimalloc_includes "${MIMALLOC_SOURCE_DIR}/include")
                elseif(DEFINED FETCHCONTENT_BASE_DIR AND EXISTS "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include")
                    list(APPEND _ascii_debug_mimalloc_includes "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include")
                endif()
                list(REMOVE_DUPLICATES _ascii_debug_mimalloc_includes)
                if(_ascii_debug_mimalloc_includes)
                    target_include_directories(ascii-debug-runtime PRIVATE ${_ascii_debug_mimalloc_includes})
                endif()
            endif()
            # Don't link Threads on Windows - we use platform abstraction (native Win32 threads)
            if(NOT WIN32 AND TARGET Threads::Threads)
                target_link_libraries(ascii-debug-runtime PUBLIC Threads::Threads)
            endif()
            set_target_properties(ascii-debug-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endif()

    if(DEFINED LLVM_DEFINITIONS)
        separate_arguments(_llvm_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
        if(_llvm_defs)
            target_compile_definitions(ascii-instr-source-print PRIVATE ${_llvm_defs})
        endif()
    endif()

    if(TARGET ascii-chat-defer AND TARGET ascii-debug-runtime)
        add_dependencies(ascii-debug-runtime ascii-chat-defer)
        get_target_property(_ascii_debug_runtime_type ascii-debug-runtime TYPE)
        if(_ascii_debug_runtime_type STREQUAL "INTERFACE_LIBRARY")
            target_link_libraries(ascii-debug-runtime INTERFACE ascii-chat-defer)
        else()
            target_link_libraries(ascii-debug-runtime PRIVATE ascii-chat-defer)
        endif()
    endif()

    # Add LLVM library directory
    if(DEFINED LLVM_LIBRARY_DIRS)
        link_directories(${LLVM_LIBRARY_DIRS})
    endif()

    # LLVM_LIB_LIST already set from llvm-config above

    if(TARGET clang-cpp)
        target_link_libraries(ascii-instr-source-print PRIVATE clang-cpp)
    endif()

    # Link LLVMSupport EXPLICITLY and FIRST to ensure command-line static initialization works
    target_link_libraries(ascii-instr-source-print PRIVATE LLVMSupport)

    target_link_libraries(ascii-instr-source-print PRIVATE
        clangTooling
        clangFrontend
        clangAST
        clangASTMatchers
        clangBasic
        clangRewrite
        clangRewriteFrontend
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangEdit
        clangAnalysis
        clangAPINotes
        clangSupport
        ${LLVM_LIB_LIST}
    )

    # Link required system libraries that LLVM/Clang depend on
    target_link_libraries(ascii-instr-source-print PRIVATE
        ${ZLIB_LIBRARY}
        ${ZSTD_LIBRARY}
    )

    # Link ncurses if available (Unix only, optional)
    if(NCURSES_LIBRARY)
        target_link_libraries(ascii-instr-source-print PRIVATE ${NCURSES_LIBRARY})
    endif()

    set_target_properties(ascii-instr-source-print PROPERTIES
        OUTPUT_NAME "ascii-instr-source-print"
    )

    # Only use libc++ on macOS where it's the default
    # On Linux, use the system libstdc++ (default)
    if(APPLE AND CMAKE_CXX_COMPILER)
        get_filename_component(_cxx_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_cxx_root "${_cxx_compiler_dir}/.." ABSOLUTE)
        set(_libcxx_include "${_cxx_root}/include/c++/v1")
        if(EXISTS "${_libcxx_include}")
            message(STATUS "ascii-instr-source-print libc++ include: ${_libcxx_include}")
            target_include_directories(ascii-instr-source-print BEFORE PRIVATE "${_libcxx_include}")
            get_target_property(_ascii_instr_includes ascii-instr-source-print INCLUDE_DIRECTORIES)
            message(STATUS "ascii-instr-source-print include dirs: ${_ascii_instr_includes}")
            target_compile_options(ascii-instr-source-print PRIVATE
                "--no-default-config"
                "-isystem"
                "${_libcxx_include}"
                "-stdlib=libc++"
            )
            target_link_options(ascii-instr-source-print PRIVATE
                "--no-default-config"
                "-stdlib=libc++"
            )
        endif()
    endif()

    # ascii-source-print-report uses POSIX headers (dirent.h, getopt.h) - skip on Windows
    if(NOT WIN32)
        add_executable(ascii-source-print-report
            ${TOOLING_SOURCE_PRINT_REPORT_SRCS}
        )
        target_include_directories(ascii-source-print-report
            PRIVATE
                ${CMAKE_SOURCE_DIR}/lib
                ${CMAKE_BINARY_DIR}/generated
        )
        # Add mimalloc include directory if USE_MIMALLOC is enabled
        # This is needed because common.h includes <mimalloc.h> when USE_MIMALLOC is defined
        if(USE_MIMALLOC AND DEFINED FETCHCONTENT_BASE_DIR)
            target_include_directories(ascii-source-print-report
                PRIVATE
                    "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include"
            )
        endif()
        target_link_libraries(ascii-source-print-report
            ascii-chat-static
        )
        set_target_properties(ascii-source-print-report PROPERTIES OUTPUT_NAME "ascii-source-print-report")
    endif()

    # Restore original flags for other targets (in case function was called mid-build)
    set(CMAKE_C_FLAGS "${_SAVED_CMAKE_C_FLAGS}" PARENT_SCOPE)
    set(CMAKE_CXX_FLAGS "${_SAVED_CMAKE_CXX_FLAGS}" PARENT_SCOPE)
    set(CMAKE_EXE_LINKER_FLAGS "${_SAVED_CMAKE_EXE_LINKER_FLAGS}" PARENT_SCOPE)
endfunction()

function(ascii_add_debug_targets)
    ascii_add_tooling_targets()
endfunction()
