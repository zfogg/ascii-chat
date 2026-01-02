# =============================================================================
# ACDS Session Library
# =============================================================================
# Creates a library for ACDS session management that can be used by both
# the acds executable and integration tests.

# Source files for ACDS session library
set(ACDS_LIB_SRCS
    src/acds/session.c
    src/acds/strings.c
    src/acds/database.c
    src/acds/identity.c
)

# Create ACDS session library
add_library(ascii-chat-acds STATIC ${ACDS_LIB_SRCS})

# Link against dependencies
target_link_libraries(ascii-chat-acds
    PUBLIC
        ascii-chat-core
        ascii-chat-crypto
        ascii-chat-network
        ascii-chat-util
)

# Add include directories
target_include_directories(ascii-chat-acds PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)
