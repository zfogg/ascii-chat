#import <AVFoundation/AVFoundation.h>

#include <stdlib.h>
#include <string.h>
#include <ascii-chat/audio/ios_portaudio.h>

struct PaStream {
  PaStreamCallback *callback;
  void *user_data;
  unsigned long frames_per_buffer;
};

static const PaDeviceInfo g_ios_device = {
    .structVersion = 1,
    .name = "iOS Audio",
    .maxInputChannels = 2,
    .maxOutputChannels = 2,
    .defaultLowInputLatency = 0.01,
    .defaultLowOutputLatency = 0.01,
    .defaultSampleRate = 48000.0,
};

PaError Pa_Initialize(void) { return paNoError; }
PaError Pa_Terminate(void) { return paNoError; }
const char *Pa_GetErrorText(PaError error) {
  (void)error;
  return "iOS audio backend";
}
PaDeviceIndex Pa_GetDefaultInputDevice(void) { return 0; }
PaDeviceIndex Pa_GetDefaultOutputDevice(void) { return 0; }
int Pa_GetDeviceCount(void) { return 1; }
const PaDeviceInfo *Pa_GetDeviceInfo(PaDeviceIndex device) { return device == 0 ? &g_ios_device : NULL; }

PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output,
                      double sample_rate, unsigned long frames_per_buffer, unsigned long flags,
                      PaStreamCallback *callback, void *user_data) {
  (void)input;
  (void)output;
  (void)sample_rate;
  (void)flags;
  if (!stream)
    return -1;
  PaStream *opened = calloc(1, sizeof(*opened));
  if (!opened)
    return -1;
  opened->callback = callback;
  opened->user_data = user_data;
  opened->frames_per_buffer = frames_per_buffer;
  *stream = opened;
  return paNoError;
}

PaError Pa_StartStream(PaStream *stream) {
  (void)stream;
  AVAudioSession *session = [AVAudioSession sharedInstance];
  [session setCategory:AVAudioSessionCategoryPlayAndRecord error:NULL];
  [session setActive:YES error:NULL];
  return paNoError;
}
PaError Pa_StopStream(PaStream *stream) { (void)stream; return paNoError; }
PaError Pa_CloseStream(PaStream *stream) { free(stream); return paNoError; }
PaError Pa_WriteStream(PaStream *stream, const void *buffer, unsigned long frames) {
  (void)stream;
  (void)buffer;
  (void)frames;
  return paNoError;
}
