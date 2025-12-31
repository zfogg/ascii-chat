# =============================================================================
# Module 7: Network (changes weekly)
# =============================================================================
# Network protocol, packet handling, and communication

set(NETWORK_SRCS
    lib/network/network.c
    lib/network/packet.c
    lib/network/packet_parsing.c
    lib/network/av.c
    lib/network/compression.c
    lib/network/crc32.c
    lib/network/packet_queue.c
    lib/network/http_client.c
    lib/network/tcp_server.c
    lib/network/errors.c
    # Rate limiting library (backend abstraction)
    lib/network/rate_limit/rate_limit.c
    lib/network/rate_limit/memory.c
    lib/network/rate_limit/sqlite.c
)
