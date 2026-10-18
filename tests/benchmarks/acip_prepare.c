/** Isolated preparation benchmark; no sockets, queue copies, or kernel time.
 * Legacy allocation/copy order matches bf4415dd8a20c8f5423c98c44524cc1b3f42b2c1.
 * Compile the production helper with local instrumentation, leaving release code
 * and the timed crypto implementation untouched.
 */
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "%s", #expr);                                                                      \
  } while (0)
static size_t allocations, copies;
static void *measured_alloc(buffer_domain_t *domain, size_t size) {
  ++allocations;
  return buffer_alloc(domain, size);
}
static void *measured_copy(void *to, const void *from, size_t size) {
  copies += size;
  return memcpy(to, from, size);
}
#define buffer_alloc measured_alloc
#define memcpy measured_copy
#define acip_packet_plan measured_plan
#define acip_packet_encrypt measured_encrypt
#include "../../lib/network/acip/prepare.c"

static void prepare(crypto_context_t *crypto, const socket_buffer_t *slices, size_t count, size_t size, int transport,
                    bool legacy) {
  // 0: synchronous TCP, 1: async TCP, 2: WebSocket (already contiguous).
  if (!crypto) {
    if (!legacy) {
      acip_packet_plan_t plan;
      CHECK(acip_packet_plan(NULL, slices, count, &plan) == ASCIICHAT_OK && !plan.encrypt);
    }
    return;
  }
  packet_header_t outer;
  size_t capacity = size + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE, length = 0;
  uint8_t *storage = buffer_alloc(NULL, capacity + (transport == 1 ? 0 : sizeof(outer)));
  CHECK(storage);
  uint8_t *ciphertext = storage + (transport == 1 ? 0 : sizeof(outer));
  if (legacy) {
    uint8_t *gathered = NULL;
    const uint8_t *plain = slices[0].data;
    if (transport == 1 || count > 1) {
      gathered = buffer_alloc(NULL, size);
      CHECK(gathered);
      size_t offset = 0;
      for (size_t i = 0; i < count; ++i) {
        if (slices[i].len)
          memcpy(gathered + offset, slices[i].data, slices[i].len);
        offset += slices[i].len;
      }
      plain = gathered;
    }
    CHECK(crypto_encrypt(crypto, plain, size, ciphertext, capacity, &length) == CRYPTO_OK);
    buffer_release(gathered);
    outer = (packet_header_t){.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                              .type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED),
                              .length = HOST_TO_NET_U32((uint32_t)length),
                              .crc32 = HOST_TO_NET_U32(asciichat_crc32(ciphertext, length)),
                              .client_id = 0};
    if (transport == 2) {
      uint8_t *wire = buffer_alloc(NULL, sizeof(outer) + length);
      CHECK(wire);
      memcpy(wire, &outer, sizeof(outer));
      memcpy(wire + sizeof(outer), ciphertext, length);
      buffer_release(wire);
    } else if (transport == 0) {
      memcpy(storage, &outer, sizeof(outer));
    }
  } else {
    acip_packet_plan_t plan;
    CHECK(acip_packet_plan(crypto, slices, count, &plan) == ASCIICHAT_OK && plan.encrypt);
    CHECK(acip_packet_encrypt(crypto, &plan, NULL, ciphertext, capacity, &outer, &length) == ASCIICHAT_OK);
    if (transport != 1)
      memcpy(storage, &outer, sizeof(outer));
  }
  buffer_release(storage);
}

int main(void) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  crypto_context_t crypto;
  CHECK(crypto_init(&crypto) == CRYPTO_OK);
  crypto.key_exchange_complete = true;
  const size_t size = 65536, iterations = 2000;
  uint8_t *packet = buffer_alloc(NULL, size);
  CHECK(packet);
  memset(packet, 0x5a, size);
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC), .type = HOST_TO_NET_U16(PACKET_TYPE_IMAGE_FRAME)};
  memcpy(packet, &header, sizeof(header));
  socket_buffer_t slices[] = {{packet, sizeof(header)}, {packet + sizeof(header), size - sizeof(header)}};
  socket_buffer_t contiguous = {packet, size};
  for (int repeat = 0; repeat < 3; ++repeat) {
    for (int transport = 0; transport < 3; ++transport) {
      for (int vector = 0; vector < (transport == 2 ? 1 : 2); ++vector) {
        for (int encrypted = 0; encrypted < 2; ++encrypted) {
          for (int order = 0; order < 2; ++order) {
            bool legacy = (order + repeat) % 2 == 0;
            allocations = copies = 0;
            uint64_t start = time_get_ns();
            for (size_t i = 0; i < iterations; ++i)
              prepare(encrypted ? &crypto : NULL, vector ? slices : &contiguous, vector ? 2 : 1, size, transport,
                      legacy);
            uint64_t elapsed = time_get_ns() - start;
            log_warn("prepare-bench run=%d transport=%d vectors=%d encrypted=%d legacy=%d allocs=%.0f copy_bytes=%.0f "
                     "MiB/s=%.2f",
                     repeat, transport, vector, encrypted, legacy, (double)allocations / iterations,
                     (double)copies / iterations, (double)size * iterations * 1e9 / elapsed / 1048576.0);
          }
        }
      }
    }
  }
  buffer_release(packet);
  crypto_destroy(&crypto);
  buffer_system_cleanup();
  CHECK(buffer_process_reserved() == 0);
  log_destroy();
  return 0;
}
