#import <AVFoundation/AVFoundation.h>

#include <ascii-chat/audio/ios_portaudio.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/memory.h>
#include <string.h>

struct PaStream {
  AVAudioEngine *engine;
  AVAudioPlayerNode *player;
  PaStreamCallback *callback;
  void *user_data;
  unsigned long frames_per_buffer;
  double sample_rate;
  int input_channels;
  int output_channels;
  float *input_interleaved;
  size_t input_capacity_frames;
  BOOL active;
  id interruption_observer;
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

PaError Pa_Initialize(void) {
  return paNoError;
}
PaError Pa_Terminate(void) {
  return paNoError;
}
const char *Pa_GetErrorText(PaError error) {
  (void)error;
  return "iOS audio backend";
}
PaDeviceIndex Pa_GetDefaultInputDevice(void) {
  return 0;
}
PaDeviceIndex Pa_GetDefaultOutputDevice(void) {
  return 0;
}
int Pa_GetDeviceCount(void) {
  return 1;
}
const PaDeviceInfo *Pa_GetDeviceInfo(PaDeviceIndex device) {
  return device == 0 ? &g_ios_device : NULL;
}

PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output,
                      double sample_rate, unsigned long frames_per_buffer, unsigned long flags,
                      PaStreamCallback *callback, void *user_data) {
  (void)input;
  (void)flags;
  if (!stream)
    return -1;
  PaStream *opened = SAFE_CALLOC(1, sizeof(*opened), PaStream *);
  opened->callback = callback;
  opened->user_data = user_data;
  opened->frames_per_buffer = frames_per_buffer;
  opened->sample_rate = sample_rate;
  opened->input_channels = input ? input->channelCount : 0;
  opened->output_channels = output ? output->channelCount : 0;
  opened->input_capacity_frames = frames_per_buffer ? frames_per_buffer : 1024;
  if (opened->input_channels > 0)
    opened->input_interleaved =
        SAFE_CALLOC(opened->input_capacity_frames, (size_t)opened->input_channels * sizeof(float), float *);
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
  stream->interruption_observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:AVAudioSessionInterruptionNotification
                  object:session
                   queue:nil
              usingBlock:^(NSNotification *note) {
                NSNumber *type = note.userInfo[AVAudioSessionInterruptionTypeKey];
                if (type.integerValue == AVAudioSessionInterruptionTypeBegan) {
                  [stream->engine pause];
                  stream->active = NO;
                } else if (stream->engine && !stream->active) {
                  NSError *resume_error = nil;
                  [session setActive:YES error:&resume_error];
                  if (!resume_error && [stream->engine startAndReturnError:&resume_error] == YES)
                    stream->active = YES;
                }
              }];
  if (stream->input_channels > 0 && stream->callback) {
    AVAudioInputNode *input = stream->engine.inputNode;
    AVAudioFormat *format = [input inputFormatForBus:0];
    [input installTapOnBus:0
                bufferSize:(AVAudioFrameCount)stream->frames_per_buffer
                    format:format
                     block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
                       (void)when;
                       PaStreamCallbackTimeInfo time_info = {0, 0, 0};
                       size_t offset = 0;
                       while (offset < buffer.frameLength) {
                         const size_t frames = MIN(stream->input_capacity_frames, buffer.frameLength - offset);
                         for (size_t frame = 0; frame < frames; ++frame)
                           for (int channel = 0; channel < stream->input_channels; ++channel)
                             stream->input_interleaved[frame * (size_t)stream->input_channels + (size_t)channel] =
                                 buffer.floatChannelData[channel][offset + frame];
                         stream->callback(stream->input_interleaved, NULL, frames, &time_info, 0, stream->user_data);
                         offset += frames;
                       }
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
  if (!stream)
    return -1;
  [stream->engine.inputNode removeTapOnBus:0];
  [stream->engine stop];
  if (stream->interruption_observer) {
    [[NSNotificationCenter defaultCenter] removeObserver:stream->interruption_observer];
    stream->interruption_observer = nil;
  }
  stream->active = NO;
  return paNoError;
}
PaError Pa_CloseStream(PaStream *stream) {
  if (!stream)
    return paNoError;
  if (stream->active)
    Pa_StopStream(stream);
  SAFE_FREE(stream->input_interleaved);
  SAFE_FREE(stream);
  return paNoError;
}
PaError Pa_WriteStream(PaStream *stream, const void *buffer, unsigned long frames) {
  if (!stream || !stream->player || !buffer || frames == 0)
    return -1;
  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:stream->sample_rate
                                                     channels:(AVAudioChannelCount)stream->output_channels];
  AVAudioPCMBuffer *audio = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:(AVAudioFrameCount)frames];
  audio.frameLength = (AVAudioFrameCount)frames;
  const float *samples = buffer;
  for (size_t frame = 0; frame < frames; ++frame)
    for (int channel = 0; channel < stream->output_channels; ++channel)
      audio.floatChannelData[channel][frame] = samples[frame * (size_t)stream->output_channels + (size_t)channel];
  [stream->player scheduleBuffer:audio completionHandler:nil];
  return paNoError;
}
