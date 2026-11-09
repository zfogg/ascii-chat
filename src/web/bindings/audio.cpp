#include <emscripten/bind.h>
extern "C" {
#include <ascii-chat/audio/processor.h>
#include "../client.h"
}

#include <cstddef>
#include <cstdint>
#include <opus.h>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(AudioSamples);
EMSCRIPTEN_DECLARE_VAL_TYPE(PcmSamples);
EMSCRIPTEN_DECLARE_VAL_TYPE(OpusBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(NullableOpusBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(NullablePcmSamples);

static audio_processor_t *create_processor(int sample_rate, bool bypass, unsigned frame_samples, int channels) {
  if (sample_rate <= 0 || frame_samples == 0 || (channels != 1 && channels != 2)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser audio processor configuration");
    return nullptr;
  }
  audio_processor_config_t config = audio_processor_default_config(sample_rate);
  config.channels = channels;
  if (bypass) {
    config.spectral_enabled = false;
    config.flags = {};
  }
  audio_processor_t *processor = nullptr;
  return audio_processor_create(&config, &processor) == ASCIICHAT_OK ? processor : nullptr;
}

class AudioProcessorBinding {
public:
  AudioProcessorBinding(int sample_rate, bool bypass, unsigned frame_samples)
      : AudioProcessorBinding(sample_rate, bypass, frame_samples, 2) {}
  AudioProcessorBinding(int sample_rate, bool bypass, unsigned frame_samples, int channels)
      : processor_(create_processor(sample_rate, bypass, frame_samples, channels)),
        scratch_(processor_ ? frame_samples : 0) {}
  ~AudioProcessorBinding() {
    if (processor_)
      audio_processor_destroy(processor_);
  }

  AudioProcessorBinding(const AudioProcessorBinding &) = delete;
  AudioProcessorBinding &operator=(const AudioProcessorBinding &) = delete;

  bool valid() const {
    return processor_ != nullptr;
  }
  int process(AudioSamples input, AudioSamples output) {
    const size_t count = input["length"].as<size_t>();
    if (!processor_)
      return SET_ERRNO(ERROR_INVALID_STATE, "Audio processor is not initialized");
    if (count == 0 || count > scratch_.size() || output["length"].as<size_t>() != count)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio processor buffer size: %zu", count);
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(count, scratch_.data()));
    view.call<void>("set", input);
    int result = audio_processor_process(processor_, scratch_.data(), scratch_.data(), count);
    if (result == 0) {
      // Processing may grow memory, so do not retain the previous JS view.
      emscripten::val result_view = emscripten::val(emscripten::typed_memory_view(count, scratch_.data()));
      output.call<void>("set", result_view);
    }
    return result;
  }
  int processBatch(emscripten::val inputs, emscripten::val outputs) {
    if (!processor_)
      return SET_ERRNO(ERROR_INVALID_STATE, "Audio processor is not initialized");
    const unsigned count = inputs["length"].as<unsigned>();
    if (count > 128 || outputs["length"].as<unsigned>() != count)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio processor batch size: %u", count);
    for (unsigned index = 0; index < count; ++index) {
      const int result = process(AudioSamples(inputs[index]), AudioSamples(outputs[index]));
      if (result)
        return result;
    }
    return 0;
  }

private:
  audio_processor *processor_;
  std::vector<float> scratch_;
};

class OpusCodecBinding {
public:
  OpusCodecBinding(int sample_rate, int channels, int bitrate, unsigned max_packet) : channels_(channels) {
    if (sample_rate <= 0 || sample_rate > 48000 || (channels != 1 && channels != 2) || !max_packet) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid Opus codec configuration");
      return;
    }
    int error = OPUS_OK;
    encoder_ = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK || !encoder_) {
      SET_ERRNO(ERROR_AUDIO, "Cannot initialize browser Opus encoder: %s", opus_strerror(error));
      return;
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate)) != OPUS_OK) {
      opus_encoder_destroy(encoder_);
      encoder_ = nullptr;
      SET_ERRNO(ERROR_AUDIO, "Cannot configure browser Opus encoder");
      return;
    }
    decoder_ = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK || !decoder_) {
      opus_encoder_destroy(encoder_);
      encoder_ = nullptr;
      SET_ERRNO(ERROR_AUDIO, "Cannot initialize browser Opus decoder: %s", opus_strerror(error));
      return;
    }
    const size_t frame = (static_cast<size_t>(sample_rate) * 12 + 99) / 100;
    pcm_.resize(frame * channels);
    encoded_.resize(max_packet);
    input_.resize(65535);
    initialized_ = true;
  }
  ~OpusCodecBinding() {
    if (encoder_)
      opus_encoder_destroy(encoder_);
    if (decoder_)
      opus_decoder_destroy(decoder_);
  }
  OpusCodecBinding(const OpusCodecBinding &) = delete;
  OpusCodecBinding &operator=(const OpusCodecBinding &) = delete;
  bool valid() const {
    return initialized_;
  }

  NullableOpusBytes encode(PcmSamples samples) {
    const size_t count = samples["length"].as<size_t>();
    if (!initialized_) {
      SET_ERRNO(ERROR_INVALID_STATE, "Opus codec is not initialized");
      return NullableOpusBytes(emscripten::val::null());
    }
    if (!count || count > pcm_.size() || count % channels_) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid Opus input sample count: %zu", count);
      return NullableOpusBytes(emscripten::val::null());
    }
    emscripten::val view = emscripten::val(emscripten::typed_memory_view(count, pcm_.data()));
    view.call<void>("set", samples);
    const int size = opus_encode(encoder_, pcm_.data(), static_cast<int>(count / channels_), encoded_.data(),
                                 static_cast<int>(encoded_.size()));
    if (size < 0) {
      SET_ERRNO(ERROR_AUDIO, "Browser Opus encoding failed");
      return NullableOpusBytes(emscripten::val::null());
    }
    emscripten::val result = emscripten::val::global("Uint8Array").new_(size);
    result.call<void>("set", emscripten::val(emscripten::typed_memory_view(size, encoded_.data())));
    return NullableOpusBytes(result);
  }
  NullablePcmSamples decode(OpusBytes packet, int frame_size) {
    const size_t count = packet["length"].as<size_t>();
    if (!initialized_) {
      SET_ERRNO(ERROR_INVALID_STATE, "Opus codec is not initialized");
      return NullablePcmSamples(emscripten::val::null());
    }
    if (count > input_.size() || frame_size <= 0 || static_cast<size_t>(frame_size) * channels_ > pcm_.size()) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid Opus packet or output frame size");
      return NullablePcmSamples(emscripten::val::null());
    }
    if (count)
      emscripten::val(emscripten::typed_memory_view(count, input_.data())).call<void>("set", packet);
    const int samples = opus_decode(decoder_, input_.data(), static_cast<int>(count), pcm_.data(), frame_size, 0);
    if (samples < 0) {
      SET_ERRNO(ERROR_AUDIO, "Browser Opus decoding failed");
      return NullablePcmSamples(emscripten::val::null());
    }
    const size_t output_count = static_cast<size_t>(samples) * channels_;
    emscripten::val result = emscripten::val::global("Int16Array").new_(output_count);
    result.call<void>("set", emscripten::val(emscripten::typed_memory_view(output_count, pcm_.data())));
    return NullablePcmSamples(result);
  }

