#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/key_types.h>

#define KEY_IDENTITY_BUFFER_SIZE 2048

/** Format public identity with full fingerprint and optional Keymask v2.
 * cols is the available content width; insufficient width suppresses the art.
 */
asciichat_error_t key_identity_format(const public_key_t *key, bool art, bool unicode, int cols, char *out,
                                      size_t size);

/** Format using the current terminal capabilities and snapshot policy. */
asciichat_error_t key_identity_format_terminal(const public_key_t *key, char *out, size_t size);

/** Announce a public identity using the existing notice/logging policy. */
void key_identity_announce(const char *label, const public_key_t *key);
