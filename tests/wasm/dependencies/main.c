#include <sodium.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <opus.h>
#include <stdio.h>

int main(void) {
  static const unsigned char expected_hash[32] = {0xbd, 0xdd, 0x81, 0x3c, 0x63, 0x42, 0x39, 0x72, 0x31, 0x71, 0xef,
                                                  0x3f, 0xee, 0x98, 0x57, 0x9b, 0x94, 0x96, 0x4e, 0x3b, 0xb1, 0xcb,
                                                  0x3e, 0x42, 0x72, 0x62, 0xc8, 0xc0, 0x68, 0xd5, 0x23, 0x19};
  unsigned char hash[32];
  if (sodium_init() < 0 || crypto_generichash(hash, sizeof(hash), (const unsigned char *)"abc", 3, NULL, 0) != 0 ||
      sodium_memcmp(hash, expected_hash, sizeof(hash)) != 0) {
    return 1;
  }

  int error;
  PCRE2_SIZE offset;
  pcre2_code *pattern = pcre2_compile((PCRE2_SPTR) "^\\p{L}+-(\\d+)$", PCRE2_ZERO_TERMINATED, PCRE2_UTF | PCRE2_UCP,
                                      &error, &offset, NULL);
  if (!pattern) {
    return 2;
  }
  pcre2_match_data *matches = pcre2_match_data_create_from_pattern(pattern, NULL);
  if (!matches) {
    pcre2_code_free(pattern);
    return 2;
  }
  int count = pcre2_match(pattern, (PCRE2_SPTR) "caf\xc3\xa9-42", 8, 0, 0, matches, NULL);
  PCRE2_SIZE *positions = pcre2_get_ovector_pointer(matches);
  int matched = count == 2 && positions[2] == 6 && positions[3] == 8;
  pcre2_match_data_free(matches);
  pcre2_code_free(pattern);
  if (!matched) {
    return 2;
  }

  OpusEncoder *encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);
  if (!encoder || error != OPUS_OK) {
    return 3;
  }
  opus_int16 pcm[960];
  for (int i = 0; i < 960; i++) {
    pcm[i] = (opus_int16)(((i % 80) - 40) * 400);
  }
  unsigned char packet[1275];
  int bytes = opus_encode(encoder, pcm, 960, packet, sizeof(packet));
  opus_encoder_destroy(encoder);
  if (bytes <= 0 || opus_packet_get_nb_samples(packet, bytes, 48000) != 960) {
    return 3;
  }
  OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
  if (!decoder || error != OPUS_OK) {
    return 3;
  }
  opus_int16 decoded[960];
  int samples = opus_decode(decoder, packet, bytes, decoded, 960, 0);
  opus_decoder_destroy(decoder);
  if (samples != 960) {
    return 3;
  }
  int audible = 0;
  for (int i = 0; i < samples; i++) {
    audible |= decoded[i] != 0;
  }
  if (!audible) {
    return 3;
  }
  puts("PASS: libsodium BLAKE2b, PCRE2 Unicode captures, and Opus encode/decode");
  return 0;
}