private:
  int channels_;
  bool initialized_ = false;
  OpusEncoder *encoder_ = nullptr;
  OpusDecoder *decoder_ = nullptr;
  std::vector<int16_t> pcm_;
  std::vector<uint8_t> encoded_, input_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_audio) {
  emscripten::register_type<AudioSamples>("AudioSamples", "Float32Array");
  emscripten::register_type<PcmSamples>("PcmSamples", "Int16Array");
  emscripten::register_type<OpusBytes>("OpusBytes", "Uint8Array");
  emscripten::register_type<NullableOpusBytes>("NullableOpusBytes", "Uint8Array | null");
  emscripten::register_type<NullablePcmSamples>("NullablePcmSamples", "Int16Array | null");
  emscripten::class_<AudioProcessorBinding>("AudioProcessorBinding")
      .constructor<int, bool, unsigned>()
      .constructor<int, bool, unsigned, int>()
      .function("valid", &AudioProcessorBinding::valid)
      .function("process", &AudioProcessorBinding::process)
      .function("processBatch", &AudioProcessorBinding::processBatch);
  emscripten::class_<OpusCodecBinding>("OpusCodecBinding")
      .constructor<int, int, int, unsigned>()
      .function("valid", &OpusCodecBinding::valid)
      .function("encode", &OpusCodecBinding::encode)
      .function("decode", &OpusCodecBinding::decode);
}
