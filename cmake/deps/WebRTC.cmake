if(EMSCRIPTEN)
    add_library(webrtc_audio_processing INTERFACE)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_aec3)
    asciichat_dependency_defaults()
    # The dependency patch and its child targets need the target architecture,
    # including Windows cross builds where the host processor is misleading.
    set(ASCIICHAT_TARGET_ARM64 OFF)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64|AARCH64)$"
       OR VCPKG_TARGET_TRIPLET MATCHES "^arm64-"
       OR CMAKE_C_COMPILER_TARGET MATCHES "^(aarch64|arm64)"
       OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(ASCIICHAT_TARGET_ARM64 ON)
    endif()
    # AEC3 exposes static component libraries that are embedded in the public library.
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_CXX_STANDARD 17)
    FetchContent_Declare(asciichat_aec3
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_aec3"
        URL https://github.com/zhixingheyixsh/webrtc_AEC3/archive/c1dbdb9924f59d9b3c09189673286665d55f6a5e.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        # Populate first; configure the patched root explicitly below.
        SOURCE_SUBDIR ascii-chat-patched-manual
        )
    # Populate without adding the project, even if a parent supplied the first
    # declaration without SOURCE_SUBDIR. Its CMake files must be patched first.
    FetchContent_GetProperties(asciichat_aec3)
    if(TARGET AudioProcess OR TARGET aec3 OR TARGET api OR TARGET base)
        message(FATAL_ERROR "AEC3 targets already exist; ascii-chat must patch AEC3 before it is added")
    endif()
    if(NOT asciichat_aec3_POPULATED)
        cmake_policy(PUSH)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(asciichat_aec3)
        cmake_policy(POP)
    endif()
    set(WEBRTC_AEC3_SOURCE_DIR "${asciichat_aec3_SOURCE_DIR}")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/patch-webrtc-aec3-cmake.cmake")
    add_subdirectory("${asciichat_aec3_SOURCE_DIR}" "${asciichat_aec3_BINARY_DIR}")
    # Upstream uses private directory includes; publish the public audio API.
    target_include_directories(AudioProcess PUBLIC
        "${asciichat_aec3_SOURCE_DIR}"
        "${asciichat_aec3_SOURCE_DIR}/base"
        "${asciichat_aec3_SOURCE_DIR}/base/abseil"
        "${asciichat_aec3_SOURCE_DIR}/api"
        "${asciichat_aec3_SOURCE_DIR}/audio_processing/include")
    if(WIN32)
        target_compile_definitions(AudioProcess PUBLIC WEBRTC_WIN NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_definitions(AudioProcess PUBLIC WEBRTC_POSIX)
    endif()
    set_target_properties(AudioProcess aec3 api base PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # MSVC's C++ container annotations must match the consuming ASan objects.
    if(WIN32 AND "-fsanitize=address" IN_LIST ASCIICHAT_SANITIZER_COMPILE_FLAGS)
        foreach(_target AudioProcess aec3 api base)
            target_compile_options(${_target} PRIVATE -fsanitize=address)
        endforeach()
    endif()
    # The factory and implementation reference each other; CMake repeats this
    # static component group when linking a consumer on one-pass linkers.
    target_link_libraries(api PUBLIC aec3)
    target_link_libraries(aec3 api AudioProcess)
    set_target_properties(AudioProcess aec3 api PROPERTIES LINK_INTERFACE_MULTIPLICITY 3)
    if(ABSEIL_INCLUDE_DIRS)
        foreach(_target AudioProcess aec3 api base)
            target_include_directories(${_target} BEFORE PRIVATE ${ABSEIL_INCLUDE_DIRS})
        endforeach()
    endif()
endfunction()
asciichat_build_aec3()
add_library(webrtc_audio_processing INTERFACE)
target_link_libraries(webrtc_audio_processing INTERFACE AudioProcess)
