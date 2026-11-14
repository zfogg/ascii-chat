#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>
TEST_CASE("analysis_stats/known_samples_have_exact_peak_rms_mean_silence_and_clipping_counts", "[analysis_stats]") {
  ASCIICHAT_REQUIRE_EQ(audio_analysis_init(), 0);
  const float samples[] = {0.0f, 0.5f, -0.5f, 1.5f, -1.5f};
  for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); ++i) {
    audio_analysis_track_sent_sample(samples[i]);
    audio_analysis_track_received_sample(samples[i]);
  }
  audio_analysis_track_sent_packet(960);
  audio_analysis_track_received_packet(960);
  audio_analysis_track_sent_packet(480);
  audio_analysis_track_received_packet(480);
  const audio_analysis_stats_t *stats[] = {audio_analysis_get_sent_stats(), audio_analysis_get_received_stats()};
  for (size_t i = 0; i < 2; ++i) {
    ASCIICHAT_REQUIRE_EQ(stats[i]->total_samples, 5);
    ASCIICHAT_REQUIRE_EQ(stats[i]->packets_count, 2);
    ASCIICHAT_REQUIRE_EQ(stats[i]->silent_samples, 1);
    ASCIICHAT_REQUIRE_EQ(stats[i]->clipping_count, 2);
    ASCIICHAT_REQUIRE_FLOAT_EQ(stats[i]->peak_level, 1.5f, 0.0001f);
    ASCIICHAT_REQUIRE_FLOAT_EQ(stats[i]->rms_level, 1.0f, 0.0001f);
    ASCIICHAT_REQUIRE_FLOAT_EQ(stats[i]->dc_offset, 0.0f, 0.0001f);
  }
  audio_analysis_destroy();
  audio_analysis_track_sent_sample(1);
  audio_analysis_track_received_sample(1);
  ASCIICHAT_REQUIRE_EQ(audio_analysis_get_sent_stats()->total_samples, 5);
  ASCIICHAT_REQUIRE_EQ(audio_analysis_get_received_stats()->total_samples, 5);
}

TEST_CASE("analysis_stats/report_describes_silence_signal_and_echo_metrics", "[analysis_stats]") {
  char path[1024];
  int fd;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(path, sizeof(path), "ascii-analysis", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  log_init(path, LOG_DEBUG, false, false);
  ASCIICHAT_REQUIRE_EQ(audio_analysis_init(), 0);
  audio_analysis_print_report();
  for (int frame = 0; frame < 12; ++frame) {
    for (int i = 0; i < 960; ++i) {
      float sample = frame % 3 == 0 ? 0.0f : 0.5f * sinf((float)i * 0.1f);
      audio_analysis_track_sent_sample(sample);
      audio_analysis_track_received_sample(sample);
    }
    audio_analysis_track_sent_packet(1920);
    audio_analysis_track_received_packet(1920);
  }
  audio_analysis_set_aec3_metrics(12.0, 18.0, 50000000);
  audio_analysis_print_report();
  ASCIICHAT_REQUIRE_EQ(audio_analysis_get_received_stats()->total_samples, 11520);
  ASCIICHAT_REQUIRE_EQ(audio_analysis_get_received_stats()->silent_samples >= 3840, true);
  ASCIICHAT_REQUIRE_GT(audio_analysis_get_received_stats()->rms_level, 0.2f);
  ASCIICHAT_REQUIRE_LT(audio_analysis_get_received_stats()->rms_level, 0.4f);
  audio_analysis_destroy();
  log_destroy();
  FILE *stream = platform_fopen("analysis-test", path, "r");
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  ASCIICHAT_REQUIRE_EQ(fseek(stream, 0, SEEK_END), 0);
  long length = ftell(stream);
  ASCIICHAT_REQUIRE_GT(length, 0);
  rewind(stream);
  char *text = SAFE_CALLOC((size_t)length + 1, 1, char *);
  ASCIICHAT_REQUIRE_EQ(fread(text, 1, (size_t)length, stream), (size_t)length);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "AUDIO ANALYSIS REPORT"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "NO AUDIO RECEIVED"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "11520"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "AEC3"));
  SAFE_FREE(text);
  fclose(stream);
  ASCIICHAT_REQUIRE_EQ(platform_delete_temp_file(path), 0);
}
