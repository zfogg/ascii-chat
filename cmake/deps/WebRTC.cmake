if(EMSCRIPTEN)
    add_library(webrtc_audio_processing INTERFACE)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
get_property(_asciichat_build_shared_libs CACHE BUILD_SHARED_LIBS PROPERTY VALUE)
function(asciichat_build_aec3)
    asciichat_dependency_defaults()
    set(BUILD_SHARED_LIBS "${_asciichat_build_shared_libs}")
    # The dependency patch and its child targets need the target architecture,
    # including Windows cross builds where the host processor is misleading.
    set(ASCIICHAT_TARGET_ARM64 OFF)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64|AARCH64)$"
       OR VCPKG_TARGET_TRIPLET MATCHES "^arm64-"
       OR CMAKE_C_COMPILER_TARGET MATCHES "^(aarch64|arm64)"
       OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(ASCIICHAT_TARGET_ARM64 ON)
    endif()
    # AEC3 exposes component libraries embedded in the public library. Keep
    # the caller's dependency mode for Debug/Dev; static release-like targets
    # already select BUILD_SHARED_LIBS=OFF at the preset level.
    if(NOT BUILD_SHARED_LIBS OR USE_MUSL OR PLATFORM_IOS)
        set(BUILD_SHARED_LIBS OFF)
    endif()
    set(CMAKE_CXX_STANDARD 17)
    FetchContent_Declare(webrtc_aec3
        BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/webrtc-aec3"
        URL https://github.com/zhixingheyixsh/webrtc_AEC3/archive/c1dbdb9924f59d9b3c09189673286665d55f6a5e.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    # Populate without adding the project. Its CMake files must be patched first.
    FetchContent_GetProperties(webrtc_aec3)
    if(TARGET AudioProcess OR TARGET aec3 OR TARGET api OR TARGET base)
        message(FATAL_ERROR "AEC3 targets already exist; ascii-chat must patch AEC3 before it is added")
    endif()
    if(NOT webrtc_aec3_POPULATED)
        cmake_policy(PUSH)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(webrtc_aec3)
        cmake_policy(POP)
    endif()
    set(WEBRTC_AEC3_SOURCE_DIR "${webrtc_aec3_SOURCE_DIR}")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/patch-webrtc-aec3-cmake.cmake")
    if(MSVC)
        # Older cached AEC3 source trees may already contain the upstream
        # GNU-only flag edits from an earlier configure. Remove those source
        # edits before adding the subproject so cl.exe sees only native flags.
        file(READ "${WEBRTC_AEC3_SOURCE_DIR}/CMakeLists.txt" _aec3_cmake)
        string(REPLACE "set(CMAKE_CXX_FLAGS \"\${CMAKE_CXX_FLAGS} -fvisibility=hidden\")" "" _aec3_cmake "${_aec3_cmake}")
        string(REPLACE "set(CMAKE_CXX_FLAGS \"\${CMAKE_CXX_FLAGS} -std=c++17\")" "" _aec3_cmake "${_aec3_cmake}")
        string(REPLACE "add_compile_options(-w)  # Suppress all warnings" "" _aec3_cmake "${_aec3_cmake}")
        file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/CMakeLists.txt" "${_aec3_cmake}")
    endif()
    if(WIN32)
        # The fetched project enables global PIC, which CMake translates to
        # ELF's -fPIC even for clang-cl/GNU-style Clang targeting Windows.
        file(READ "${WEBRTC_AEC3_SOURCE_DIR}/CMakeLists.txt" _aec3_cmake)
        string(REPLACE "set(CMAKE_POSITION_INDEPENDENT_CODE ON)"
            "set(CMAKE_POSITION_INDEPENDENT_CODE OFF)" _aec3_cmake "${_aec3_cmake}")
        string(REPLACE "set(CMAKE_CXX_FLAGS \"\${CMAKE_CXX_FLAGS} -fvisibility=hidden -fPIC -pthread -std=c++17\")"
            "set(CMAKE_CXX_FLAGS \"\${CMAKE_CXX_FLAGS} -fvisibility=hidden -pthread -std=c++17\")"
            _aec3_cmake "${_aec3_cmake}")
        file(WRITE "${WEBRTC_AEC3_SOURCE_DIR}/CMakeLists.txt" "${_aec3_cmake}")
        set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
    endif()
    add_subdirectory("${webrtc_aec3_SOURCE_DIR}" "${webrtc_aec3_BINARY_DIR}")
    # Upstream uses private directory includes; publish the public audio API.
    target_include_directories(AudioProcess PUBLIC
        "${webrtc_aec3_SOURCE_DIR}"
        "${webrtc_aec3_SOURCE_DIR}/base"
        "${webrtc_aec3_SOURCE_DIR}/base/abseil"
        "${webrtc_aec3_SOURCE_DIR}/api"
        "${webrtc_aec3_SOURCE_DIR}/audio_processing/include")
    if(WIN32)
        target_compile_definitions(AudioProcess PUBLIC WEBRTC_WIN NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_definitions(AudioProcess PUBLIC WEBRTC_POSIX)
    endif()
    # Windows object files are position-independent by default; Clang's
    # Windows driver rejects the ELF-only -fPIC flag emitted by CMake for this
    # property. Keep PIC for ELF/Mach-O targets only.
    if(NOT WIN32)
        set_target_properties(AudioProcess aec3 api base PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
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
target_link_libraries(webrtc_audio_processing INTERFACE AudioProcess aec3 api base)
