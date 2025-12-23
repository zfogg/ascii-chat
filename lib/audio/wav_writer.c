/**
 * @file wav_writer.c
 * @brief Simple WAV file writer implementation for audio debugging
 */

#include "audio/wav_writer.h"
#include "common.h"
#include "platform/string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// WAV file header structures
#pragma pack(push, 1)
typedef struct {
  char riff[4];       // "RIFF"
  uint32_t file_size; // File size - 8
  char wave[4];       // "WAVE"
} wav_riff_header_t;

typedef struct {
  char fmt[4];              // "fmt "
  uint32_t chunk_size;      // 16 for PCM
  uint16_t audio_format;    // 3 = IEEE float
  uint16_t num_channels;    // 1 = mono, 2 = stereo
  uint32_t sample_rate;     // 44100, etc.
  uint32_t byte_rate;       // sample_rate * num_channels * bytes_per_sample
  uint16_t block_align;     // num_channels * bytes_per_sample
  uint16_t bits_per_sample; // 32 for float
} wav_fmt_chunk_t;

typedef struct {
  char data[4];       // "data"
  uint32_t data_size; // Size of data section
} wav_data_header_t;
#pragma pack(pop)

wav_writer_t *wav_writer_open(const char *filepath, int sample_rate, int channels) {
  if (!filepath || sample_rate <= 0 || channels <= 0 || channels > 2) {
    return NULL;
  }

  wav_writer_t *writer = SAFE_MALLOC(sizeof(wav_writer_t), wav_writer_t *);
  if (!writer) {
    return NULL;
  }

  writer->file = fopen(filepath, "wb");
  if (!writer->file) {
    SAFE_FREE(writer);
    return NULL;
  }

  writer->samples_written = 0;
  writer->sample_rate = sample_rate;
  writer->channels = channels;

  // Write WAV header (we'll update sizes when closing)
  wav_riff_header_t riff = {.riff = {'R', 'I', 'F', 'F'},
                            .file_size = 0, // Will update on close
                            .wave = {'W', 'A', 'V', 'E'}};

  wav_fmt_chunk_t fmt = {.fmt = {'f', 'm', 't', ' '},
                         .chunk_size = 16,
                         .audio_format = 3, // IEEE float
                         .num_channels = (uint16_t)channels,
                         .sample_rate = (uint32_t)sample_rate,
                         .byte_rate = (uint32_t)(sample_rate * channels * sizeof(float)),
                         .block_align = (uint16_t)(channels * sizeof(float)),
                         .bits_per_sample = 32};

  wav_data_header_t data = {
      .data = {'d', 'a', 't', 'a'},
      .data_size = 0 // Will update on close
  };

  fwrite(&riff, sizeof(riff), 1, writer->file);
  fwrite(&fmt, sizeof(fmt), 1, writer->file);
  fwrite(&data, sizeof(data), 1, writer->file);

  return writer;
}

int wav_writer_write(wav_writer_t *writer, const float *samples, int num_samples) {
  if (!writer || !writer->file || !samples || num_samples <= 0) {
    return -1;
  }

  size_t written = fwrite(samples, sizeof(float), num_samples, writer->file);
  if (written != (size_t)num_samples) {
    return -1;
  }

  writer->samples_written += num_samples;
  return 0;
}

void wav_writer_close(wav_writer_t *writer) {
  if (!writer) {
    return;
  }

  if (writer->file) {
    // Update file size and data size in header
    uint32_t data_size = (uint32_t)(writer->samples_written * sizeof(float));
    uint32_t file_size = data_size + 36; // 36 = header size without RIFF chunk

    // Seek to file size field (offset 4)
    fseek(writer->file, 4, SEEK_SET);
    fwrite(&file_size, sizeof(uint32_t), 1, writer->file);

    // Seek to data size field (offset 40)
    fseek(writer->file, 40, SEEK_SET);
    fwrite(&data_size, sizeof(uint32_t), 1, writer->file);

    fclose(writer->file);
  }

  SAFE_FREE(writer);
}

bool wav_dump_enabled(void) {
  const char *env = SAFE_GETENV("ASCIICHAT_DUMP_AUDIO");
  return env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0);
}
