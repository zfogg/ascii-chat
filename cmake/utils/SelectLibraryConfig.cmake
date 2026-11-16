# =============================================================================
# SelectLibraryConfig.cmake - Release/Debug Library Selection
# =============================================================================
# Provides a macro to select between release and debug library variants.
# Single-config builds select explicitly so the custom Dev configuration uses
# vcpkg's debug libraries just like Debug. Multi-config generators retain the
# CMake-standard `optimized X debug Y` syntax.
#
# Usage:
#   asciichat_select_library_config(
#       RELEASE_LIB ${MY_LIB_RELEASE}
#       DEBUG_LIB   ${MY_LIB_DEBUG}
#       OUTPUT      MY_LIBRARIES
#   )
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_SELECT_LIBRARY_CONFIG_INCLUDED)
    return()
endif()
set(_ASCIICHAT_SELECT_LIBRARY_CONFIG_INCLUDED TRUE)

macro(asciichat_select_library_config)
    cmake_parse_arguments(_SLC "" "RELEASE_LIB;DEBUG_LIB;OUTPUT" "" ${ARGN})

    if(CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev)$")
        if(_SLC_DEBUG_LIB)
            set(${_SLC_OUTPUT} ${_SLC_DEBUG_LIB})
        else()
            set(${_SLC_OUTPUT} ${_SLC_RELEASE_LIB})
        endif()
    elseif(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
        if(_SLC_RELEASE_LIB)
            set(${_SLC_OUTPUT} ${_SLC_RELEASE_LIB})
        else()
            set(${_SLC_OUTPUT} ${_SLC_DEBUG_LIB})
        endif()
    elseif(_SLC_RELEASE_LIB AND _SLC_DEBUG_LIB)
        set(${_SLC_OUTPUT} optimized ${_SLC_RELEASE_LIB} debug ${_SLC_DEBUG_LIB})
    elseif(_SLC_RELEASE_LIB)
        set(${_SLC_OUTPUT} ${_SLC_RELEASE_LIB})
    elseif(_SLC_DEBUG_LIB)
        set(${_SLC_OUTPUT} ${_SLC_DEBUG_LIB})
    else()
        set(${_SLC_OUTPUT} "")
    endif()
endmacro()
