# =============================================================================
# Network Module (ascii-chat-network)
# =============================================================================
# Network protocol, packet handling, compression, and CRC32 checksums
# =============================================================================

set(NETWORK_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/network.c
    ${CMAKE_CURRENT_SOURCE_DIR}/packet.c
    ${CMAKE_CURRENT_SOURCE_DIR}/av.c
    ${CMAKE_SOURCE_DIR}/lib/compression.c
    ${CMAKE_SOURCE_DIR}/lib/crc32.c
)

create_ascii_chat_module(ascii-chat-network "${NETWORK_SRCS}")

# =============================================================================
# Dependencies
# =============================================================================

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-network
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-crypto
        ascii-chat-core
        ${ZSTD_LIBRARIES}
    )
else()
    # For OBJECT libs, link external deps only
    target_link_libraries(ascii-chat-network ${ZSTD_LIBRARIES})
endif()
