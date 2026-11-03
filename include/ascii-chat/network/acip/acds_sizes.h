#pragma once

/** Fixed ACDS wire storage widths in bytes. Text fields retain their existing
 * padding/NUL convention; changing these widths requires a protocol change.
 * They are distinct from generated session-name and general DNS limits. */
#define ACIP_MAX_SESSION_STRING_LEN 48
#define ACIP_ADDRESS_SIZE 64
#define ACIP_PASSWORD_SIZE 128
#define ACIP_PASSWORD_HASH_SIZE 128
#define ACIP_ERROR_MESSAGE_SIZE 128
#define ACIP_SIGNAL_ERROR_MESSAGE_SIZE 256
#define ACIP_TURN_AUTH_SIZE 128
