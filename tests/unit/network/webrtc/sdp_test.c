#include <criterion/criterion.h>
#include <ascii-chat/network/webrtc/sdp.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/platform/system.h>
#include <string.h>

TestSuite(sdp);

static opus_config_t audio_config(void) {
  return (opus_config_t){.sample_rate = 48000,
                         .channels = 2,
                         .bitrate = 24000,
                         .frame_duration = 20,
                         .dtx_enabled = true,
                         .fec_enabled = true};
}

static terminal_capability_t capability(acip_codec_t codec) {
  return (terminal_capability_t){.codec = codec,
                                 .format = {.width = 100,
                                            .height = 40,
                                            .renderer = RENDERER_BRAILLE,
                                            .charset = CHARSET_UTF8,
                                            .compression = COMPRESSION_ZSTD,
                                            .csi_rep_support = true}};
}

Test(sdp, offers_round_trip_audio_rendering_and_codec_capabilities) {
  terminal_capability_t caps[4];
  for (int i = 0; i < 4; ++i)
    caps[i] = capability((acip_codec_t)(96 + i));
  opus_config_t audio = audio_config();
  sdp_session_t offer = {0}, parsed = {0};
  cr_assert_eq(sdp_generate_offer(caps, 4, &audio, &caps[0].format, 3, 1, &offer), ASCIICHAT_OK);
  cr_assert_gt(offer.sdp_length, 0);
  cr_assert_eq(offer.sdp_length, strlen(offer.sdp_string));
  cr_assert_not_null(strstr(offer.sdp_string, "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99"));
  cr_assert_eq(sdp_parse(offer.sdp_string, &parsed), ASCIICHAT_OK);
  cr_assert(parsed.has_audio);
  cr_assert(parsed.has_video);
  cr_assert_eq(parsed.video_codec_count, 4);
  cr_assert_eq(parsed.audio_config.sample_rate, 48000);
  cr_assert(parsed.audio_config.fec_enabled);
  cr_assert(parsed.audio_config.dtx_enabled);
  for (size_t i = 0; i < 4; ++i) {
    cr_assert_eq(parsed.video_codecs[i].codec, caps[i].codec);
    cr_assert_eq(parsed.video_codecs[i].format.width, 100);
    cr_assert_eq(parsed.video_codecs[i].format.height, 40);
    cr_assert_eq(parsed.video_codecs[i].format.renderer, RENDERER_BRAILLE);
    cr_assert_eq(parsed.video_codecs[i].format.compression, COMPRESSION_ZSTD);
  }
  sdp_session_destroy(&parsed);
  sdp_session_destroy(&offer);
}

Test(sdp, answer_obeys_server_preference_and_format_constraints) {
  terminal_capability_t offered[] = {capability(ACIP_CODEC_TRUECOLOR), capability(ACIP_CODEC_256COLOR),
                                     capability(ACIP_CODEC_MONO)};
  terminal_capability_t supported[] = {capability(ACIP_CODEC_256COLOR), capability(ACIP_CODEC_TRUECOLOR)};
  opus_config_t audio = audio_config();
  sdp_session_t offer = {0}, answer = {0};
  terminal_format_params_t constraint = {.width = 80, .height = 24, .renderer = RENDERER_HALFBLOCK};
  cr_assert_eq(sdp_generate_offer(offered, 3, &audio, NULL, 7, 3, &offer), ASCIICHAT_OK);
  cr_assert_eq(sdp_generate_answer(&offer, supported, 2, &audio, &constraint, 3, 1, &answer), ASCIICHAT_OK);
  acip_codec_t selected;
  terminal_format_params_t format;
  cr_assert_eq(sdp_get_selected_video_codec(&answer, &selected, &format), ASCIICHAT_OK);
  cr_assert_eq(selected, ACIP_CODEC_256COLOR);
  cr_assert_eq(format.width, 80);
  cr_assert_eq(format.height, 24);
  cr_assert_eq(format.renderer, RENDERER_HALFBLOCK);
  cr_assert_eq(answer.codec_capabilities_video, 3);
  cr_assert_eq(answer.codec_capabilities_audio, 1);
  cr_assert_not_null(strstr(answer.sdp_string, "ACIP-256"));
  sdp_session_destroy(&answer);
  sdp_session_destroy(&offer);
}

Test(sdp, absent_media_and_invalid_arguments_are_reported) {
  sdp_session_t session = {0};
  acip_codec_t codec;
  terminal_format_params_t format;
  opus_config_t audio = audio_config();
  terminal_capability_t cap = capability(ACIP_CODEC_MONO);
  cr_assert_eq(sdp_get_selected_video_codec(&session, &codec, &format), ERROR_NOT_FOUND);
  cr_assert_eq(sdp_get_selected_video_codec(NULL, &codec, &format), ERROR_INVALID_PARAM);
  cr_assert_eq(sdp_generate_offer(NULL, 1, &audio, NULL, 0, 0, &session), ERROR_INVALID_PARAM);
  cr_assert_eq(sdp_generate_offer(&cap, 0, &audio, NULL, 0, 0, &session), ERROR_INVALID_PARAM);
  cr_assert_eq(sdp_generate_offer(&cap, 1, NULL, NULL, 0, 0, &session), ERROR_INVALID_PARAM);
  cr_assert_eq(sdp_parse(NULL, &session), ERROR_INVALID_PARAM);
  cr_assert_eq(sdp_parse("v=0\r\n", NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(sdp_parse("v=0\r\nx=ignored\r\na=unknown:value\r\n", &session), ASCIICHAT_OK);
  cr_assert_not(session.has_video);
  cr_assert_not(session.has_audio);
  sdp_session_destroy(&session);
  sdp_session_destroy(NULL);
  cr_assert_str_eq(sdp_codec_name((acip_codec_t)0), "UNKNOWN");
  cr_assert_str_eq(sdp_renderer_name(-1), "unknown");
}

Test(sdp, terminal_detection_honors_depth_utf8_and_output_capacity) {
  const struct {
    const char *term, *color, *lang;
    acip_codec_t preferred;
  } cases[] = {{"xterm", "truecolor", "en_US.UTF-8", ACIP_CODEC_TRUECOLOR},
               {"xterm-256color", "", "C", ACIP_CODEC_256COLOR},
               {"xterm", "", "C", ACIP_CODEC_16COLOR},
               {"dumb", "", "C", ACIP_CODEC_MONO}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
    platform_setenv("TERM", cases[i].term);
    platform_setenv("COLORTERM", cases[i].color);
    platform_setenv("LANG", cases[i].lang);
    terminal_capability_t caps[8];
    size_t count = 0;
    cr_assert_eq(sdp_detect_terminal_capabilities(caps, 8, &count), ASCIICHAT_OK);
    cr_assert_gt(count, 0);
    cr_assert_leq(count, 8);
    cr_assert_eq(caps[0].codec, cases[i].preferred);
    cr_assert_eq(sdp_detect_terminal_capabilities(caps, 1, &count), ASCIICHAT_OK);
    cr_assert_eq(count, 1);
  }
  size_t count;
  cr_assert_eq(sdp_detect_terminal_capabilities(NULL, 1, &count), ERROR_INVALID_PARAM);
  terminal_capability_t cap;
  cr_assert_eq(sdp_detect_terminal_capabilities(&cap, 0, &count), ERROR_INVALID_PARAM);
  cr_assert_eq(sdp_detect_terminal_capabilities(&cap, 1, NULL), ERROR_INVALID_PARAM);
}
