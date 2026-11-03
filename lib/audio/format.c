#include <ascii-chat/audio/format.h>
#include <math.h>

asciichat_error_t audio_route_capture(const float *input, size_t frames, unsigned channels, int first_channel,
                                      unsigned selected_channels, float *output) {
  if (!input || !output || channels == 0 || channels > AUDIO_DEVICE_MAX_CHANNELS ||
      (selected_channels != 1 && selected_channels != 2) || first_channel < -1 ||
      (first_channel >= 0 &&
       ((unsigned)first_channel >= channels || selected_channels > channels - (unsigned)first_channel)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid microphone channel routing");
  for (size_t frame = 0; frame < frames; frame++) {
    float left, right;
    if (first_channel < 0) {
      double sum = 0;
      for (unsigned channel = 0; channel < channels; channel++)
        sum += input[frame * channels + channel];
      left = right = (float)(sum / channels);
    } else {
      left = input[frame * channels + (unsigned)first_channel];
      right = selected_channels == 2 ? input[frame * channels + (unsigned)first_channel + 1] : left;
    }
    output[frame * 2] = left;
    output[frame * 2 + 1] = right;
  }
  return ASCIICHAT_OK;
}

asciichat_error_t audio_convert_channels(const float *input, size_t frames, unsigned input_channels, float *output,
                                         unsigned output_channels) {
  if (!input || !output || (input_channels != 1 && input_channels != 2) ||
      (output_channels != 1 && output_channels != 2))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Only mono and stereo PCM are supported");
  // Backwards iteration also permits in-place mono expansion.
  if (input_channels == 1 && output_channels == 2) {
    for (size_t frame = frames; frame > 0; frame--)
      output[(frame - 1) * 2] = output[(frame - 1) * 2 + 1] = input[frame - 1];
  } else {
    for (size_t frame = 0; frame < frames; frame++) {
      float left = input[frame * input_channels];
      float right = input_channels == 2 ? input[frame * input_channels + 1] : left;
      if (output_channels == 1)
        output[frame] = left * 0.5f + right * 0.5f;
      else {
        output[frame * 2] = left;
        output[frame * 2 + 1] = right;
      }
    }
  }
  return ASCIICHAT_OK;
}

size_t audio_resample_stereo(audio_resampler_t *state, const float *input, size_t frames, double input_rate,
                             float *output, size_t capacity, double output_rate) {
  if (!state || !input || !output || !frames || !isfinite(input_rate) || !isfinite(output_rate) || input_rate < 8000 ||
      input_rate > 192000 || output_rate < 8000 || output_rate > 192000) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stereo resampling buffers, frame count, or sample rates");
    return 0;
  }
  const double step = input_rate / output_rate;
  double position = state->phase;
  if (!state->has_previous && position < 0)
    position += ceil(-position / step) * step;
  double limit = (double)frames - 1;
  size_t needed = position < limit ? (size_t)ceil((limit - position) / step) : 0;
  if (needed > capacity)
    return 0;
  size_t produced = 0;
  while (position < limit) {
    size_t first = position < 0 ? 0 : (size_t)position;
    double fraction = position < 0 ? position + 1 : position - (double)first;
    for (unsigned channel = 0; channel < 2; channel++) {
      float a = position < 0 ? state->previous[channel] : input[first * 2 + channel];
      float b = position < 0 ? input[channel] : input[(first + 1) * 2 + channel];
      output[produced * 2 + channel] = (float)(a + fraction * (b - a));
    }
    produced++;
    position += step;
  }
  state->phase = position - (double)frames;
  state->previous[0] = input[(frames - 1) * 2];
  state->previous[1] = input[(frames - 1) * 2 + 1];
  state->has_previous = true;
  return produced;
}
