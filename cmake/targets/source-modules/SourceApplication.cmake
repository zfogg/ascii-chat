# =============================================================================
# Module 10: Application Sources (main executable)
# =============================================================================
# Main executable and mode-specific implementations (server, client, mirror)

set(APP_SRCS
    src/main.c
    # Server mode sources
    src/server/main.c
    src/server/client.c
    src/server/protocol.c
    src/server/crypto.c
    src/server/stream.c
    src/server/render.c
    src/server/stats.c
    # Client mode sources
    src/client/main.c
    src/client/server.c
    src/client/protocol.c
    src/client/crypto.c
    src/client/display.c
    src/client/capture.c
    src/client/audio.c
    src/client/keepalive.c
    # Mirror mode sources
    src/mirror/main.c
)

# =============================================================================
# Module 11: Discovery Server (ACDS) Sources
# =============================================================================
set(ACDS_SRCS
    src/acds/main.c
    src/acds/server.c
    src/acds/session.c
    src/acds/signaling.c
    src/acds/database.c
    src/acds/identity.c
    src/acds/strings.c
)

# =============================================================================
# Module 12: Panic Instrumentation Runtime
# =============================================================================
set(TOOLING_PANIC_SRCS
    lib/tooling/panic/instrument_log.c
    lib/tooling/panic/instrument_cov.c
)

set(TOOLING_PANIC_REPORT_SRCS
    src/tooling/panic/report.c
)
