#pragma once

/** Shared mono audio framing contract used by capture, mixing, and Opus. */
#define AUDIO_DEFAULT_SAMPLE_RATE 48000
#define AUDIO_CAPTURE_FRAME_MS 10
#define AUDIO_CAPTURE_FRAME_SAMPLES (AUDIO_DEFAULT_SAMPLE_RATE * AUDIO_CAPTURE_FRAME_MS / 1000)
#define AUDIO_OPUS_FRAME_MS 20
#define AUDIO_OPUS_FRAME_SAMPLES (AUDIO_DEFAULT_SAMPLE_RATE * AUDIO_OPUS_FRAME_MS / 1000)
/** Decoder storage permits packets representing up to 120 ms at the default rate. */
#define AUDIO_OPUS_MAX_DECODE_MS 120
#define AUDIO_OPUS_MAX_DECODE_SAMPLES (AUDIO_DEFAULT_SAMPLE_RATE * AUDIO_OPUS_MAX_DECODE_MS / 1000)
/** Existing encoded-output budgets in bytes, distinct from PCM sample counts.
 * Encoders receive the actual budget; these are not codec-wide packet maxima. */
#define AUDIO_OPUS_PIPELINE_PACKET_CAPACITY 4000
#define AUDIO_OPUS_SESSION_PACKET_CAPACITY 1000
#define AUDIO_OPUS_SERVER_PACKET_CAPACITY 1024
