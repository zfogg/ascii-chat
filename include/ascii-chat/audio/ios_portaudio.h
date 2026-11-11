#pragma once

#include <stddef.h>

typedef int PaError;
typedef int PaDeviceIndex;
typedef unsigned long PaStreamCallbackFlags;
typedef struct PaStream PaStream;
typedef struct PaStreamCallbackTimeInfo {
  double inputBufferAdcTime;
  double currentTime;
  double outputBufferDacTime;
} PaStreamCallbackTimeInfo;
typedef int PaStreamCallback(const void *, void *, unsigned long, const PaStreamCallbackTimeInfo *,
                             PaStreamCallbackFlags, void *);
typedef struct PaDeviceInfo {
  int structVersion;
  const char *name;
  int maxInputChannels;
  int maxOutputChannels;
  double defaultLowInputLatency;
  double defaultLowOutputLatency;
  double defaultSampleRate;
} PaDeviceInfo;
typedef struct PaStreamParameters {
  PaDeviceIndex device;
  int channelCount;
  unsigned long sampleFormat;
  double suggestedLatency;
  void *hostApiSpecificStreamInfo;
} PaStreamParameters;

enum {
  paNoError = 0,
  paNoDevice = -1,
  paInvalidChannelCount = -9998,
  paInvalidSampleRate = -9997,
  paInvalidDevice = -9996,
  paFloat32 = 1,
  paClipOff = 1UL << 5,
  paContinue = 0,
  paComplete = 1,
  paAbort = 2,
  paInputOverflow = 1UL << 0,
  paOutputUnderflow = 1UL << 1
};

PaError Pa_Initialize(void);
PaError Pa_Terminate(void);
const char *Pa_GetErrorText(PaError error);
PaDeviceIndex Pa_GetDefaultInputDevice(void);
PaDeviceIndex Pa_GetDefaultOutputDevice(void);
int Pa_GetDeviceCount(void);
const PaDeviceInfo *Pa_GetDeviceInfo(PaDeviceIndex device);
PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output,
                      double sample_rate, unsigned long frames_per_buffer, unsigned long flags,
                      PaStreamCallback *callback, void *user_data);
PaError Pa_StartStream(PaStream *stream);
PaError Pa_StopStream(PaStream *stream);
PaError Pa_CloseStream(PaStream *stream);
PaError Pa_WriteStream(PaStream *stream, const void *buffer, unsigned long frames);
int asciichat_ios_audio_device_count(void);
const char *asciichat_ios_audio_device_name(int index);
int asciichat_ios_audio_select_device(int index);
