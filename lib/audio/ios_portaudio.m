#import <AVFoundation/AVFoundation.h>

#include <stdlib.h>
#include <string.h>
#include <ascii-chat/audio/ios_portaudio.h>

struct PaStream {
  AVAudioEngine *engine;
  AVAudioPlayerNode *player;
  PaStreamCallback *callback;
  void *user_data;
  unsigned long frames_per_buffer;
  double sample_rate;
  int input_channels;
  int output_channels;
  BOOL active;
};

int asciichat_ios_audio_authorized(void) {
  return [AVAudioSession.sharedInstance recordPermission] == AVAudioSessionRecordPermissionGranted;
}

void asciichat_ios_audio_request_access(void) {
  [AVAudioSession.sharedInstance requestRecordPermission:^(BOOL granted) {
    (void)granted;
  }];
}

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
  (void)flags;
  if (!stream)
    return -1;
  PaStream *opened = calloc(1, sizeof(*opened));
  if (!opened)
    return -1;
  opened->callback = callback;
  opened->user_data = user_data;
  opened->frames_per_buffer = frames_per_buffer;
  opened->sample_rate = sample_rate;
  opened->input_channels = input ? input->channelCount : 0;
  opened->output_channels = output ? output->channelCount : 0;
  opened->engine = [AVAudioEngine new];
  if (opened->output_channels > 0)
    opened->player = [AVAudioPlayerNode new];
  *stream = opened;
  return paNoError;
}

PaError Pa_StartStream(PaStream *stream) {
  if (!stream || !stream->engine)
    return -1;
  AVAudioSession *session = [AVAudioSession sharedInstance];
  [session setCategory:AVAudioSessionCategoryPlayAndRecord error:NULL];
  [session setMode:AVAudioSessionModeVideoChat error:NULL];
  [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:NULL];
  [session setActive:YES error:NULL];
  if (stream->input_channels > 0 && stream->callback) {
    AVAudioInputNode *input = stream->engine.inputNode;
    AVAudioFormat *format = [input inputFormatForBus:0];
    [input installTapOnBus:0 bufferSize:(AVAudioFrameCount)stream->frames_per_buffer format:format
                     block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
      (void)when;
      PaStreamCallbackTimeInfo time_info = {0, 0, 0};
      stream->callback(buffer.floatChannelData, NULL, buffer.frameLength, &time_info, 0, stream->user_data);
    }];
  }
  if (stream->player) {
    [stream->engine attachNode:stream->player];
    [stream->engine connect:stream->player to:stream->engine.mainMixerNode format:nil];
  }
  NSError *error = nil;
  [stream->engine startAndReturnError:&error];
  if (error)
    return -1;
  stream->active = YES;
  return paNoError;
}
PaError Pa_StopStream(PaStream *stream) {
  if (!stream) return -1;
  [stream->engine.inputNode removeTapOnBus:0];
  [stream->engine stop];
  stream->active = NO;
  return paNoError;
}
PaError Pa_CloseStream(PaStream *stream) {
  if (!stream) return paNoError;
  if (stream->active) Pa_StopStream(stream);
  free(stream);
  return paNoError;
}
PaError Pa_WriteStream(PaStream *stream, const void *buffer, unsigned long frames) {
  if (!stream || !stream->player || !buffer || frames == 0)
    return -1;
  AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:stream->sample_rate
                                                                            channels:(AVAudioChannelCount)stream->output_channels];
  AVAudioPCMBuffer *audio = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:(AVAudioFrameCount)frames];
  audio.frameLength = (AVAudioFrameCount)frames;
  const float *samples = buffer;
  for (int channel = 0; channel < stream->output_channels; ++channel)
    memcpy(audio.floatChannelData[channel], samples + (size_t)channel * frames, frames * sizeof(float));
  [stream->player scheduleBuffer:audio completionHandler:nil];
  return paNoError;
}
