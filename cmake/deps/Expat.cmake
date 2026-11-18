include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_expat)
    asciichat_dependency_defaults()
    set(_prefix "${ASCIICHAT_DEPS_CACHE_DIR}/build/installed/expat")
    if(USE_MUSL OR PLATFORM_IOS)
        set(CMAKE_INSTALL_PREFIX "${_prefix}")
    endif()
    set(EXPAT_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(EXPAT_BUILD_DOCS OFF)
    set(EXPAT_BUILD_EXAMPLES OFF)
    set(EXPAT_BUILD_TESTS OFF)
    set(EXPAT_BUILD_TOOLS OFF)
    asciichat_fetchcontent_declare(asciichat_expat
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/expat"
        URL https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(asciichat_expat)
    if(NOT TARGET asciichat-expat-stage)
        add_custom_target(asciichat-expat-stage
            COMMAND "${CMAKE_COMMAND}" --install "${asciichat_expat_BINARY_DIR}" --prefix "${_prefix}"
            DEPENDS expat VERBATIM)
    endif()
    set(EXPAT_PREFIX "${_prefix}" PARENT_SCOPE)
endfunction()
asciichat_build_expat()
if(NOT TARGET asciichat-expat)
    add_library(asciichat-expat INTERFACE)
    if(TARGET expat)
        target_link_libraries(asciichat-expat INTERFACE expat)
    elseif(TARGET expat::expat)
        target_link_libraries(asciichat-expat INTERFACE expat::expat)
    else()
        message(FATAL_ERROR "Expat target is required")
    endif()
    target_include_directories(asciichat-expat INTERFACE
        "${asciichat_expat_SOURCE_DIR}/lib"
        "${asciichat_expat_BINARY_DIR}")
endif()
