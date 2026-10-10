#include <ascii-chat/common.h>
#include <ascii-chat/crypto/key_identity/display.h>
#include <ascii-chat/crypto/key_identity/keymask.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/notice.h>
#include <string.h>

asciichat_error_t key_identity_format(const public_key_t *key, bool art, bool unicode, int cols, char *out,
                                      size_t size) {
  if (!out || !size)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing identity output buffer");
  out[0] = '\0';
  if (!key)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing public identity");
  const char *type;
  switch (key->type) {
  case KEY_TYPE_ED25519:
    type = "Ed25519";
    break;
  case KEY_TYPE_GPG:
    type = "Ed25519 (GPG)";
    break;
  case KEY_TYPE_X25519:
    type = "X25519";
    break;
  default:
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported public identity type");
  }
  uint8_t digest[KEYMASK_DIGEST_SIZE];
  asciichat_error_t result = key_fingerprint_digest(key->key, digest);
  if (result != ASCIICHAT_OK)
    return result;
  char hex[65];
  for (size_t i = 0; i < sizeof(digest); ++i) {
    hex[i * 2] = "0123456789abcdef"[digest[i] >> 4];
    hex[i * 2 + 1] = "0123456789abcdef"[digest[i] & 15];
  }
  hex[64] = '\0';
  char mask[KEYMASK_BUFFER_SIZE] = "";
  bool show = art && cols >= KEYMASK_WIDTH;
  if (show && (result = keymask_render(digest, unicode, mask, sizeof(mask))) != ASCIICHAT_OK)
    return result;
  const char *heading = show ? "Keymask v2 / SHA-256\n" : "";
  size_t needed = strlen(type) + strlen(" public key\n") + strlen(heading) + strlen(mask) + strlen("SHA256:") + 64 + 2;
  if (size < needed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Identity output needs %zu bytes", needed);
  safe_snprintf(out, size, "%s public key\n%s%sSHA256:%s\n", type, heading, mask, hex);
  return ASCIICHAT_OK;
}

asciichat_error_t key_identity_format_terminal(const public_key_t *key, char *out, size_t size) {
  bool tty = platform_isatty(STDERR_FILENO);
  int mode = GET_OPTION(key_art);
  bool art = mode == 1 || (mode == 0 && tty && !GET_OPTION(snapshot_mode));
  int cols = tty ? ui_controller_size().cols : 80;
  // Notices reserve borders, padding, and the terminal's final column.
  cols = (cols > 81 ? 80 : cols - 1) - 4;
  return key_identity_format(key, art, tty && terminal_supports_utf8(), cols, out, size);
}

void key_identity_announce(const char *label, const public_key_t *key) {
  char identity[KEY_IDENTITY_BUFFER_SIZE];
  if (key_identity_format_terminal(key, identity, sizeof(identity)) == ASCIICHAT_OK)
    NOTICE_ANNOUNCE(label, "%s", identity);
}
