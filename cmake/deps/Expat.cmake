include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_expat)
    asciichat_dependency_defaults()
    set(_prefix "${CMAKE_CURRENT_BINARY_DIR}/_deps/installed/expat")
    if(USE_MUSL OR PLATFORM_IOS)
        set(CMAKE_INSTALL_PREFIX "${_prefix}")
    endif()
    set(EXPAT_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(EXPAT_BUILD_DOCS OFF)
    set(EXPAT_BUILD_EXAMPLES OFF)
    set(EXPAT_BUILD_TESTS OFF)
    set(EXPAT_BUILD_TOOLS OFF)
    FetchContent_Declare(asciichat_expat
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_expat"
        URL https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(asciichat_expat)
    add_custom_target(asciichat-expat-stage
        COMMAND "${CMAKE_COMMAND}" --install "${asciichat_expat_BINARY_DIR}" --prefix "${_prefix}"
        DEPENDS expat VERBATIM)
    set(EXPAT_PREFIX "${_prefix}" PARENT_SCOPE)
    set(EXPAT_INCLUDE_DIRS "${asciichat_expat_SOURCE_DIR}/lib;${asciichat_expat_BINARY_DIR}" PARENT_SCOPE)
endfunction()
asciichat_build_expat()
