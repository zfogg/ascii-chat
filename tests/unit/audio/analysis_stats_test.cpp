#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/log/log.h>
#include <libplatform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>
TEST_CASE("analysis_stats/known_samples_have_exact_peak_rms_mean_silence_and_clipping_counts", "[analysis_stats] [unit]") {
  REQUIRE((audio_analysis_init()) == (0));
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
    REQUIRE((stats[i]->total_samples) == (5));
    REQUIRE((stats[i]->packets_count) == (2));
    REQUIRE((stats[i]->silent_samples) == (1));
    REQUIRE((stats[i]->clipping_count) == (2));
    REQUIRE((stats[i]->peak_level) == Catch::Approx(1.5f));
    REQUIRE((stats[i]->rms_level) == Catch::Approx(1.0f));
    REQUIRE((stats[i]->dc_offset) == Catch::Approx(0.0f));
  }
  audio_analysis_destroy();
  audio_analysis_track_sent_sample(1);
  audio_analysis_track_received_sample(1);
  REQUIRE((audio_analysis_get_sent_stats()->total_samples) == (5));
  REQUIRE((audio_analysis_get_received_stats()->total_samples) == (5));
}

TEST_CASE("analysis_stats/report_describes_silence_signal_and_echo_metrics", "[analysis_stats] [unit]") {
  char path[1024];
  int fd;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "ascii-analysis", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  log_init(path, LOG_DEBUG, false, false);
  REQUIRE((audio_analysis_init()) == (0));
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
  REQUIRE((audio_analysis_get_received_stats()->total_samples) == (11520));
  REQUIRE((audio_analysis_get_received_stats()->silent_samples >= 3840) == (true));
  REQUIRE((audio_analysis_get_received_stats()->rms_level) > (0.2f));
  REQUIRE((audio_analysis_get_received_stats()->rms_level) < (0.4f));
  audio_analysis_destroy();
  log_destroy();
  FILE *stream = platform_fopen("analysis-test", path, "r");
  REQUIRE((stream) != nullptr);
  REQUIRE((fseek(stream, 0, SEEK_END)) == (0));
  long length = ftell(stream);
  REQUIRE((length) > (0));
  rewind(stream);
  char *text = SAFE_CALLOC((size_t)length + 1, 1, char *);
  REQUIRE((fread(text, 1, (size_t)length, stream)) == ((size_t)length));
  REQUIRE((strstr(text, "AUDIO ANALYSIS REPORT")) != nullptr);
  REQUIRE((strstr(text, "NO AUDIO RECEIVED")) != nullptr);
  REQUIRE((strstr(text, "11520")) != nullptr);
  REQUIRE((strstr(text, "AEC3")) != nullptr);
  SAFE_FREE(text);
  fclose(stream);
  REQUIRE((platform_delete_temp_file(path)) == (0));
}
