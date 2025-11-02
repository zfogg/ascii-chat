# =============================================================================
# Windows-Specific Workarounds Module
# =============================================================================
# This module contains all Windows-specific fixes and workarounds

# Fix Windows-Clang platform issues with -nostartfiles and -nostdlib
# These flags are inappropriate for normal userspace applications
function(fix_windows_clang_linking)
    if(NOT (WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang"))
        return()
    endif()

    # CMake's Windows-Clang platform files incorrectly add -nostartfiles -nostdlib
    # These flags prevent AddressSanitizer from finding required runtime libraries

    # Remove the problematic flags if they exist in CMAKE_C_STANDARD_LIBRARIES
    string(REPLACE "-nostdlib" "" CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}")
    string(REPLACE "-nostartfiles" "" CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}")
    set(CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}" PARENT_SCOPE)

    # Remove from linker flags too
    string(REPLACE "-nostdlib" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-nostartfiles" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-nostdlib" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REPLACE "-nostartfiles" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" PARENT_SCOPE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}" PARENT_SCOPE)

    message(STATUS "Applied Windows-Clang linking fixes for normal userspace application")
endfunction()

# Find and configure MSVC libraries for Windows builds
# Required for Debug and Dev builds with Clang on Windows
function(find_msvc_libraries)
    if(NOT WIN32)
        return()
    endif()

    if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
        return()
    endif()

    # Try to find MSVC installation in common locations and editions
    set(MSVC_LIB_PATH "")

    # Try different Visual Studio editions and years
    set(VS_EDITIONS "Community" "Professional" "Enterprise" "BuildTools" "Insiders")
    set(VS_YEARS "18" "2026" "2022" "2019" "2017")

    foreach(VS_YEAR ${VS_YEARS})
        foreach(VS_EDITION ${VS_EDITIONS})
            set(VS_PATH "C:/Program Files/Microsoft Visual Studio/${VS_YEAR}/${VS_EDITION}/VC/Tools/MSVC")
            if(EXISTS "${VS_PATH}")
                file(GLOB MSVC_VERSIONS "${VS_PATH}/*")
                if(MSVC_VERSIONS)
                    list(GET MSVC_VERSIONS -1 MSVC_VERSION_DIR)  # Get the latest version
                    set(MSVC_LIB_CANDIDATE "${MSVC_VERSION_DIR}/lib/x64")
                    if(EXISTS "${MSVC_LIB_CANDIDATE}")
                        set(MSVC_LIB_PATH "${MSVC_LIB_CANDIDATE}")
                        break()
                    endif()
                endif()
            endif()
        endforeach()
        if(MSVC_LIB_PATH)
            break()
        endif()
    endforeach()

    if(MSVC_LIB_PATH)
        link_directories("${MSVC_LIB_PATH}")
        message(STATUS "Added MSVC library path: ${MSVC_LIB_PATH}")
    else()
        message(FATAL_ERROR "Visual Studio MSVC libraries not found! Windows builds require Visual Studio runtime libraries.\n"
                          "Please install Visual Studio 2017, 2019, 2022, or 2026 Insiders (Community, Professional, Enterprise, Build Tools, or Insiders).")
    endif()
endfunction()
