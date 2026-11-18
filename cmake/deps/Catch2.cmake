# =============================================================================
# Catch2 v3 test framework
# =============================================================================

set(ASCIICHAT_TEST_FRAMEWORKS "catch2" CACHE STRING
    "Semicolon-separated native test frameworks to enable")

if(NOT BUILD_TESTS)
    set(BUILD_CATCH2_TESTS OFF)
    return()
endif()

if(NOT "catch2" IN_LIST ASCIICHAT_TEST_FRAMEWORKS)
    set(BUILD_CATCH2_TESTS OFF)
    message(STATUS "Catch2 adapter disabled by ASCIICHAT_TEST_FRAMEWORKS=${ASCIICHAT_TEST_FRAMEWORKS}")
    return()
endif()

set(BUILD_CATCH2_TESTS ON)

if(BUILD_CATCH2_TESTS)
    # Windows obtains the pinned package from the vcpkg manifest. Linux and
    # macOS use the same pinned Catch2 release through FetchContent when a
    # system package is not available, so the test contract is identical on
    # all three native platforms.
    # The vcpkg Windows Catch2 binaries are not built with ascii-chat's
    # sanitizer/STL ABI flags. Build the pinned Catch2 sources in that case so
    # the test driver can link the same Catch2 release everywhere.
    set(_catch2_use_package TRUE)
    if(WIN32)
        set(_catch2_use_package FALSE)
    endif()
    if(_catch2_use_package)
        find_package(Catch2 3 CONFIG QUIET)
    endif()
    if(NOT Catch2_FOUND)
        include(FetchContent)
        if(ASCIICHAT_DEPS_CACHE_DIR)
            set(FETCHCONTENT_BASE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/catch2" CACHE PATH
                "Shared source/build cache for Catch2" FORCE)
        endif()
        set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_HELPERS OFF CACHE BOOL "" FORCE)
    asciichat_fetchcontent_declare(catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.12.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(catch2)
        set(ASCIICHAT_CATCH2_FROM_SOURCE TRUE)
        # Dependency include directories can put vcpkg's Catch2 headers ahead
        # of the pinned source tree. Keep the source and headers from this
        # FetchContent instance paired together.
        foreach(_catch2_target Catch2 Catch2Main)
            if(TARGET ${_catch2_target})
                target_include_directories(${_catch2_target} BEFORE PRIVATE
                    "${catch2_SOURCE_DIR}/src")
            endif()
        endforeach()
        foreach(_catch2_target Catch2 Catch2Main)
            if(TARGET ${_catch2_target})
                if(WIN32)
                    # The project deliberately uses the release MSVC STL ABI
                    # for native dependencies, including Debug builds.
                    target_compile_definitions(${_catch2_target} PRIVATE
                        _ITERATOR_DEBUG_LEVEL=0)
                endif()
                # Catch2 is third-party code. Keep it outside ascii-chat's
                # sanitizer instrumentation: Catch2 intentionally relies on
                # unsigned wraparound in its RNG and hashing implementation.
            endif()
        endforeach()
    endif()
    include(Catch)
    set(CATCH2_FOUND TRUE)
    message(STATUS "Catch2: enabled native test framework")
endif()
