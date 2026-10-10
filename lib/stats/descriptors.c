#include <ascii-chat/stats/stats.h>
#include <stddef.h>

static const stats_descriptor_t counter_descriptors[STATS_COUNTER_COUNT] = {
    [STATS_COUNTER_FRAMES_CAPTURED] = {"frames_captured", "frames", "Frames acquired from a media source"},
    [STATS_COUNTER_FRAMES_CONVERTED] = {"frames_converted", "frames", "Successful ASCII frame conversions"},
    [STATS_COUNTER_FRAMES_ENCODED] = {"frames_encoded", "frames", "Successfully encoded video frames"},
    [STATS_COUNTER_FRAMES_DECODED] = {"frames_decoded", "frames", "Successfully decoded video frames"},
    [STATS_COUNTER_FRAMES_PRESENTED] = {"frames_presented", "frames", "Complete media frames written to the terminal"},
    [STATS_COUNTER_FRAMES_SKIPPED] = {"frames_skipped", "frames", "Frames deliberately skipped by policy"},
    [STATS_COUNTER_FRAMES_DROPPED] = {"frames_dropped", "frames",
                                      "Frames lost to capacity limits or processing failures"},
    [STATS_COUNTER_PACKETS_SENT] = {"packets_sent", "packets", "Successfully sent application packets"},
    [STATS_COUNTER_PACKETS_RECEIVED] = {"packets_received", "packets", "Received application packets"},
    [STATS_COUNTER_BYTES_SENT] = {"bytes_sent", "bytes",
                                  "Sent ACIP header and payload bytes, excluding transport overhead"},
    [STATS_COUNTER_BYTES_RECEIVED] = {"bytes_received", "bytes",
                                      "Received ACIP header and payload bytes, excluding transport overhead"},
    [STATS_COUNTER_SEND_ERRORS] = {"send_errors", "errors", "Failed application send operations"},
    [STATS_COUNTER_RECEIVE_ERRORS] = {"receive_errors", "errors", "Failed application receive operations"},
    [STATS_COUNTER_AUDIO_UNDERRUNS] = {"audio_underruns", "events", "Playback buffer underrun events"},
    [STATS_COUNTER_AUDIO_OVERRUNS] = {"audio_overruns", "events", "Audio buffer overrun events"},
    [STATS_COUNTER_SESSION_CREATES] = {"session_creates", "sessions", "Successful session creations"},
    [STATS_COUNTER_SESSION_JOINS] = {"session_joins", "joins", "Successful session joins"},
    [STATS_COUNTER_SESSION_LOOKUPS] = {"session_lookups", "requests", "Successful session lookups"},
    [STATS_COUNTER_REQUEST_FAILURES] = {"request_failures", "requests", "Failed service requests"},
    [STATS_COUNTER_RATE_LIMIT_REJECTIONS] = {"rate_limit_rejections", "requests", "Requests rejected by rate limiting"},
    [STATS_COUNTER_SESSION_EXPIRATIONS] = {"session_expirations", "sessions", "Sessions removed by expiration"},
    [STATS_COUNTER_HOST_MIGRATIONS] = {"host_migrations", "events", "Completed host migrations"},
};

static const stats_descriptor_t gauge_descriptors[STATS_GAUGE_COUNT] = {
    [STATS_GAUGE_CONNECTIONS_ACTIVE] = {"connections_active", "connections", "Currently connected peers"},
    [STATS_GAUGE_SESSIONS_ACTIVE] = {"sessions_active", "sessions", "Currently active sessions"},
    [STATS_GAUGE_PARTICIPANTS_ACTIVE] = {"participants_active", "participants", "Currently active participants"},
    [STATS_GAUGE_VIDEO_QUEUE_DEPTH] = {"video_queue_depth", "frames", "Frames currently queued for delivery"},
    [STATS_GAUGE_AUDIO_BUFFERED_SAMPLES] = {"audio_buffered_samples", "samples",
                                            "Buffered audio sample frames per channel"},
};

static const stats_descriptor_t duration_descriptors[STATS_DURATION_COUNT] = {
    [STATS_DURATION_CAPTURE] = {"capture", "ns", "Media acquisition attempt, including source wait"},
    [STATS_DURATION_ASCII_CONVERT] = {"ascii_convert", "ns", "ASCII conversion attempt, excluding capture and output"},
    [STATS_DURATION_ENCODE] = {"encode", "ns", "Video codec encode call, excluding queue wait"},
    [STATS_DURATION_DECODE] = {"decode", "ns", "Video codec decode call, excluding queue wait"},
    [STATS_DURATION_AUDIO_MIX] = {"audio_mix", "ns", "Audio mixing attempt, excluding playback"},
    [STATS_DURATION_TERMINAL_WRITE] = {"terminal_write", "ns", "Media frame write attempt, including output blocking"},
    [STATS_DURATION_CONNECTION_SETUP] = {"connection_setup", "ns", "Connection attempt through readiness or failure"},
    [STATS_DURATION_REQUEST] = {"request", "ns", "Service request handling through response submission or failure"},
    [STATS_DURATION_RTT] = {"rtt", "ns", "Matched probe send-to-response elapsed time; excludes unanswered probes"},
};

const stats_descriptor_t *stats_counter_descriptor(stats_counter_id_t id) {
  return (unsigned)id < STATS_COUNTER_COUNT ? &counter_descriptors[id] : NULL;
}

const stats_descriptor_t *stats_gauge_descriptor(stats_gauge_id_t id) {
  return (unsigned)id < STATS_GAUGE_COUNT ? &gauge_descriptors[id] : NULL;
}

const stats_descriptor_t *stats_duration_descriptor(stats_duration_id_t id) {
  return (unsigned)id < STATS_DURATION_COUNT ? &duration_descriptors[id] : NULL;
}
