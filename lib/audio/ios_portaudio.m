#import <AVFoundation/AVFoundation.h>

#include <ascii-chat/audio/ios_portaudio.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/memory.h>
#include <stdio.h>
#include <string.h>

#define IOS_AUDIO_MAX_DEVICES 16

typedef struct {
  PaDeviceInfo info;
  AVAudioSessionPortDescription *input_port;
  AVAudioSessionPortDescription *output_port;
  char name[128];
} ios_audio_device_t;

static ios_audio_device_t g_ios_devices[IOS_AUDIO_MAX_DEVICES];
static int g_ios_device_count;
static NSLock *g_ios_device_lock;
static id g_ios_route_observer;
static int g_ios_selected_device = -1;

static BOOL ios_audio_device_supports_input(PaDeviceIndex index) {
  return index >= 0 && index < g_ios_device_count && g_ios_devices[index].info.maxInputChannels > 0;
}

static void ios_audio_refresh_devices(void) {
  AVAudioSession *session = AVAudioSession.sharedInstance;
  [session setCategory:AVAudioSessionCategoryPlayAndRecord
                  mode:AVAudioSessionModeVideoChat
               options:AVAudioSessionCategoryOptionAllowBluetoothHFP | AVAudioSessionCategoryOptionAllowBluetoothA2DP
                 error:NULL];
  memset(g_ios_devices, 0, sizeof(g_ios_devices));
  g_ios_device_count = 0;

  NSArray<AVAudioSessionPortDescription *> *inputs = session.availableInputs;
  for (AVAudioSessionPortDescription *input in inputs) {
    if (g_ios_device_count >= IOS_AUDIO_MAX_DEVICES)
      break;
    ios_audio_device_t *device = &g_ios_devices[g_ios_device_count++];
    device->input_port = input;
    device->output_port = input.portType == AVAudioSessionPortBuiltInMic ? nil : input;
    snprintf(device->name, sizeof(device->name), "%s", input.portName.UTF8String ?: "iOS Audio");
    device->info = (PaDeviceInfo){.structVersion = 1,
                                  .name = device->name,
                                  .maxInputChannels = (int)input.channels.count,
                                  .maxOutputChannels = input.portType == AVAudioSessionPortBuiltInMic ? 2 : 0,
                                  .defaultLowInputLatency = 0.01,
                                  .defaultLowOutputLatency = 0.01,
                                  .defaultSampleRate = 48000.0};
  }

  NSArray<AVAudioSessionPortDescription *> *outputs = session.currentRoute.outputs;
  for (AVAudioSessionPortDescription *output in outputs) {
    if (g_ios_device_count >= IOS_AUDIO_MAX_DEVICES)
      break;
    BOOL already_listed = NO;
    for (int index = 0; index < g_ios_device_count; ++index) {
      AVAudioSessionPortDescription *input = g_ios_devices[index].input_port;
      if (input && [input.UID isEqualToString:output.UID]) {
        g_ios_devices[index].output_port = output;
        g_ios_devices[index].info.maxOutputChannels = (int)output.channels.count;
        already_listed = YES;
        break;
      }
    }
    if (already_listed)
      continue;
    ios_audio_device_t *device = &g_ios_devices[g_ios_device_count++];
    device->output_port = output;
    snprintf(device->name, sizeof(device->name), "%s", output.portName.UTF8String ?: "iOS Output");
    device->info = (PaDeviceInfo){.structVersion = 1,
                                  .name = device->name,
                                  .maxInputChannels = 0,
                                  .maxOutputChannels = (int)output.channels.count,
                                  .defaultLowInputLatency = 0.01,
                                  .defaultLowOutputLatency = 0.01,
                                  .defaultSampleRate = 48000.0};
  }

  if (g_ios_device_count == 0 && g_ios_device_count < IOS_AUDIO_MAX_DEVICES) {
    ios_audio_device_t *device = &g_ios_devices[g_ios_device_count++];
    snprintf(device->name, sizeof(device->name), "Apple Built-in Audio");
    device->info = (PaDeviceInfo){.structVersion = 1,
                                  .name = device->name,
                                  .maxInputChannels = 2,
                                  .maxOutputChannels = 2,
                                  .defaultLowInputLatency = 0.01,
                                  .defaultLowOutputLatency = 0.01,
                                  .defaultSampleRate = 48000.0};
  }
}

struct PaStream {
  AVAudioEngine *engine;
  AVAudioPlayerNode *player;
  AVAudioSourceNode *source;
  PaStreamCallback *callback;
  void *user_data;
  unsigned long frames_per_buffer;
  double sample_rate;
  int input_channels;
  int output_channels;
  PaDeviceIndex device_index;
  float *input_interleaved;
  size_t input_capacity_frames;
  float *output_interleaved;
  size_t output_capacity_frames;
  BOOL active;
  unsigned long queued_frames;
  NSLock *queue_lock;
  id interruption_observer;
};

