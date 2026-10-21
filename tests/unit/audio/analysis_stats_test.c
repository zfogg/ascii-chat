#include <criterion/criterion.h>
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>

TestSuite(analysis_stats);

Test(analysis_stats, known_samples_have_exact_peak_rms_mean_silence_and_clipping_counts) {
  cr_assert_eq(audio_analysis_init(), 0);
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
    cr_assert_eq(stats[i]->total_samples, 5);
    cr_assert_eq(stats[i]->packets_count, 2);
    cr_assert_eq(stats[i]->silent_samples, 1);
    cr_assert_eq(stats[i]->clipping_count, 2);
    cr_assert_float_eq(stats[i]->peak_level, 1.5f, 0.0001f);
    cr_assert_float_eq(stats[i]->rms_level, 1.0f, 0.0001f);
    cr_assert_float_eq(stats[i]->dc_offset, 0.0f, 0.0001f);
  }
  audio_analysis_destroy();
  audio_analysis_track_sent_sample(1);
  audio_analysis_track_received_sample(1);
  cr_assert_eq(audio_analysis_get_sent_stats()->total_samples, 5);
  cr_assert_eq(audio_analysis_get_received_stats()->total_samples, 5);
}

Test(analysis_stats, report_describes_silence_signal_and_echo_metrics) {
  char path[1024];
  int fd;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "ascii-analysis", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  log_init(path, LOG_DEBUG, false, false);
  cr_assert_eq(audio_analysis_init(), 0);
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
  cr_assert_eq(audio_analysis_get_received_stats()->total_samples, 11520);
  cr_assert_eq(audio_analysis_get_received_stats()->silent_samples >= 3840, true);
  cr_assert_gt(audio_analysis_get_received_stats()->rms_level, 0.2f);
  cr_assert_lt(audio_analysis_get_received_stats()->rms_level, 0.4f);
  audio_analysis_destroy();
  log_destroy();
  FILE *stream = platform_fopen("analysis-test", path, "r");
  cr_assert_not_null(stream);
  cr_assert_eq(fseek(stream, 0, SEEK_END), 0);
  long length = ftell(stream);
  cr_assert_gt(length, 0);
  rewind(stream);
  char *text = SAFE_CALLOC((size_t)length + 1, 1, char *);
  cr_assert_eq(fread(text, 1, (size_t)length, stream), (size_t)length);
  cr_assert_not_null(strstr(text, "AUDIO ANALYSIS REPORT"));
  cr_assert_not_null(strstr(text, "NO AUDIO RECEIVED"));
  cr_assert_not_null(strstr(text, "11520"));
  cr_assert_not_null(strstr(text, "AEC3"));
  SAFE_FREE(text);
  fclose(stream);
  cr_assert_eq(platform_delete_temp_file(path), 0);
}
