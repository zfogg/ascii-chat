#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/webrtc/sdp.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <libplatform/system.h>
#include <string.h>

using sdp_renderer_t = decltype(terminal_format_params_t{}.renderer);
using sdp_charset_t = decltype(terminal_format_params_t{}.charset);
using sdp_compression_t = decltype(terminal_format_params_t{}.compression);
static constexpr sdp_renderer_t sdp_renderer_halfblock = static_cast<sdp_renderer_t>(1);
static constexpr sdp_renderer_t sdp_renderer_braille = static_cast<sdp_renderer_t>(2);
static constexpr sdp_charset_t sdp_charset_utf8 = static_cast<sdp_charset_t>(1);
static constexpr sdp_compression_t sdp_compression_zstd = static_cast<sdp_compression_t>(2);
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
                                            .renderer = sdp_renderer_braille,
                                            .charset = sdp_charset_utf8,
                                            .compression = sdp_compression_zstd,
                                            .csi_rep_support = true}};
}

TEST_CASE("sdp/offers_round_trip_audio_rendering_and_codec_capabilities", "[sdp]") {
  terminal_capability_t caps[4];
  for (int i = 0; i < 4; ++i)
    caps[i] = capability((acip_codec_t)(96 + i));
  opus_config_t audio = audio_config();
  sdp_session_t offer = {0}, parsed = {0};
  ASCIICHAT_REQUIRE_EQ(sdp_generate_offer(caps, 4, &audio, &caps[0].format, 3, 1, &offer), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_GT(offer.sdp_length, 0);
  ASCIICHAT_REQUIRE_EQ(offer.sdp_length, strlen(offer.sdp_string));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(offer.sdp_string, "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99"));
  ASCIICHAT_REQUIRE_EQ(sdp_parse(offer.sdp_string, &parsed), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(parsed.has_audio);
  ASCIICHAT_REQUIRE(parsed.has_video);
  ASCIICHAT_REQUIRE_EQ(parsed.video_codec_count, 4);
  ASCIICHAT_REQUIRE_EQ(parsed.audio_config.sample_rate, 48000);
  ASCIICHAT_REQUIRE(parsed.audio_config.fec_enabled);
  ASCIICHAT_REQUIRE(parsed.audio_config.dtx_enabled);
  for (size_t i = 0; i < 4; ++i) {
    ASCIICHAT_REQUIRE_EQ(parsed.video_codecs[i].codec, caps[i].codec);
    ASCIICHAT_REQUIRE_EQ(parsed.video_codecs[i].format.width, 100);
    ASCIICHAT_REQUIRE_EQ(parsed.video_codecs[i].format.height, 40);
    ASCIICHAT_REQUIRE_EQ(parsed.video_codecs[i].format.renderer, sdp_renderer_braille);
    ASCIICHAT_REQUIRE_EQ(parsed.video_codecs[i].format.compression, sdp_compression_zstd);
  }
  sdp_session_destroy(&parsed);
  sdp_session_destroy(&offer);
}

TEST_CASE("sdp/answer_obeys_server_preference_and_format_constraints", "[sdp]") {
  terminal_capability_t offered[] = {capability(ACIP_CODEC_TRUECOLOR), capability(ACIP_CODEC_256COLOR),
                                     capability(ACIP_CODEC_MONO)};
  terminal_capability_t supported[] = {capability(ACIP_CODEC_256COLOR), capability(ACIP_CODEC_TRUECOLOR)};
  opus_config_t audio = audio_config();
  sdp_session_t offer = {0}, answer = {0};
  terminal_format_params_t constraint = {.width = 80, .height = 24, .renderer = sdp_renderer_halfblock};
  ASCIICHAT_REQUIRE_EQ(sdp_generate_offer(offered, 3, &audio, NULL, 7, 3, &offer), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(sdp_generate_answer(&offer, supported, 2, &audio, &constraint, 3, 1, &answer), ASCIICHAT_OK);
  acip_codec_t selected;
  terminal_format_params_t format;
  ASCIICHAT_REQUIRE_EQ(sdp_get_selected_video_codec(&answer, &selected, &format), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(selected, ACIP_CODEC_256COLOR);
  ASCIICHAT_REQUIRE_EQ(format.width, 80);
  ASCIICHAT_REQUIRE_EQ(format.height, 24);
  ASCIICHAT_REQUIRE_EQ(format.renderer, sdp_renderer_halfblock);
  ASCIICHAT_REQUIRE_EQ(answer.codec_capabilities_video, 3);
  ASCIICHAT_REQUIRE_EQ(answer.codec_capabilities_audio, 1);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(answer.sdp_string, "ACIP-256"));
  sdp_session_destroy(&answer);
  sdp_session_destroy(&offer);
}

TEST_CASE("sdp/absent_media_and_invalid_arguments_are_reported", "[sdp]") {
  sdp_session_t session = {0};
  acip_codec_t codec;
  terminal_format_params_t format;
  opus_config_t audio = audio_config();
  terminal_capability_t cap = capability(ACIP_CODEC_MONO);
  ASCIICHAT_REQUIRE_EQ(sdp_get_selected_video_codec(&session, &codec, &format), ERROR_NOT_FOUND);
  ASCIICHAT_REQUIRE_EQ(sdp_get_selected_video_codec(NULL, &codec, &format), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(sdp_generate_offer(NULL, 1, &audio, NULL, 0, 0, &session), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(sdp_generate_offer(&cap, 0, &audio, NULL, 0, 0, &session), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(sdp_generate_offer(&cap, 1, NULL, NULL, 0, 0, &session), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(sdp_parse(NULL, &session), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(sdp_parse("v=0\r\n", NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(sdp_parse("v=0\r\nx=ignored\r\na=unknown:value\r\n", &session), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(session.has_video);
  ASCIICHAT_REQUIRE_NOT(session.has_audio);
  sdp_session_destroy(&session);
  sdp_session_destroy(NULL);
  ASCIICHAT_REQUIRE_STR_EQ(sdp_codec_name((acip_codec_t)0), "UNKNOWN");
  ASCIICHAT_REQUIRE_STR_EQ(sdp_renderer_name(-1), "unknown");
}

TEST_CASE("sdp/terminal_detection_honors_depth_utf8_and_output_capacity", "[sdp]") {
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
    ASCIICHAT_REQUIRE_EQ(sdp_detect_terminal_capabilities(caps, 8, &count), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_GT(count, 0);
    ASCIICHAT_REQUIRE_LE(count, 8);
    ASCIICHAT_REQUIRE_EQ(caps[0].codec, cases[i].preferred);
    ASCIICHAT_REQUIRE_EQ(sdp_detect_terminal_capabilities(caps, 1, &count), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(count, 1);
  }
  size_t count;
  ASCIICHAT_REQUIRE_EQ(sdp_detect_terminal_capabilities(NULL, 1, &count), ERROR_INVALID_PARAM);
  terminal_capability_t cap;
  ASCIICHAT_REQUIRE_EQ(sdp_detect_terminal_capabilities(&cap, 0, &count), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(sdp_detect_terminal_capabilities(&cap, 1, NULL), ERROR_INVALID_PARAM);
}
