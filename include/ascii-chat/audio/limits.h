#pragma once

/** Audio rate and channel configuration shared by capture, mixing, and codecs. */
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 2
/** Maximum channels exposed by a native audio device. */
#define AUDIO_DEVICE_MAX_CHANNELS 64
/** Capture uses 10-ms blocks for WebRTC AEC3. */
#define AUDIO_CAPTURE_FRAME_MS 10
#define AUDIO_FRAMES_PER_BUFFER (AUDIO_SAMPLE_RATE * AUDIO_CAPTURE_FRAME_MS / 1000)
#define AUDIO_BUFFER_SIZE (AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS)

/** Opus capture and mixing use 20-ms frames; sample counts below are per channel. */
#define CLIENT_AUDIO_PIPELINE_FRAME_MS 20
#define OPUS_FRAME_SAMPLES (AUDIO_SAMPLE_RATE * CLIENT_AUDIO_PIPELINE_FRAME_MS / 1000)
/** Echo-reference storage holds half a second at the configured rate. */
#define CLIENT_AUDIO_PIPELINE_ECHO_REF_SIZE (AUDIO_SAMPLE_RATE / 2)
/** Existing encoder byte budgets; these are not codec-wide packet maxima. */
#define CLIENT_AUDIO_PIPELINE_MAX_OPUS_PACKET 4000
#define AUDIO_OPUS_SESSION_PACKET_CAPACITY 1000
#define AUDIO_OPUS_SERVER_PACKET_CAPACITY 1024
/** Decoder storage permits packets representing up to 120 ms per channel at the configured rate. */
#define AUDIO_OPUS_MAX_DECODE_MS 120
#define AUDIO_OPUS_MAX_DECODE_SAMPLES (AUDIO_SAMPLE_RATE * AUDIO_OPUS_MAX_DECODE_MS / 1000)