int asciichat_ios_audio_authorized(void) {
#if TARGET_OS_SIMULATOR
  return 1;
#else
  return [AVAudioSession.sharedInstance recordPermission] == AVAudioSessionRecordPermissionGranted;
#endif
}

void asciichat_ios_audio_request_access(void) {
#if TARGET_OS_SIMULATOR
  return;
#else
  [AVAudioSession.sharedInstance requestRecordPermission:^(BOOL granted) {
    (void)granted;
  }];
#endif
}

PaError Pa_Initialize(void) {
  if (!g_ios_device_lock)
    g_ios_device_lock = [NSLock new];
  ios_audio_refresh_devices();
  if (!g_ios_route_observer) {
    g_ios_route_observer =
        [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification
                                                          object:AVAudioSession.sharedInstance
                                                           queue:nil
                                                      usingBlock:^(NSNotification *note) {
                                                        (void)note;
                                                        [g_ios_device_lock lock];
                                                        ios_audio_refresh_devices();
                                                        [g_ios_device_lock unlock];
                                                      }];
  }
  return paNoError;
}
PaError Pa_Terminate(void) {
  if (g_ios_route_observer) {
    [[NSNotificationCenter defaultCenter] removeObserver:g_ios_route_observer];
    g_ios_route_observer = nil;
  }
  g_ios_selected_device = -1;
  g_ios_device_count = 0;
  memset(g_ios_devices, 0, sizeof(g_ios_devices));
  return paNoError;
}
const char *Pa_GetErrorText(PaError error) {
  (void)error;
  return "iOS audio backend";
}
PaDeviceIndex Pa_GetDefaultInputDevice(void) {
  ios_audio_refresh_devices();
  for (int index = 0; index < g_ios_device_count; ++index)
    if (g_ios_devices[index].info.maxInputChannels > 0)
      return index;
  return 0;
}
PaDeviceIndex Pa_GetDefaultOutputDevice(void) {
  ios_audio_refresh_devices();
  for (int index = 0; index < g_ios_device_count; ++index)
    if (g_ios_devices[index].info.maxOutputChannels > 0)
      return index;
  return 0;
}
int Pa_GetDeviceCount(void) {
  ios_audio_refresh_devices();
  return g_ios_device_count;
}
const PaDeviceInfo *Pa_GetDeviceInfo(PaDeviceIndex device) {
  ios_audio_refresh_devices();
  return device >= 0 && device < g_ios_device_count ? &g_ios_devices[device].info : NULL;
}

PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output,
                      double sample_rate, unsigned long frames_per_buffer, unsigned long flags,
                      PaStreamCallback *callback, void *user_data) {
  (void)flags;
  if (!stream)
    return -1;
  ios_audio_refresh_devices();
  PaStream *opened = SAFE_CALLOC(1, sizeof(*opened), PaStream *);
  opened->callback = callback;
  opened->user_data = user_data;
  opened->frames_per_buffer = frames_per_buffer;
  opened->sample_rate = sample_rate;
  opened->input_channels = input ? input->channelCount : 0;
  opened->output_channels = output ? output->channelCount : 0;
  opened->device_index = ios_audio_device_supports_input(g_ios_selected_device)    ? g_ios_selected_device
                         : input && ios_audio_device_supports_input(input->device) ? input->device
                                                                                   : Pa_GetDefaultInputDevice();
  if (opened->device_index < 0 || opened->device_index >= g_ios_device_count) {
    SAFE_FREE(opened);
    return paInvalidDevice;
  }
  opened->input_capacity_frames = frames_per_buffer ? frames_per_buffer : 1024;
  opened->queue_lock = [NSLock new];
  if (opened->input_channels > 0)
    opened->input_interleaved =
        SAFE_CALLOC(opened->input_capacity_frames, (size_t)opened->input_channels * sizeof(float), float *);
  if (opened->output_channels > 0 && callback) {
    opened->output_capacity_frames = opened->frames_per_buffer ? opened->frames_per_buffer : 1024;
    opened->output_interleaved =
        SAFE_CALLOC(opened->output_capacity_frames, (size_t)opened->output_channels * sizeof(float), float *);
  }
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
  ios_audio_refresh_devices();
  if (stream->device_index >= 0 && stream->device_index < g_ios_device_count) {
    AVAudioSessionPortDescription *preferred = g_ios_devices[stream->device_index].input_port;
    if (preferred)
      [session setPreferredInput:preferred error:NULL];
  }
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
                         const int callback_result = stream->callback(stream->input_interleaved, NULL, frames,
                                                                      &time_info, 0, stream->user_data);
                         if (callback_result == paAbort || callback_result == paComplete) {
                           stream->active = NO;
                           [stream->engine.inputNode removeTapOnBus:0];
                           break;
                         }
                         offset += frames;
                       }
                     }];
  }
  if (stream->player) {
    [stream->engine attachNode:stream->player];
    [stream->engine connect:stream->player to:stream->engine.mainMixerNode format:nil];
  }
  if (stream->callback && stream->output_channels > 0) {
    AVAudioFormat *format =
        [[AVAudioFormat alloc] initStandardFormatWithSampleRate:stream->sample_rate
                                                       channels:(AVAudioChannelCount)stream->output_channels];
    stream->source = [[AVAudioSourceNode alloc]
        initWithFormat:format
           renderBlock:^OSStatus(BOOL *is_silence, const AudioTimeStamp *timestamp, AVAudioFrameCount frame_count,
                                 AudioBufferList *output_data) {
             (void)timestamp;
             const size_t frames = MIN((size_t)frame_count, stream->output_capacity_frames);
             PaStreamCallbackTimeInfo time_info = {0, 0, 0};
             const int result =
                 stream->callback(NULL, stream->output_interleaved, frames, &time_info, 0, stream->user_data);
             for (UInt32 buffer_index = 0; buffer_index < output_data->mNumberBuffers; ++buffer_index) {
               AudioBuffer *buffer = &output_data->mBuffers[buffer_index];
               float *destination = buffer->mData;
               const UInt32 channel = buffer_index;
               for (size_t frame = 0; frame < frames; ++frame)
                 destination[frame] = stream->output_interleaved[frame * (size_t)stream->output_channels + channel];
               buffer->mDataByteSize = (UInt32)(frames * sizeof(float));
             }
             *is_silence = result == paComplete || result == paAbort;
             return noErr;
           }];
    [stream->engine attachNode:stream->source];
    [stream->engine connect:stream->source to:stream->engine.mainMixerNode format:format];
  }
  NSError *error = nil;
  [stream->engine startAndReturnError:&error];
  if (error)
    return -1;
  if (stream->player)
    [stream->player play];
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
  [stream->engine.inputNode removeTapOnBus:0];
  [stream->engine stop];
  if (stream->interruption_observer) {
    [[NSNotificationCenter defaultCenter] removeObserver:stream->interruption_observer];
    stream->interruption_observer = nil;
  }
  stream->active = NO;
  stream->player = nil;
  stream->source = nil;
  stream->engine = nil;
  stream->queue_lock = nil;
  SAFE_FREE(stream->input_interleaved);
  SAFE_FREE(stream->output_interleaved);
  SAFE_FREE(stream);
  return paNoError;
}

int asciichat_ios_audio_device_count(void) {
  ios_audio_refresh_devices();
  return g_ios_device_count;
}

const char *asciichat_ios_audio_device_name(int index) {
  ios_audio_refresh_devices();
  return index >= 0 && index < g_ios_device_count ? g_ios_devices[index].name : NULL;
}

int asciichat_ios_audio_select_device(int index) {
  ios_audio_refresh_devices();
  if (index < 0 || index >= g_ios_device_count)
    return -1;
  g_ios_selected_device = index;
  return 0;
}

PaError Pa_WriteStream(PaStream *stream, const void *buffer, unsigned long frames) {
  if (!stream || !stream->player || !buffer || frames == 0)
    return -1;
  [stream->queue_lock lock];
  {
    const unsigned long max_queued_frames = (unsigned long)(stream->sample_rate * 2.0);
    if (!stream->active || frames > max_queued_frames || stream->queued_frames > max_queued_frames - frames) {
      [stream->queue_lock unlock];
      return paOutputUnderflow;
    }
    stream->queued_frames += frames;
  }
  [stream->queue_lock unlock];
  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:stream->sample_rate
                                                     channels:(AVAudioChannelCount)stream->output_channels];
  AVAudioPCMBuffer *audio = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:(AVAudioFrameCount)frames];
  audio.frameLength = (AVAudioFrameCount)frames;
  const float *samples = buffer;
  for (size_t frame = 0; frame < frames; ++frame)
    for (int channel = 0; channel < stream->output_channels; ++channel)
      audio.floatChannelData[channel][frame] = samples[frame * (size_t)stream->output_channels + (size_t)channel];
  [stream->player scheduleBuffer:audio
               completionHandler:^{
                 [stream->queue_lock lock];
                 stream->queued_frames = stream->queued_frames > frames ? stream->queued_frames - frames : 0;
                 [stream->queue_lock unlock];
               }];
  return paNoError;
}
