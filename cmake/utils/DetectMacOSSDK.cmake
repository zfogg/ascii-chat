# =============================================================================
# DetectMacOSSDK.cmake - Centralized macOS SDK Path Detection
# =============================================================================
# Provides a function to resolve the macOS SDK path using a cascade:
#   1. CMAKE_OSX_SYSROOT (if already set)
#   2. HOMEBREW_SDKROOT environment variable
#   3. xcrun --show-sdk-path
#
# Usage:
#   asciichat_detect_macos_sdk(_result)
#   if(_result)
#       message(STATUS "SDK: ${_result}")
#   endif()
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_DETECT_MACOS_SDK_INCLUDED)
    return()
endif()
set(_ASCIICHAT_DETECT_MACOS_SDK_INCLUDED TRUE)

function(asciichat_detect_macos_sdk _output_var)
    # 1. Use CMAKE_OSX_SYSROOT if already set
    if(CMAKE_OSX_SYSROOT)
        set(${_output_var} "${CMAKE_OSX_SYSROOT}" PARENT_SCOPE)
        return()
    endif()

    # 2. Check HOMEBREW_SDKROOT environment variable
    if(DEFINED ENV{HOMEBREW_SDKROOT})
        set(${_output_var} "$ENV{HOMEBREW_SDKROOT}" PARENT_SCOPE)
        return()
    endif()

    # 3. Detect via xcrun
    find_program(_XCRUN_EXECUTABLE xcrun)
    if(_XCRUN_EXECUTABLE)
        execute_process(
            COMMAND ${_XCRUN_EXECUTABLE} --show-sdk-path
            OUTPUT_VARIABLE _sdk_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_sdk_path AND EXISTS "${_sdk_path}")
            set(${_output_var} "${_sdk_path}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${_output_var} "" PARENT_SCOPE)
endfunction()
