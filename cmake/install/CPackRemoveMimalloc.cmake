# CPackRemoveMimalloc.cmake
# This script removes mimalloc files from the CPack staging directory
# Mimalloc is statically linked into the binary, so headers/libraries are not needed

message(STATUS "Removing mimalloc files from installation...")
message(STATUS "  CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  CPACK_TEMPORARY_DIRECTORY = ${CPACK_TEMPORARY_DIRECTORY}")
message(STATUS "  CPACK_TOPLEVEL_DIRECTORY = ${CPACK_TOPLEVEL_DIRECTORY}")
message(STATUS "  DESTDIR = $ENV{DESTDIR}")

# The install directory is set by CPack
# For STGZ generator, it's CMAKE_INSTALL_PREFIX
set(INSTALL_DIR "${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Using install directory: ${INSTALL_DIR}")

# Remove mimalloc headers
file(GLOB_RECURSE MIMALLOC_HEADERS "${INSTALL_DIR}/include/mimalloc*")
message(STATUS "  Found ${CMAKE_MATCH_COUNT} mimalloc header files")
message(STATUS "  Pattern: ${INSTALL_DIR}/include/mimalloc*")
message(STATUS "  Results: ${MIMALLOC_HEADERS}")
foreach(HEADER_FILE ${MIMALLOC_HEADERS})
    message(STATUS "  Removing header: ${HEADER_FILE}")
    file(REMOVE "${HEADER_FILE}")
endforeach()

# Remove empty mimalloc include directory
if(EXISTS "${INSTALL_DIR}/include")
    file(GLOB MIMALLOC_DIRS "${INSTALL_DIR}/include/mimalloc*")
    foreach(MIMALLOC_DIR ${MIMALLOC_DIRS})
        if(IS_DIRECTORY "${MIMALLOC_DIR}")
            message(STATUS "  Removing directory: ${MIMALLOC_DIR}")
            file(REMOVE_RECURSE "${MIMALLOC_DIR}")
        endif()
    endforeach()
endif()

# Remove mimalloc libraries
file(GLOB_RECURSE MIMALLOC_LIBS "${INSTALL_DIR}/lib/mimalloc*")
foreach(LIB_FILE ${MIMALLOC_LIBS})
    message(STATUS "  Removing: ${LIB_FILE}")
    if(IS_DIRECTORY "${LIB_FILE}")
        file(REMOVE_RECURSE "${LIB_FILE}")
    else()
        file(REMOVE "${LIB_FILE}")
    endif()
endforeach()

# Remove mimalloc cmake files
file(GLOB_RECURSE MIMALLOC_CMAKE "${INSTALL_DIR}/lib/cmake/mimalloc*")
foreach(CMAKE_FILE ${MIMALLOC_CMAKE})
    message(STATUS "  Removing: ${CMAKE_FILE}")
    if(IS_DIRECTORY "${CMAKE_FILE}")
        file(REMOVE_RECURSE "${CMAKE_FILE}")
    else()
        file(REMOVE "${CMAKE_FILE}")
    endif()
endforeach()

# Remove mimalloc pkgconfig files
file(GLOB_RECURSE MIMALLOC_PC "${INSTALL_DIR}/lib/pkgconfig/*mimalloc*")
foreach(PC_FILE ${MIMALLOC_PC})
    message(STATUS "  Removing: ${PC_FILE}")
    file(REMOVE "${PC_FILE}")
endforeach()

message(STATUS "Mimalloc files removed from installation")
