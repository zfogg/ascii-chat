# =============================================================================
# ACDS (ASCII-Chat Discovery Service) Executable Target
# =============================================================================
# Creates the discovery service binary that handles session management,
# WebRTC signaling, and peer discovery using the ACIP protocol over raw TCP.
#
# Dependencies:
#   - ascii-chat-static (reuses all existing libraries)
#   - No new external dependencies (uses existing libsodium, sqlite3)
#
# Output:
#   - build/bin/acds (or acds.exe on Windows)
# =============================================================================

message(STATUS "${BoldCyan}Building ACDS discovery service...${ColorReset}")

# Create discovery service executable
add_executable(acds ${ACDS_SRCS})

# Unity builds for faster compilation
if(ASCIICHAT_ENABLE_UNITY_BUILDS)
    set_target_properties(acds PROPERTIES UNITY_BUILD ON)
endif()

# Interprocedural optimization (LTO) for release builds
if(ASCIICHAT_ENABLE_IPO)
    set_property(TARGET acds PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Link against unified ascii-chat library (reuses all existing code)
add_dependencies(acds ascii-chat-static-build generate_version)
target_link_libraries(acds PRIVATE ascii-chat-static)

# Link system libraries explicitly (libsodium, sqlite3, etc.)
get_core_deps_libraries(CORE_LIBS)
target_link_libraries(acds PRIVATE ${CORE_LIBS})

# Link SQLite3 explicitly (acds-specific dependency)
target_link_libraries(acds PRIVATE ${SQLITE3_LIBRARIES})

# Include generated version header
target_include_directories(acds PRIVATE
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
)

# Build timing (matches pattern from Executables.cmake)
add_custom_command(TARGET acds PRE_LINK
    COMMAND ${CMAKE_COMMAND}
        -DACTION=start
        -DTARGET_NAME=acds
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Starting acds link timer"
)

add_custom_command(TARGET acds POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DACTION=end
        -DTARGET_NAME=acds
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Discovery service build complete"
)

# Set output directory
set_target_properties(acds PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Install target (optional - for system-wide installation)
install(TARGETS acds
    RUNTIME DESTINATION bin
    COMPONENT discovery_service
)

message(STATUS "${Green}ACDS discovery service target configured${ColorReset}")
