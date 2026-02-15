# src/ - Application mode entry points

set(APP_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    # Server mode
    ${CMAKE_CURRENT_SOURCE_DIR}/server/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/stream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/render.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/stats.c
    # Client mode
    ${CMAKE_CURRENT_SOURCE_DIR}/client/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/capture.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/keepalive.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/webrtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/connection_attempt.c
    # Mirror mode
    ${CMAKE_CURRENT_SOURCE_DIR}/mirror/main.c
    # Discovery-service mode
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery-service/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery-service/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery-service/signaling.c
    # Discovery mode
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/main.c
)

set(APP_SRCS ${APP_SRCS} PARENT_SCOPE)
