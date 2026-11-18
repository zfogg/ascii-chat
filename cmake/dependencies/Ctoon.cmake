# CToon serialization dependency used by the MCP response transports.

include_guard(GLOBAL)
include(FetchContent)
include("${CMAKE_SOURCE_DIR}/cmake/utils/SourceDependency.cmake")

set(CTOON_BUILD_TESTS OFF CACHE BOOL "Disable CToon tests in ascii-chat" FORCE)
set(CTOON_BUILD_EXAMPLES OFF CACHE BOOL "Disable CToon examples in ascii-chat" FORCE)
set(CTOON_BUILD_DOCS OFF CACHE BOOL "Disable CToon documentation in ascii-chat" FORCE)

if(NOT TARGET ctoon::ctoon)
    asciichat_fetchcontent_declare(
        ctoon
        GIT_REPOSITORY https://github.com/MohammadRaziei/ctoon.git
        GIT_TAG v0.9.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(ctoon)
endif()

if(NOT TARGET ctoon::ctoon)
    message(FATAL_ERROR "CToon did not provide the ctoon::ctoon target")
endif()

if(WIN32 AND BUILD_SHARED_LIBS AND TARGET ctoon)
    # CToon’s Windows shared target does not annotate its C API with
    # __declspec(dllexport), so CMake otherwise emits a DLL without an import
    # library. Export the public C API from the upstream target automatically.
    set_target_properties(ctoon PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
if(TARGET ctoon_cli)
    set_target_properties(ctoon_cli PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# CToon is a normal C library. Its target follows BUILD_SHARED_LIBS, including
# static linkage required by the musl toolchain.
if(NOT TARGET asciichat-ctoon)
    add_library(asciichat-ctoon INTERFACE)
    target_link_libraries(asciichat-ctoon INTERFACE ctoon::ctoon)
endif()
