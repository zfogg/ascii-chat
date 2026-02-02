#pragma once

/**
 * @file wav_writer.h
 * @brief Simple WAV file writer for audio debugging
 * @ingroup audio_debug
 *
 * Provides utilities to dump audio buffers to WAV files for debugging.
 * Enable with environment variable: ASCIICHAT_DUMP_AUDIO=1
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WAV file writer context
 */
typedef struct {
  FILE *file;
  size_t samples_written;
  int sample_rate;
  int channels;
} wav_writer_t;

/**
 * @brief Open WAV file for writing
 * @param filepath Path to WAV file to create
 * @param sample_rate Sample rate (e.g., 44100)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @return WAV writer context or NULL on error
 */
wav_writer_t *wav_writer_open(const char *filepath, int sample_rate, int channels);

/**
 * @brief Write audio samples to WAV file
 * @param writer WAV writer context
 * @param samples Float samples in range [-1.0, 1.0]
 * @param num_samples Number of samples to write
 * @return 0 on success, -1 on error
 */
int wav_writer_write(wav_writer_t *writer, const float *samples, int num_samples);

/**
 * @brief Close WAV file and finalize header
 * @param writer WAV writer context
 */
void wav_writer_close(wav_writer_t *writer);

/**
 * @brief Check if audio dumping is enabled via environment
 * @return true if ASCIICHAT_DUMP_AUDIO=1
 */
bool wav_dump_enabled(void);

#ifdef __cplusplus
}
#endif
