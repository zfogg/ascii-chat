# =============================================================================
# SelectLibraryConfig.cmake - Release/Debug Library Selection
# =============================================================================
# Provides a macro to select between release and debug library variants.
# Produces the CMake-standard `optimized X debug Y` syntax when both are found.
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

    if(_SLC_RELEASE_LIB AND _SLC_DEBUG_LIB)
        set(${_SLC_OUTPUT} optimized ${_SLC_RELEASE_LIB} debug ${_SLC_DEBUG_LIB})
    elseif(_SLC_RELEASE_LIB)
        set(${_SLC_OUTPUT} ${_SLC_RELEASE_LIB})
    elseif(_SLC_DEBUG_LIB)
        set(${_SLC_OUTPUT} ${_SLC_DEBUG_LIB})
    else()
        set(${_SLC_OUTPUT} "")
    endif()
endmacro()
