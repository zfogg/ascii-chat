#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/crypto/key_identity/keymask.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
}
TEST_CASE("keymask_contract/fingerprint_matches_sha256_known_answer_and_rejects_missing_arguments", "[keymask_contract]") {
  const uint8_t key[32] = {0};
  const uint8_t expected[32] = {0x66, 0x68, 0x7a, 0xad, 0xf8, 0x62, 0xbd, 0x77, 0x6c, 0x8f, 0xc1,
                                0x8b, 0x8e, 0x9f, 0x8e, 0x20, 0x08, 0x97, 0x14, 0x85, 0x6e, 0xe2,
                                0x33, 0xb3, 0x90, 0x2a, 0x59, 0x1d, 0x0d, 0x5f, 0x29, 0x25};
  uint8_t actual[32];
  ASCIICHAT_REQUIRE_EQ(key_fingerprint_digest(key, actual), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_ARRAY_EQ(actual, expected, sizeof(actual));
  ASCIICHAT_REQUIRE_EQ(key_fingerprint_digest(NULL, actual), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(key_fingerprint_digest(key, NULL), ERROR_INVALID_PARAM);
}

TEST_CASE("keymask_contract/unicode_preserves_geometry_and_exact_capacity_is_sufficient", "[keymask_contract]") {
  for (unsigned variant = 0; variant < 32; ++variant) {
    uint8_t digest[32] = {0};
    digest[variant] = 1;
    char ascii[KEYMASK_BUFFER_SIZE], unicode[KEYMASK_BUFFER_SIZE], again[KEYMASK_BUFFER_SIZE];
    ASCIICHAT_REQUIRE_EQ(keymask_render(digest, false, ascii, sizeof(ascii)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(keymask_render(digest, true, unicode, sizeof(unicode)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(strlen(ascii), KEYMASK_ROWS * (KEYMASK_WIDTH + 1));
    size_t a = 0, u = 0;
    while (ascii[a]) {
      if (ascii[a] == '#') {
        ASCIICHAT_REQUIRE_ARRAY_EQ(unicode + u, "\xe2\x96\x91", 3);
        u += 3;
      } else {
        ASCIICHAT_REQUIRE_EQ(unicode[u++], ascii[a]);
      }
      ++a;
    }
    ASCIICHAT_REQUIRE_EQ(unicode[u], '\0');
    ASCIICHAT_REQUIRE_EQ(keymask_render(digest, true, again, u + 1), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(again, unicode);
    memset(again, 'X', sizeof(again));
    ASCIICHAT_REQUIRE_EQ(keymask_render(digest, true, again, u), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(again[0], '\0');
    ASCIICHAT_REQUIRE_EQ(again[u], 'X');
    ASCIICHAT_REQUIRE_EQ(keymask_render(digest, false, again, a + 1), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(again, ascii);
    ASCIICHAT_REQUIRE_EQ(keymask_render(digest, false, again, a), ERROR_INVALID_PARAM);
  }
}

TEST_CASE("keymask_contract/invalid_inputs_leave_output_empty_and_zero_capacity_untouched", "[keymask_contract]") {
  uint8_t digest[32] = {0};
  char output[] = "guard";
  ASCIICHAT_REQUIRE_EQ(keymask_render(digest, false, NULL, 1), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(keymask_render(digest, false, output, 0), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_STR_EQ(output, "guard");
  ASCIICHAT_REQUIRE_EQ(keymask_render(NULL, false, output, sizeof(output)), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(output[0], '\0');
}
