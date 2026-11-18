# CToon serialization dependency used by the MCP response transports.

include_guard(GLOBAL)
include(FetchContent)
include("${CMAKE_SOURCE_DIR}/cmake/utils/SourceDependency.cmake")

set(CTOON_BUILD_TESTS OFF CACHE BOOL "Disable CToon tests in ascii-chat" FORCE)
set(CTOON_BUILD_EXAMPLES OFF CACHE BOOL "Disable CToon examples in ascii-chat" FORCE)
set(CTOON_BUILD_DOCS OFF CACHE BOOL "Disable CToon documentation in ascii-chat" FORCE)

if(NOT TARGET ctoon::ctoon)
    FetchContent_Declare(
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

# CToon is a normal C library. Its target follows BUILD_SHARED_LIBS, including
# static linkage required by the musl toolchain.
set(CTOON_LIBRARIES ctoon::ctoon CACHE INTERNAL "CToon target used by ascii-chat")
if(NOT TARGET asciichat-ctoon)
    add_library(asciichat-ctoon INTERFACE)
    target_link_libraries(asciichat-ctoon INTERFACE ctoon::ctoon)
endif()
