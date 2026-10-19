cmake_minimum_required(VERSION 3.30)
project(ascii-chat-project-settings LANGUAGES C)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/compiler/ProjectSettings.cmake")
target_compile_definitions(asciichat-project-options INTERFACE PROJECT_SETTINGS_PROBE=1)
target_include_directories(asciichat-project-includes INTERFACE "${CMAKE_CURRENT_LIST_DIR}")

add_executable(ascii-chat-settings-probe main.c)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vendor")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vendor/CMakeLists.txt"
    "add_library(vendor OBJECT vendor.c)\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vendor/vendor.c"
    "#ifdef PROJECT_SETTINGS_PROBE\n#error Project options leaked into a dependency\n#endif\nint vendor(void) { return 0; }\n")
add_subdirectory("${CMAKE_CURRENT_BINARY_DIR}/vendor" "${CMAKE_CURRENT_BINARY_DIR}/vendor-build")
cmake_language(DEFER CALL asciichat_finalize_project_settings)
enable_testing()
add_test(NAME project-settings COMMAND ascii-chat-settings-probe)
