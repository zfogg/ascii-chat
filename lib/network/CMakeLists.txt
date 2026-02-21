# lib/network - Networking layer
# Note: Source file properties (C++ language, mdns warnings) set in root CMakeLists.txt

set(NETWORK_SRCS
    # Core network files
    ${CMAKE_CURRENT_SOURCE_DIR}/network.c
    ${CMAKE_CURRENT_SOURCE_DIR}/packet.c
    ${CMAKE_CURRENT_SOURCE_DIR}/packet_parsing.c
    ${CMAKE_CURRENT_SOURCE_DIR}/frame_validator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/compression.c
    ${CMAKE_CURRENT_SOURCE_DIR}/crc32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/packet_queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/http_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dns.c
    ${CMAKE_CURRENT_SOURCE_DIR}/update_checker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/errors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/parallel_connect.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client.c
    # rate_limit/
    ${CMAKE_CURRENT_SOURCE_DIR}/rate_limit/rate_limit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rate_limit/memory.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rate_limit/sqlite.c
    # tcp/
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/transport.c
    # websocket/
    ${CMAKE_CURRENT_SOURCE_DIR}/websocket/transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/websocket/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/websocket/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/websocket/callback_timing.c
    # webrtc/
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/webrtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/peer_manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/stun.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/turn_credentials.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/sdp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/ice.c
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc/ice_selected_pair.cpp
    # acip/
    ${CMAKE_CURRENT_SOURCE_DIR}/acip/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/acip/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/acip/acds_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/acip/acds_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/acip/send.c
    ${CMAKE_CURRENT_SOURCE_DIR}/acip/handlers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/acip/acds_handlers.c
    # consensus/
    ${CMAKE_CURRENT_SOURCE_DIR}/consensus/packets.c
    ${CMAKE_CURRENT_SOURCE_DIR}/consensus/topology.c
    ${CMAKE_CURRENT_SOURCE_DIR}/consensus/state.c
    ${CMAKE_CURRENT_SOURCE_DIR}/consensus/election.c
    ${CMAKE_CURRENT_SOURCE_DIR}/consensus/metrics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/consensus/coordinator.c
    # nat/
    ${CMAKE_CURRENT_SOURCE_DIR}/nat/upnp.c
    # mdns/
    ${CMAKE_CURRENT_SOURCE_DIR}/mdns/mdns.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mdns/discovery_tui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mdns/discovery.c
)

# Third-party code properties (mdns)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/mdns/mdns.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-unused-but-set-variable"
    SKIP_LINTING ON
    DIRECTORY ${CMAKE_SOURCE_DIR}
)

set(NETWORK_SRCS ${NETWORK_SRCS} PARENT_SCOPE)
