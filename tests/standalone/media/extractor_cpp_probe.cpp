#include <ascii-chat/media/yt_dlp.h>

extern "C" bool extractor_cpp_linkage_probe(void) {
  char video[256], audio[256];
  yt_dlp_request_options_t video_options = {}, audio_options = {};
  return yt_dlp_extract_with_options(nullptr, nullptr, video, sizeof(video), audio, sizeof(audio), &video_options,
                                     &audio_options, nullptr, nullptr) != ASCIICHAT_OK;
}
