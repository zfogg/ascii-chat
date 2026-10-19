if(EMSCRIPTEN)
    add_library(webrtc_audio_processing INTERFACE)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_aec3)
    asciichat_dependency_defaults()
    # AEC3 exposes static component libraries that are embedded in the public library.
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_CXX_STANDARD 17)
    FetchContent_Declare(asciichat_aec3
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_aec3"
        URL https://github.com/zhixingheyixsh/webrtc_AEC3/archive/c1dbdb9924f59d9b3c09189673286665d55f6a5e.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR asciichat-patch-first)
    FetchContent_MakeAvailable(asciichat_aec3)
    set(WEBRTC_AEC3_SOURCE_DIR "${asciichat_aec3_SOURCE_DIR}")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/patch-webrtc-aec3-cmake.cmake")
    add_subdirectory("${asciichat_aec3_SOURCE_DIR}" "${asciichat_aec3_BINARY_DIR}" EXCLUDE_FROM_ALL)
    # Upstream uses private directory includes; publish the public audio API.
    target_include_directories(AudioProcess PUBLIC
        "${asciichat_aec3_SOURCE_DIR}"
        "${asciichat_aec3_SOURCE_DIR}/base"
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
