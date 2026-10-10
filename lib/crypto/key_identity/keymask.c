#include <ascii-chat/crypto/key_identity/keymask.h>
#include <sodium.h>
#include <string.h>

asciichat_error_t key_fingerprint_digest(const uint8_t public_key[32], uint8_t digest[KEYMASK_DIGEST_SIZE]) {
  if (!public_key || !digest)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing public key or fingerprint output");
  if (crypto_hash_sha256(digest, public_key, 32) != 0)
    return SET_ERRNO(ERROR_CRYPTO, "Cannot compute public-key fingerprint");
  return ASCIICHAT_OK;
}

asciichat_error_t keymask_render(const uint8_t digest[KEYMASK_DIGEST_SIZE], bool unicode, char *out, size_t size) {
  if (!out || !size)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing Keymask output buffer");
  out[0] = '\0';
  if (!digest)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing Keymask digest");

  size_t required = KEYMASK_ROWS * (KEYMASK_WIDTH + 1) + 1;
  if (unicode) {
    for (size_t i = 0; i < KEYMASK_DIGEST_SIZE; ++i)
      for (unsigned bit = 0; bit < 8; ++bit)
        if (digest[i] & (1u << bit))
          required += 4; // Two mirrored blocks, each two bytes longer than ASCII.
  }
  if (size < required)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Keymask output needs %zu bytes", required);

  char *p = out;
  const char border[] = "+--------------------------------+\n";
  memcpy(p, border, sizeof(border) - 1);
  p += sizeof(border) - 1;
  for (size_t row = 0; row < 16; ++row) {
    *p++ = '|';
    for (size_t col = 0; col < 32; ++col) {
      size_t source_col = col < 16 ? col : 31 - col;
      bool set = (digest[row * 2 + source_col / 8] & (0x80u >> (source_col % 8))) != 0;
      if (set && unicode) {
        memcpy(p, "\xe2\x96\x88", 3);
        p += 3;
      } else {
        *p++ = set ? '#' : ' ';
      }
    }
    *p++ = '|';
    *p++ = '\n';
  }
  memcpy(p, border, sizeof(border));
  return ASCIICHAT_OK;
}
