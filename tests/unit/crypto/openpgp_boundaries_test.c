#include <criterion/criterion.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/common.h>
#include <sodium.h>

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
}
static void cleanup(void) {
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(openpgp_boundaries, .init = setup, .fini = cleanup);

static void key_body(uint8_t body[76]) {
  memset(body, 0, 76);
  body[0] = 4;
  body[1] = 1;
  body[2] = 2;
  body[3] = 3;
  body[4] = 4;
  body[5] = 22;
  body[6] = 0x40;
  for (unsigned i = 0; i < 32; ++i) {
    body[7 + i] = (uint8_t)(i + 1);
    body[42 + i] = (uint8_t)(0x80 + i);
  }
  body[40] = 1;
  body[41] = 0;
}

static void armor(const char *kind, const uint8_t *packet, size_t length, char output[1024]) {
  char encoded[512];
  cr_assert_not_null(sodium_bin2base64(encoded, sizeof(encoded), packet, length, sodium_base64_VARIANT_ORIGINAL));
  SAFE_SNPRINTF(output, 1024, "-----BEGIN PGP %s KEY BLOCK-----\n\n%s\n=AAAA\n-----END PGP %s KEY BLOCK-----\n", kind,
                encoded, kind);
}

Test(openpgp_boundaries, short_eddsa_bodies_are_bounded_even_when_no_prefix_exists) {
  for (size_t length = 6; length < 39; ++length) {
    uint8_t *body = SAFE_CALLOC(length, 1, uint8_t *);
    body[0] = 4;
    body[5] = 22;
    openpgp_public_key_t pub;
    openpgp_secret_key_t secret;
    cr_assert_neq(openpgp_parse_public_key_packet(body, length, &pub), ASCIICHAT_OK, "length %zu", length);
    cr_assert_neq(openpgp_parse_secret_key_packet(body, length, &secret), ASCIICHAT_OK, "length %zu", length);
    SAFE_FREE(body);
    CLEAR_ERRNO_ALL();
  }
}

Test(openpgp_boundaries, old_and_new_packet_lengths_decode_and_reject_every_truncated_header) {
  const struct {
    uint8_t data[6];
    size_t size, body;
    bool modern;
  } vectors[] = {{{0xc6, 191}, 2, 191, true},
                 {{0xc6, 192, 0}, 3, 192, true},
                 {{0xc6, 223, 255}, 3, 8383, true},
                 {{0xc6, 255, 1, 2, 3, 4}, 6, 16909060, true},
                 {{0x98, 17}, 2, 17, false},
                 {{0x99, 1, 2}, 3, 258, false},
                 {{0x9a, 1, 2, 3, 4}, 5, 16909060, false}};
  for (size_t i = 0; i < ARRAY_SIZE(vectors); ++i) {
    openpgp_packet_header_t header;
    for (size_t n = 0; n < vectors[i].size; ++n) {
      cr_assert_neq(openpgp_parse_packet_header(vectors[i].data, n, &header), ASCIICHAT_OK);
      CLEAR_ERRNO_ALL();
    }
    cr_assert_eq(openpgp_parse_packet_header(vectors[i].data, vectors[i].size, &header), ASCIICHAT_OK);
    cr_assert_eq(header.tag, 6);
    cr_assert_eq(header.header_len, vectors[i].size);
    cr_assert_eq(header.length, vectors[i].body);
    cr_assert_eq(header.new_format, vectors[i].modern);
  }
  openpgp_packet_header_t header;
  const uint8_t partial[] = {0xc6, 224}, indefinite[] = {0x9b, 0}, bad[] = {0x06, 0};
  cr_assert_neq(openpgp_parse_packet_header(partial, sizeof(partial), &header), ASCIICHAT_OK);
  cr_assert_neq(openpgp_parse_packet_header(indefinite, sizeof(indefinite), &header), ASCIICHAT_OK);
  cr_assert_neq(openpgp_parse_packet_header(bad, sizeof(bad), &header), ASCIICHAT_OK);
  cr_assert_eq(openpgp_parse_packet_header(NULL, 1, &header), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_packet_header(bad, 1, NULL), ERROR_INVALID_PARAM);
}

Test(openpgp_boundaries, binary_and_armored_keys_return_exact_material_and_skip_unrelated_packets) {
  uint8_t body[76];
  key_body(body);
  uint8_t packet[81] = {0xcd, 1, 'x', 0xc5, 76};
  memcpy(packet + 5, body, sizeof(body));
  uint8_t pub[32], seed[32];
  cr_assert_eq(openpgp_parse_binary_seckey(packet, sizeof(packet), pub, seed), ASCIICHAT_OK);
  cr_assert_arr_eq(pub, body + 7, 32);
  cr_assert_arr_eq(seed, body + 42, 32);
  char text[1024];
  armor("SECRET", packet, sizeof(packet), text);
  cr_assert_eq(openpgp_parse_armored_seckey(text, pub, seed), ASCIICHAT_OK);
  cr_assert_arr_eq(seed, body + 42, 32);
  armor("PRIVATE", packet, sizeof(packet), text);
  cr_assert_eq(openpgp_parse_armored_seckey(text, pub, seed), ASCIICHAT_OK);
  packet[3] = 0xc6;
  packet[4] = 39;
  cr_assert_eq(openpgp_parse_binary_pubkey(packet, 44, pub), ASCIICHAT_OK);
  cr_assert_arr_eq(pub, body + 7, 32);
  armor("PUBLIC", packet, 44, text);
  cr_assert_eq(openpgp_parse_armored_pubkey(text, pub), ASCIICHAT_OK);
  cr_assert_arr_eq(pub, body + 7, 32);
}

Test(openpgp_boundaries, truncated_outer_packets_never_publish_partial_key_material) {
  uint8_t body[76];
  key_body(body);
  uint8_t packet[78] = {0xc5, 76};
  memcpy(packet + 2, body, sizeof(body));
  uint8_t pub[32], seed[32], unchanged[32];
  memset(unchanged, 0xa5, sizeof(unchanged));
  for (size_t n = 1; n < sizeof(packet); ++n) {
    memcpy(pub, unchanged, 32);
    memcpy(seed, unchanged, 32);
    cr_assert_neq(openpgp_parse_binary_seckey(packet, n, pub, seed), ASCIICHAT_OK);
    cr_assert_arr_eq(pub, unchanged, 32);
    cr_assert_arr_eq(seed, unchanged, 32);
    char text[1024];
    armor("PRIVATE", packet, n, text);
    cr_assert_neq(openpgp_parse_armored_seckey(text, pub, seed), ASCIICHAT_OK, "armored length %zu", n);
    cr_assert_arr_eq(pub, unchanged, 32);
    cr_assert_arr_eq(seed, unchanged, 32);
    CLEAR_ERRNO_ALL();
  }
  packet[0] = 0xc6;
  packet[1] = 39;
  for (size_t n = 1; n < 41; ++n) {
    cr_assert_neq(openpgp_parse_binary_pubkey(packet, n, pub), ASCIICHAT_OK);
    CLEAR_ERRNO_ALL();
  }
}

Test(openpgp_boundaries, unsupported_versions_algorithms_and_encryption_are_rejected) {
  uint8_t body[76];
  key_body(body);
  openpgp_public_key_t pub;
  openpgp_secret_key_t secret;
  body[0] = 3;
  cr_assert_neq(openpgp_parse_public_key_packet(body, sizeof(body), &pub), ASCIICHAT_OK);
  cr_assert_neq(openpgp_parse_secret_key_packet(body, sizeof(body), &secret), ASCIICHAT_OK);
  body[0] = 4;
  body[5] = 18;
  cr_assert_neq(openpgp_parse_public_key_packet(body, sizeof(body), &pub), ASCIICHAT_OK);
  cr_assert_neq(openpgp_parse_secret_key_packet(body, sizeof(body), &secret), ASCIICHAT_OK);
  body[5] = 22;
  body[39] = 254;
  cr_assert_eq(openpgp_parse_secret_key_packet(body, sizeof(body), &secret), ASCIICHAT_OK);
  cr_assert(secret.is_encrypted);
  uint8_t packet[78] = {0xc5, 76}, key[32], seed[32];
  memcpy(packet + 2, body, 76);
  cr_assert_eq(openpgp_parse_binary_seckey(packet, sizeof(packet), key, seed), ERROR_NOT_SUPPORTED);
  body[39] = 0;
  cr_assert_neq(openpgp_parse_secret_key_packet(body, 39, &secret), ASCIICHAT_OK);
  cr_assert_neq(openpgp_parse_secret_key_packet(body, 40, &secret), ASCIICHAT_OK);
  body[40] = 0;
  body[41] = 0;
  cr_assert_neq(openpgp_parse_secret_key_packet(body, 76, &secret), ASCIICHAT_OK);
  body[40] = 1;
  body[41] = 1;
  cr_assert_neq(openpgp_parse_secret_key_packet(body, 76, &secret), ASCIICHAT_OK);
}

Test(openpgp_boundaries, mpi_prefix_and_bit_count_are_checked_before_copying) {
  uint8_t mpi[35] = {1, 7, 0x40}, key[32];
  for (unsigned i = 0; i < 32; ++i)
    mpi[i + 3] = (uint8_t)i;
  cr_assert_eq(openpgp_extract_ed25519_from_mpi(mpi, sizeof(mpi), key), ASCIICHAT_OK);
  cr_assert_arr_eq(key, mpi + 3, 32);
  mpi[0] = 0;
  mpi[1] = 255;
  cr_assert_neq(openpgp_extract_ed25519_from_mpi(mpi, sizeof(mpi), key), ASCIICHAT_OK);
  mpi[0] = 1;
  mpi[1] = 15;
  cr_assert_neq(openpgp_extract_ed25519_from_mpi(mpi, sizeof(mpi), key), ASCIICHAT_OK);
  mpi[1] = 7;
  mpi[2] = 0x41;
  cr_assert_neq(openpgp_extract_ed25519_from_mpi(mpi, sizeof(mpi), key), ASCIICHAT_OK);
  cr_assert_eq(openpgp_extract_ed25519_from_mpi(NULL, 35, key), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_extract_ed25519_from_mpi(mpi, 35, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_extract_ed25519_from_mpi(mpi, 34, key), ERROR_INVALID_PARAM);
}

Test(openpgp_boundaries, malformed_armor_and_null_outputs_are_reported_as_errors) {
  uint8_t pub[32], seed[32], byte = 0;
  uint8_t *decoded = NULL;
  size_t count;
  const char *bad_public[] = {"", "-----BEGIN PGP PUBLIC KEY BLOCK-----", "-----BEGIN PGP PUBLIC KEY BLOCK-----\n",
                              "-----BEGIN PGP PUBLIC KEY BLOCK-----\n!\n-----END PGP PUBLIC KEY BLOCK-----"};
  const char *bad_secret[] = {"", "-----BEGIN PGP SECRET KEY BLOCK-----", "-----BEGIN PGP PRIVATE KEY BLOCK-----\n",
                              "-----BEGIN PGP SECRET KEY BLOCK-----\n!\n-----END PGP SECRET KEY BLOCK-----"};
  for (size_t i = 0; i < 4; ++i) {
    cr_assert_neq(openpgp_parse_armored_pubkey(bad_public[i], pub), ASCIICHAT_OK);
    cr_assert_neq(openpgp_parse_armored_seckey(bad_secret[i], pub, seed), ASCIICHAT_OK);
  }
  cr_assert_eq(openpgp_parse_armored_pubkey(NULL, pub), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_armored_pubkey("", NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_armored_seckey(NULL, pub, seed), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_armored_seckey("", NULL, seed), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_armored_seckey("", pub, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_binary_pubkey(NULL, 1, pub), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_binary_pubkey(&byte, 0, pub), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_binary_pubkey(&byte, 1, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_binary_seckey(NULL, 1, pub, seed), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_binary_seckey(&byte, 0, pub, seed), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_binary_seckey(&byte, 1, NULL, seed), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_parse_binary_seckey(&byte, 1, pub, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_base64_decode(NULL, 0, &decoded, &count), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_base64_decode("AA==", 4, NULL, &count), ERROR_INVALID_PARAM);
  cr_assert_eq(openpgp_base64_decode("AA==", 4, &decoded, NULL), ERROR_INVALID_PARAM);
}
