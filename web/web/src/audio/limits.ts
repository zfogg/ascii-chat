/** Native audio/limits.h framing contract, in samples per mono channel. */
export const AUDIO_SAMPLE_RATE = 48000;
export const OPUS_FRAME_MS = 20;
export const OPUS_FRAME_SAMPLES = (AUDIO_SAMPLE_RATE * OPUS_FRAME_MS) / 1000;
/** Existing encoder scratch budget, in encoded bytes. */
export const OPUS_ENCODE_BUFFER_SIZE = 4000;
export const OPUS_BATCH_HEADER_SIZE = 16;
export const OPUS_FRAME_LENGTH_SIZE = Uint16Array.BYTES_PER_ELEMENT;
export const OPUS_SINGLE_FRAME_HEADER_SIZE =
  OPUS_BATCH_HEADER_SIZE + OPUS_FRAME_LENGTH_SIZE;
