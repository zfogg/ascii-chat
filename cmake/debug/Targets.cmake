# Guard against multiple inclusions
if(DEFINED ASCII_DEBUG_TARGETS_CMAKE_INCLUDED)
    return()
endif()
set(ASCII_DEBUG_TARGETS_CMAKE_INCLUDED TRUE)

function(ascii_add_debug_targets)
    if(TARGET ascii-instr-tool)
        return()
    endif()

    find_package(LLVM REQUIRED CONFIG)
    find_package(Clang REQUIRED CONFIG)
    find_package(Threads REQUIRED)

    add_library(ascii-debug-runtime STATIC ${DEBUG_RUNTIME_SRCS})
    target_include_directories(ascii-debug-runtime
        PUBLIC
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_BINARY_DIR}/generated
    )
    target_link_libraries(ascii-debug-runtime PUBLIC Threads::Threads)
    set_target_properties(ascii-debug-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_executable(ascii-instr-tool
        ${CMAKE_SOURCE_DIR}/src/debug/ascii_instr_tool.cpp
    )
    target_compile_features(ascii-instr-tool PRIVATE cxx_std_17)
    target_include_directories(ascii-instr-tool
        PRIVATE
            ${LLVM_INCLUDE_DIRS}
            ${CLANG_INCLUDE_DIRS}
            ${CMAKE_SOURCE_DIR}/lib
    )

    if(DEFINED LLVM_DEFINITIONS)
        target_compile_definitions(ascii-instr-tool PRIVATE ${LLVM_DEFINITIONS})
    endif()

    if(TARGET clang-cpp)
        target_link_libraries(ascii-instr-tool PRIVATE clang-cpp)
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

    target_link_libraries(ascii-instr-tool PRIVATE LLVM::Support)
    set_target_properties(ascii-instr-tool PROPERTIES OUTPUT_NAME "ascii-instr-tool")
endfunction()
