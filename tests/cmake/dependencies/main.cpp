#include <absl/strings/str_cat.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <opus.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <portaudio.h>
#include <upnperrors.h>
#include <zlib.h>
#include <zstd.h>
#include <expat.h>
#include <mimalloc.h>
#include <utf8proc.h>
#include <vterm.h>
#include <libwebsockets.h>
#include <rtc/rtc.h>
#include "api/echo_canceller3_factory.h"

#include <cstdio>
#include <cstring>

int main() {
  FT_Library library = nullptr;
  if (FT_Init_FreeType(&library) != 0)
    return 1;
  FT_Done_FreeType(library);
  char compressed[128];
  char decoded[128] = {};
  const char input[] = "CMake dependency round trip";
  auto size = ZSTD_compress(compressed, sizeof(compressed), input, sizeof(input), 1);
  if (ZSTD_isError(size) || ZSTD_decompress(decoded, sizeof(decoded), compressed, size) != sizeof(input) ||
      std::strcmp(decoded, input) != 0)
    return 2;
  unsigned char zipped[128];
  uLongf bytes = sizeof(zipped);
  if (compress(zipped, &bytes, reinterpret_cast<const Bytef *>(input), sizeof(input)) != Z_OK)
    return 3;
  uLongf output = sizeof(decoded);
  if (uncompress(reinterpret_cast<Bytef *>(decoded), &output, zipped, bytes) != Z_OK ||
      std::strcmp(decoded, input) != 0)
    return 3;
  int error = 0;
  OpusEncoder *encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);
  if (!encoder || error != OPUS_OK)
    return 4;
  opus_encoder_destroy(encoder);
  PCRE2_SIZE offset = 0;
  auto *pattern =
      pcre2_compile(reinterpret_cast<PCRE2_SPTR>("CMake"), PCRE2_ZERO_TERMINATED, 0, &error, &offset, nullptr);
  if (!pattern)
    return 5;
  pcre2_code_free(pattern);
  auto message = absl::StrCat("PASS ", zlibVersion(), " ", ZSTD_versionString(), " ", opus_get_version_string());
  if (!Pa_GetVersionText() || !strupnperror(501))
    return 6;
  auto parser = XML_ParserCreate(nullptr);
  if (!parser || XML_Parse(parser, "<cmake/>", 8, XML_TRUE) != XML_STATUS_OK)
    return 7;
  XML_ParserFree(parser);
  auto terminal = vterm_new(4, 20);
  if (!terminal)
    return 8;
  vterm_free(terminal);
  if (!lws_get_library_version())
    return 9;
  rtcInitLogger(RTC_LOG_NONE, nullptr);
  webrtc::EchoCanceller3Factory factory;
  auto echo = factory.Create(48000, 1, 1);
  if (!echo)
    return 10;
  auto allocation = mi_malloc(64);
  if (!allocation)
    return 11;
  mi_free(allocation);
  if (utf8proc_charwidth(0x754c) != 2)
    return 12;
  std::puts(message.c_str());
  return 0;
}
