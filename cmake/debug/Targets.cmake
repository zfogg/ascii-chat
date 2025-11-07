# Guard against multiple inclusions
if(DEFINED ASCII_DEBUG_TARGETS_CMAKE_INCLUDED)
    return()
endif()
set(ASCII_DEBUG_TARGETS_CMAKE_INCLUDED TRUE)

function(ascii_add_debug_targets)
    if(NOT CMAKE_CXX_COMPILER_LOADED)
        if(NOT CMAKE_CXX_COMPILER)
            get_filename_component(_clang_dir "${CMAKE_C_COMPILER}" DIRECTORY)
            find_program(_clangxx NAMES clang++ PATHS "${_clang_dir}" NO_DEFAULT_PATH)
            if(NOT _clangxx)
                find_program(_clangxx NAMES clang++)
            endif()
            if(_clangxx)
                set(CMAKE_CXX_COMPILER "${_clangxx}" CACHE FILEPATH "C++ compiler for instrumentation" FORCE)
            endif()
        endif()
        enable_language(CXX)
    endif()

    find_package(LLVM REQUIRED CONFIG)
    find_package(Clang REQUIRED CONFIG)
    find_package(Threads REQUIRED)

    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
    include(AddLLVM)
    include(HandleLLVMOptions)

    if(NOT TARGET ascii-instr-tool)
        add_executable(ascii-instr-tool
            src/debug/ascii_instr_tool.cpp
        )
    endif()

    target_include_directories(ascii-instr-tool PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
    )

    target_compile_features(ascii-instr-tool PRIVATE cxx_std_17)
    target_compile_options(ascii-instr-tool PRIVATE -stdlib=libc++)
    target_link_options(ascii-instr-tool PRIVATE -stdlib=libc++)

    add_library(ascii-debug-runtime STATIC ${DEBUG_RUNTIME_SRCS})
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
    target_link_libraries(ascii-debug-runtime PUBLIC Threads::Threads)
    set_target_properties(ascii-debug-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(DEFINED LLVM_DEFINITIONS)
        separate_arguments(_llvm_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
        if(_llvm_defs)
            target_compile_definitions(ascii-instr-tool PRIVATE ${_llvm_defs})
        endif()
    endif()

    if(TARGET clang-cpp)
        target_link_libraries(ascii-instr-tool PRIVATE clang-cpp LLVM)
    else()
        target_link_libraries(ascii-instr-tool PRIVATE
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
            LLVM
        )
    endif()

    set_target_properties(ascii-instr-tool PROPERTIES
        OUTPUT_NAME "ascii-instr-tool"
    )

    if(CMAKE_CXX_COMPILER)
        get_filename_component(_cxx_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_cxx_root "${_cxx_compiler_dir}/.." ABSOLUTE)
        set(_libcxx_include "${_cxx_root}/include/c++/v1")
        if(EXISTS "${_libcxx_include}")
            message(STATUS "ascii-instr-tool libc++ include: ${_libcxx_include}")
            target_include_directories(ascii-instr-tool BEFORE PRIVATE "${_libcxx_include}")
            get_target_property(_ascii_instr_includes ascii-instr-tool INCLUDE_DIRECTORIES)
            message(STATUS "ascii-instr-tool include dirs: ${_ascii_instr_includes}")
            target_compile_options(ascii-instr-tool PRIVATE
                "--no-default-config"
                "-isystem"
                "${_libcxx_include}"
                "-stdlib=libc++"
            )
            target_link_options(ascii-instr-tool PRIVATE
                "--no-default-config"
                "-stdlib=libc++"
            )
        endif()
    endif()

    add_executable(ascii-instr-report
        ${DEBUG_TOOL_SRCS}
    )
    target_include_directories(ascii-instr-report
        PRIVATE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_BINARY_DIR}/generated
    )
    target_link_libraries(ascii-instr-report
        PRIVATE
            ascii-chat-core
            ascii-chat-platform
            ascii-chat-util
    )
    set_target_properties(ascii-instr-report PROPERTIES OUTPUT_NAME "ascii-instr-report")
endfunction()
