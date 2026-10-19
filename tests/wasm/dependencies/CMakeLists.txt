cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ascii-chat-wasm-dependency-test LANGUAGES C)
endif()

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure this test with the Emscripten toolchain (emcmake cmake).")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    get_filename_component(ASCIICHAT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/sources" CACHE PATH "Dependency sources")
    include("${ASCIICHAT_ROOT}/cmake/deps/Libsodium.cmake")
    include("${ASCIICHAT_ROOT}/cmake/deps/PCRE2.cmake")
    include("${ASCIICHAT_ROOT}/cmake/deps/Opus.cmake")
endif()

add_executable(wasm-dependencies main.c)
set_target_properties(wasm-dependencies PROPERTIES SUFFIX ".cjs")
target_link_libraries(wasm-dependencies PRIVATE
    ${LIBSODIUM_LIBRARIES} ${PCRE2_LIBRARIES} ${OPUS_LIBRARIES})
target_link_options(wasm-dependencies PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sSINGLE_FILE=1)

enable_testing()
add_test(NAME wasm-dependencies
    COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:wasm-dependencies>)
