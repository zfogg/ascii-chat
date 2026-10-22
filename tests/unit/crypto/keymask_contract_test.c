#include <criterion/criterion.h>
#include <ascii-chat/crypto/key_identity/keymask.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
}
TestSuite(keymask_contract, .init = setup, .fini = log_destroy);

Test(keymask_contract, fingerprint_matches_sha256_known_answer_and_rejects_missing_arguments) {
  const uint8_t key[32] = {0};
  const uint8_t expected[32] = {0x66, 0x68, 0x7a, 0xad, 0xf8, 0x62, 0xbd, 0x77, 0x6c, 0x8f, 0xc1,
                                0x8b, 0x8e, 0x9f, 0x8e, 0x20, 0x08, 0x97, 0x14, 0x85, 0x6e, 0xe2,
                                0x33, 0xb3, 0x90, 0x2a, 0x59, 0x1d, 0x0d, 0x5f, 0x29, 0x25};
  uint8_t actual[32];
  cr_assert_eq(key_fingerprint_digest(key, actual), ASCIICHAT_OK);
  cr_assert_arr_eq(actual, expected, sizeof(actual));
  cr_assert_eq(key_fingerprint_digest(NULL, actual), ERROR_INVALID_PARAM);
  cr_assert_eq(key_fingerprint_digest(key, NULL), ERROR_INVALID_PARAM);
}

Test(keymask_contract, unicode_preserves_geometry_and_exact_capacity_is_sufficient) {
  for (unsigned variant = 0; variant < 32; ++variant) {
    uint8_t digest[32] = {0};
    digest[variant] = 1;
    char ascii[KEYMASK_BUFFER_SIZE], unicode[KEYMASK_BUFFER_SIZE], again[KEYMASK_BUFFER_SIZE];
    cr_assert_eq(keymask_render(digest, false, ascii, sizeof(ascii)), ASCIICHAT_OK);
    cr_assert_eq(keymask_render(digest, true, unicode, sizeof(unicode)), ASCIICHAT_OK);
    cr_assert_eq(strlen(ascii), KEYMASK_ROWS * (KEYMASK_WIDTH + 1));
    size_t a = 0, u = 0;
    while (ascii[a]) {
      if (ascii[a] == '#') {
        cr_assert_arr_eq(unicode + u, "\xe2\x96\x91", 3);
        u += 3;
      } else {
        cr_assert_eq(unicode[u++], ascii[a]);
      }
      ++a;
    }
    cr_assert_eq(unicode[u], '\0');
    cr_assert_eq(keymask_render(digest, true, again, u + 1), ASCIICHAT_OK);
    cr_assert_str_eq(again, unicode);
    memset(again, 'X', sizeof(again));
    cr_assert_eq(keymask_render(digest, true, again, u), ERROR_INVALID_PARAM);
    cr_assert_eq(again[0], '\0');
    cr_assert_eq(again[u], 'X');
    cr_assert_eq(keymask_render(digest, false, again, a + 1), ASCIICHAT_OK);
    cr_assert_str_eq(again, ascii);
    cr_assert_eq(keymask_render(digest, false, again, a), ERROR_INVALID_PARAM);
  }
}

Test(keymask_contract, invalid_inputs_leave_output_empty_and_zero_capacity_untouched) {
  uint8_t digest[32] = {0};
  char output[] = "guard";
  cr_assert_eq(keymask_render(digest, false, NULL, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(keymask_render(digest, false, output, 0), ERROR_INVALID_PARAM);
  cr_assert_str_eq(output, "guard");
  cr_assert_eq(keymask_render(NULL, false, output, sizeof(output)), ERROR_INVALID_PARAM);
  cr_assert_eq(output[0], '\0');
}
