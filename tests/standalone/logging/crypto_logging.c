#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/time.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "Logging regression failed at line %d: %s", __LINE__, #expr);                      \
  } while (0)

// Exercise production crypto and logging without Criterion, including on Windows.
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  log_init(argv[1], LOG_DEBUG, false, false);
  log_set_terminal_output(false);
  log_set_flush_delay(0);

  crypto_context_t sender, receiver;
  CHECK(crypto_init(&sender) == CRYPTO_OK);
  CHECK(crypto_init(&receiver) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&sender, receiver.public_key) == CRYPTO_OK);
  CHECK(crypto_set_peer_public_key(&receiver, sender.public_key) == CRYPTO_OK);
  // Recognizable test-only key: a failure must never serialize it into a log.
  memset(sender.shared_key, 0xb7, sizeof(sender.shared_key));
  memset(receiver.shared_key, 0xb7, sizeof(receiver.shared_key));
  const uint8_t message[] = "logging-test-private-payload";
  uint8_t encrypted[256], decrypted[256];
  size_t encrypted_len = 0, decrypted_len = 0;
  for (int i = 0; i < 1000; ++i) {
    CHECK(crypto_encrypt(&sender, message, sizeof(message), encrypted, sizeof(encrypted), &encrypted_len) == CRYPTO_OK);
    CHECK(crypto_decrypt(&receiver, encrypted, encrypted_len, decrypted, sizeof(decrypted), &decrypted_len) ==
          CRYPTO_OK);
    CHECK(decrypted_len == sizeof(message));
    CHECK(memcmp(message, decrypted, sizeof(message)) == 0);
  }
  encrypted[encrypted_len - 1] ^= 1;
  for (int i = 0; i < 1000; ++i) {
    CHECK(crypto_decrypt(&receiver, encrypted, encrypted_len, decrypted, sizeof(decrypted), &decrypted_len) ==
          CRYPTO_ERROR_INVALID_MAC);
    CLEAR_ERRNO();
  }
  // After the interval, a persistent failure must become visible again.
  platform_sleep_ns(1100 * NS_PER_MS_INT);
  CHECK(crypto_decrypt(&receiver, encrypted, encrypted_len, decrypted, sizeof(decrypted), &decrypted_len) ==
        CRYPTO_ERROR_INVALID_MAC);
  CLEAR_ERRNO();
  crypto_destroy(&sender);
  crypto_destroy(&receiver);
  log_destroy();
  return 0;
}
