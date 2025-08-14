#include "aes_hw.h"
#include "common.h"
#include <string.h>
#include <time.h>

// AES-128 S-box for SubBytes transformation
static const uint8_t aes_sbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9,
    0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F,
    0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15, 0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07,
    0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3,
    0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58,
    0xCF, 0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3,
    0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC, 0x5F,
    0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73, 0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88,
    0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC,
    0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A,
    0xAE, 0x08, 0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A, 0x70,
    0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF, 0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42,
    0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16};

// AES-128 inverse S-box for InvSubBytes transformation
static const uint8_t aes_inv_sbox[256] = {
    0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E, 0x81, 0xF3, 0xD7, 0xFB, 0x7C, 0xE3, 0x39,
    0x82, 0x9B, 0x2F, 0xFF, 0x87, 0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB, 0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2,
    0x23, 0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E, 0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2, 0x76,
    0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25, 0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xD4, 0xA4, 0x5C, 0xCC,
    0x5D, 0x65, 0xB6, 0x92, 0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D,
    0x84, 0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A, 0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06, 0xD0, 0x2C,
    0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02, 0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B, 0x3A, 0x91, 0x11, 0x41, 0x4F,
    0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73, 0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85,
    0xE2, 0xF9, 0x37, 0xE8, 0x1C, 0x75, 0xDF, 0x6E, 0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89, 0x6F, 0xB7, 0x62,
    0x0E, 0xAA, 0x18, 0xBE, 0x1B, 0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2, 0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD,
    0x5A, 0xF4, 0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F, 0x60,
    0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D, 0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF, 0xA0, 0xE0, 0x3B, 0x4D,
    0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61, 0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6,
    0x26, 0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D};

// Round constants for key expansion
static const uint8_t aes_rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};

// Key expansion for AES-128 (generates 11 round keys from master key)
static void aes_key_expansion(const uint8_t key[16], uint8_t round_keys[11][16]) {
  memcpy(round_keys[0], key, 16);

  for (int round = 1; round <= 10; round++) {
    uint8_t *prev_key = round_keys[round - 1];
    uint8_t *curr_key = round_keys[round];

    // Copy first 3 words from previous round key
    memcpy(curr_key, prev_key, 12);

    // Generate 4th word: RotWord + SubBytes + Rcon
    curr_key[12] = aes_sbox[prev_key[13]] ^ aes_rcon[round];
    curr_key[13] = aes_sbox[prev_key[14]];
    curr_key[14] = aes_sbox[prev_key[15]];
    curr_key[15] = aes_sbox[prev_key[12]];

    // XOR with first word of previous round key
    for (int i = 0; i < 4; i++) {
      curr_key[i] ^= curr_key[12 + i];
      curr_key[4 + i] ^= curr_key[i];
      curr_key[8 + i] ^= curr_key[4 + i];
      curr_key[12 + i] ^= curr_key[8 + i];
    }
  }
}

// Multi-architecture hardware acceleration support
#if defined(__aarch64__) || defined(__arm64__)
#include <arm_acle.h>
#include <arm_neon.h>
#define ARCH_ARM64
#elif defined(__x86_64__) && defined(HAVE_AES_HW)
#include <immintrin.h>
#include <wmmintrin.h>
#include <cpuid.h>
#define ARCH_X86_64
#elif defined(__i386__) && defined(HAVE_AES_HW)
#include <immintrin.h>
#include <wmmintrin.h>
#include <cpuid.h>
#define ARCH_X86_32
#elif defined(__powerpc64__) && defined(HAVE_AES_HW)
#include <altivec.h>
#define ARCH_PPC64
#elif defined(__riscv) && (__riscv_xlen == 64) && defined(HAVE_AES_HW)
// RISC-V crypto extensions (future)
#define ARCH_RISCV64
#endif

// Check if AES instructions are available at runtime
static bool aes_hw_available = false;
static bool aes_hw_checked = false;

static void check_aes_hw_support(void) {
  if (aes_hw_checked)
    return;

#ifdef ARCH_ARM64
// ARM64 AES detection
#ifdef __APPLE__
  // Apple Silicon always has AES crypto extensions
  aes_hw_available = true;
  log_debug("ARM AES hardware acceleration: enabled (Apple Silicon)");
#else
  // Linux ARM64: Check for crypto extensions
  // Could also check auxiliary vector (AT_HWCAP) for more accuracy
  FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
  if (cpuinfo) {
    char line[256];
    aes_hw_available = false;
    while (fgets(line, sizeof(line), cpuinfo)) {
      if (strstr(line, "Features") && strstr(line, "aes")) {
        aes_hw_available = true;
        break;
      }
    }
    fclose(cpuinfo);
  } else {
    // Fallback: assume modern ARM64 has AES
    aes_hw_available = true;
  }
  log_debug("ARM AES hardware acceleration: %s (Linux ARM64)", aes_hw_available ? "enabled" : "disabled");
#endif
#elif defined(ARCH_X86_64) || defined(ARCH_X86_32)
  // x86/x86_64 AES-NI detection
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
    aes_hw_available = (ecx & bit_AES) != 0;

    // Get CPU vendor for enhanced logging
    char vendor[13] = {0};
    __get_cpuid(0, &eax, &ebx, &ecx, &edx);
    memcpy(vendor, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);

    if (strstr(vendor, "AuthenticAMD")) {
      log_debug("AMD AES-NI hardware acceleration: %s", aes_hw_available ? "enabled" : "disabled");
    } else if (strstr(vendor, "GenuineIntel")) {
      log_debug("Intel AES-NI hardware acceleration: %s", aes_hw_available ? "enabled" : "disabled");
    } else {
      log_debug("x86 AES-NI hardware acceleration: %s (%s)", aes_hw_available ? "enabled" : "disabled", vendor);
    }
  } else {
    aes_hw_available = false;
    log_debug("x86 AES-NI hardware acceleration: disabled (CPUID unavailable)");
  }
#elif defined(ARCH_PPC64)
  // PowerPC POWER8+ AES detection
  FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
  if (cpuinfo) {
    char line[256];
    aes_hw_available = false;
    while (fgets(line, sizeof(line), cpuinfo)) {
      // Look for POWER8+ or crypto features
      if ((strstr(line, "POWER8") || strstr(line, "POWER9") || strstr(line, "POWER10")) ||
          (strstr(line, "Features") && strstr(line, "aes"))) {
        aes_hw_available = true;
        break;
      }
    }
    fclose(cpuinfo);
  }
  log_debug("PowerPC AES hardware acceleration: %s", aes_hw_available ? "enabled" : "disabled");
#elif defined(ARCH_RISCV64)
  // RISC-V crypto extensions (experimental/future)
  FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
  if (cpuinfo) {
    char line[256];
    aes_hw_available = false;
    while (fgets(line, sizeof(line), cpuinfo)) {
      if (strstr(line, "isa") && (strstr(line, "zkn") || strstr(line, "zks"))) {
        aes_hw_available = true;
        break;
      }
    }
    fclose(cpuinfo);
  }
  log_debug("RISC-V AES hardware acceleration: %s (experimental)", aes_hw_available ? "enabled" : "disabled");
#else
  aes_hw_available = false;
  log_debug("No hardware AES acceleration available for this architecture");
#endif

  aes_hw_checked = true;
}

bool aes_hw_is_available(void) {
  check_aes_hw_support();
  return aes_hw_available;
}

#ifdef ARCH_ARM64
// ARM AES hardware implementation using NEON crypto extensions
static int aes_arm_encrypt(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext,
                           uint8_t *iv) {
  // Generate random IV
  for (int i = 0; i < AES_IV_SIZE; i++) {
    iv[i] = (uint8_t)(rand() & 0xFF);
  }

  uint8x16_t iv_vec = vld1q_u8(iv);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t padded_block[AES_BLOCK_SIZE];

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;
    size_t block_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;

    memset(padded_block, 0, AES_BLOCK_SIZE);
    memcpy(padded_block, plaintext + block_start, block_len);

    uint8x16_t plain_vec = vld1q_u8(padded_block);

    // XOR with IV (CBC mode)
    plain_vec = veorq_u8(plain_vec, iv_vec);

    // ARM AES encryption using crypto extensions (full AES-128)
    uint8_t round_keys[11][16];
    aes_key_expansion(ctx->key, round_keys);

    uint8x16_t cipher_vec = plain_vec;

    // Initial AddRoundKey (round 0)
    cipher_vec = veorq_u8(cipher_vec, vld1q_u8(round_keys[0]));

    // Rounds 1-9: SubBytes, ShiftRows, MixColumns, AddRoundKey
    for (int round = 1; round <= 9; round++) {
      cipher_vec = vaesmcq_u8(vaeseq_u8(cipher_vec, vld1q_u8(round_keys[round])));
    }

    // Final round 10: SubBytes, ShiftRows, AddRoundKey (no MixColumns)
    cipher_vec = veorq_u8(vaeseq_u8(cipher_vec, vdupq_n_u8(0)), vld1q_u8(round_keys[10]));

    vst1q_u8(ciphertext + block_start, cipher_vec);
    iv_vec = cipher_vec; // Update IV for next block
  }

  return 0;
}
#endif

#if defined(ARCH_X86_64) || defined(ARCH_X86_32)
// Intel/AMD AES hardware implementation using AES-NI (works on both x86_64 and x86_32)
static int aes_x86_encrypt(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext,
                           uint8_t *iv) {
  // Generate random IV
  for (int i = 0; i < AES_IV_SIZE; i++) {
    iv[i] = (uint8_t)(rand() & 0xFF);
  }

  // Load key (simplified - real implementation would do key expansion)
  __m128i key_vec = _mm_loadu_si128((__m128i *)ctx->key);
  __m128i iv_vec = _mm_loadu_si128((__m128i *)iv);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t padded_block[AES_BLOCK_SIZE];

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;
    size_t block_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;

    // Prepare block with padding
    memset(padded_block, 0, AES_BLOCK_SIZE);
    memcpy(padded_block, plaintext + block_start, block_len);

    __m128i plain_vec = _mm_loadu_si128((__m128i *)padded_block);

    // XOR with IV (CBC mode)
    plain_vec = _mm_xor_si128(plain_vec, iv_vec);

    // Full AES-128 encryption using AES-NI
    uint8_t round_keys[11][16];
    aes_key_expansion(ctx->key, round_keys);

    __m128i cipher_vec = plain_vec;

    // Initial AddRoundKey (round 0)
    cipher_vec = _mm_xor_si128(cipher_vec, _mm_loadu_si128((__m128i *)round_keys[0]));

    // Rounds 1-9: SubBytes, ShiftRows, MixColumns, AddRoundKey
    for (int round = 1; round <= 9; round++) {
      cipher_vec = _mm_aesenc_si128(cipher_vec, _mm_loadu_si128((__m128i *)round_keys[round]));
    }

    // Final round 10: SubBytes, ShiftRows, AddRoundKey (no MixColumns)
    cipher_vec = _mm_aesenclast_si128(cipher_vec, _mm_loadu_si128((__m128i *)round_keys[10]));

    _mm_storeu_si128((__m128i *)(ciphertext + block_start), cipher_vec);
    iv_vec = cipher_vec; // Update IV for next block
  }

  return 0;
}
#endif

#ifdef ARCH_PPC64
// PowerPC AES hardware implementation using AltiVec/VSX (POWER8+)
static int aes_ppc64_encrypt(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext,
                             uint8_t *iv) {
  // Generate random IV
  for (int i = 0; i < AES_IV_SIZE; i++) {
    iv[i] = (uint8_t)(rand() & 0xFF);
  }

  // Load key and IV into vector registers
  vector unsigned char key_vec = vec_ld(0, (const vector unsigned char *)ctx->key);
  vector unsigned char iv_vec = vec_ld(0, (const vector unsigned char *)iv);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t padded_block[AES_BLOCK_SIZE];

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;
    size_t block_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;

    // Prepare block with padding
    memset(padded_block, 0, AES_BLOCK_SIZE);
    memcpy(padded_block, plaintext + block_start, block_len);

    vector unsigned char plain_vec = vec_ld(0, (const vector unsigned char *)padded_block);

    // XOR with IV (CBC mode)
    plain_vec = vec_xor(plain_vec, iv_vec);

// PowerPC AES encryption using real crypto extensions
#ifdef __CRYPTO
    // Use real POWER8+ AES crypto instructions with key expansion
    uint8_t round_keys[11][16] __attribute__((aligned(16)));
    aes_key_expansion(ctx->key, round_keys);

    vector unsigned char cipher_vec = plain_vec;

    // Initial AddRoundKey (round 0)
    cipher_vec = vec_xor(cipher_vec, vec_ld(0, (vector unsigned char *)round_keys[0]));

    // Rounds 1-9: SubBytes, ShiftRows, MixColumns, AddRoundKey
    for (int round = 1; round <= 9; round++) {
      cipher_vec = __builtin_crypto_vcipher(cipher_vec, vec_ld(0, (vector unsigned char *)round_keys[round]));
    }

    // Final round 10: SubBytes, ShiftRows, AddRoundKey (no MixColumns)
    cipher_vec = __builtin_crypto_vcipherlast(cipher_vec, vec_ld(0, (vector unsigned char *)round_keys[10]));
#else
    // No PowerPC crypto extensions available - fall back to software
    return aes_encrypt_sw(ctx, plaintext, len, ciphertext, iv);
#endif

    vec_st(cipher_vec, 0, (vector unsigned char *)(ciphertext + block_start));
    iv_vec = cipher_vec; // Update IV for next block
  }

  return 0;
}

// PowerPC AES hardware decryption implementation
static int aes_ppc64_decrypt(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                             const uint8_t *iv) {
  vector unsigned char key_vec = vec_ld(0, (const vector unsigned char *)ctx->key);
  vector unsigned char iv_vec = vec_ld(0, (const vector unsigned char *)iv);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t decrypted_block[AES_BLOCK_SIZE];

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;

    vector unsigned char cipher_vec = vec_ld(0, (const vector unsigned char *)(ciphertext + block_start));

// PowerPC AES decryption using real crypto extensions
#ifdef __CRYPTO
    // Use real POWER8+ AES crypto instructions with key expansion
    uint8_t round_keys[11][16] __attribute__((aligned(16)));
    aes_key_expansion(ctx->key, round_keys);

    vector unsigned char plain_vec = cipher_vec;

    // Initial AddRoundKey (round 10 key)
    plain_vec = vec_xor(plain_vec, vec_ld(0, (vector unsigned char *)round_keys[10]));

    // Inverse rounds 9-1: InvSubBytes, InvShiftRows, InvMixColumns, AddRoundKey
    for (int round = 9; round >= 1; round--) {
      plain_vec = __builtin_crypto_vncipher(plain_vec, vec_ld(0, (vector unsigned char *)round_keys[round]));
    }

    // Final round 0: InvSubBytes, InvShiftRows, AddRoundKey (no InvMixColumns)
    plain_vec = __builtin_crypto_vncipherlast(plain_vec, vec_ld(0, (vector unsigned char *)round_keys[0]));
#else
    // No PowerPC crypto extensions available - fall back to software
    return aes_decrypt_sw(ctx, ciphertext, len, plaintext, iv);
#endif

    // XOR with IV (CBC mode)
    plain_vec = vec_xor(plain_vec, iv_vec);

    vec_st(plain_vec, 0, (vector unsigned char *)decrypted_block);

    size_t copy_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;
    memcpy(plaintext + block_start, decrypted_block, copy_len);

    iv_vec = cipher_vec; // Update IV for next block
  }

  return 0;
}
#endif

#ifdef ARCH_RISCV64
// RISC-V AES hardware implementation using crypto extensions (Zkn/Zks)
static int aes_riscv64_encrypt(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext,
                               uint8_t *iv) {
  // Generate random IV
  for (int i = 0; i < AES_IV_SIZE; i++) {
    iv[i] = (uint8_t)(rand() & 0xFF);
  }

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t padded_block[AES_BLOCK_SIZE];

  // Load key and IV (RISC-V crypto extensions work with 64-bit registers)
  uint64_t key_lo, key_hi, iv_lo, iv_hi;
  memcpy(&key_lo, ctx->key, 8);
  memcpy(&key_hi, ctx->key + 8, 8);
  memcpy(&iv_lo, iv, 8);
  memcpy(&iv_hi, iv + 8, 8);

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;
    size_t block_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;

    // Prepare block with padding
    memset(padded_block, 0, AES_BLOCK_SIZE);
    memcpy(padded_block, plaintext + block_start, block_len);

    uint64_t plain_lo, plain_hi;
    memcpy(&plain_lo, padded_block, 8);
    memcpy(&plain_hi, padded_block + 8, 8);

    // XOR with IV (CBC mode)
    plain_lo ^= iv_lo;
    plain_hi ^= iv_hi;

// RISC-V AES encryption using real crypto extensions
#if defined(__riscv_zkn) || defined(__riscv_zks)
    // Use actual RISC-V AES instructions with key expansion
    uint8_t round_keys[11][16];
    aes_key_expansion(ctx->key, round_keys);

    uint32_t plain_w0 = (uint32_t)plain_lo;
    uint32_t plain_w1 = (uint32_t)(plain_lo >> 32);
    uint32_t plain_w2 = (uint32_t)plain_hi;
    uint32_t plain_w3 = (uint32_t)(plain_hi >> 32);

    // RISC-V AES round function using dedicated instructions
    // Round 0-10: Full AES-128 with proper round keys
    uint32_t s0 = plain_w0, s1 = plain_w1, s2 = plain_w2, s3 = plain_w3;

    // AddRoundKey for round 0
    uint32_t *rk0 = (uint32_t *)round_keys[0];
    s0 ^= rk0[0];
    s1 ^= rk0[1];
    s2 ^= rk0[2];
    s3 ^= rk0[3];

    // AES rounds 1-9 using RISC-V crypto instructions
    for (int round = 1; round <= 9; round++) {
      // RISC-V AES round instructions (SubBytes, ShiftRows, MixColumns)
      asm volatile("aes32esi %0, %1, 0" : "=r"(s0) : "r"(s1));
      asm volatile("aes32esi %0, %1, 1" : "=r"(s1) : "r"(s2));
      asm volatile("aes32esi %0, %1, 2" : "=r"(s2) : "r"(s3));
      asm volatile("aes32esi %0, %1, 3" : "=r"(s3) : "r"(s0));

      // AddRoundKey with proper round key
      uint32_t *rk = (uint32_t *)round_keys[round];
      s0 ^= rk[0];
      s1 ^= rk[1];
      s2 ^= rk[2];
      s3 ^= rk[3];
    }

    // Final round 10 (no MixColumns): aes32esmi
    asm volatile("aes32esmi %0, %1, 0" : "=r"(s0) : "r"(s1));
    asm volatile("aes32esmi %0, %1, 1" : "=r"(s1) : "r"(s2));
    asm volatile("aes32esmi %0, %1, 2" : "=r"(s2) : "r"(s3));
    asm volatile("aes32esmi %0, %1, 3" : "=r"(s3) : "r"(s0));

    // Final AddRoundKey
    uint32_t *rk10 = (uint32_t *)round_keys[10];
    s0 ^= rk10[0];
    s1 ^= rk10[1];
    s2 ^= rk10[2];
    s3 ^= rk10[3];

    uint64_t cipher_lo = ((uint64_t)s1 << 32) | s0;
    uint64_t cipher_hi = ((uint64_t)s3 << 32) | s2;
#else
    // No RISC-V crypto extensions available - fall back to software
    return aes_encrypt_sw(ctx, plaintext, len, ciphertext, iv);
#endif

    memcpy(ciphertext + block_start, &cipher_lo, 8);
    memcpy(ciphertext + block_start + 8, &cipher_hi, 8);

    // Update IV for next block
    iv_lo = cipher_lo;
    iv_hi = cipher_hi;
  }

  return 0;
}

// RISC-V AES hardware decryption implementation
static int aes_riscv64_decrypt(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                               const uint8_t *iv) {
  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t decrypted_block[AES_BLOCK_SIZE];

  // Load key and IV
  uint64_t key_lo, key_hi, iv_lo, iv_hi;
  memcpy(&key_lo, ctx->key, 8);
  memcpy(&key_hi, ctx->key + 8, 8);
  memcpy(&iv_lo, iv, 8);
  memcpy(&iv_hi, iv + 8, 8);

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;

    uint64_t cipher_lo, cipher_hi;
    memcpy(&cipher_lo, ciphertext + block_start, 8);
    memcpy(&cipher_hi, ciphertext + block_start + 8, 8);

// RISC-V AES decryption using real crypto extensions
#if defined(__riscv_zkn) || defined(__riscv_zks)
    // Use actual RISC-V AES decryption instructions with key expansion
    uint8_t round_keys[11][16];
    aes_key_expansion(ctx->key, round_keys);

    uint32_t cipher_w0 = (uint32_t)cipher_lo;
    uint32_t cipher_w1 = (uint32_t)(cipher_lo >> 32);
    uint32_t cipher_w2 = (uint32_t)cipher_hi;
    uint32_t cipher_w3 = (uint32_t)(cipher_hi >> 32);

    uint32_t s0 = cipher_w0, s1 = cipher_w1, s2 = cipher_w2, s3 = cipher_w3;

    // Initial AddRoundKey (round 10 key)
    uint32_t *rk10 = (uint32_t *)round_keys[10];
    s0 ^= rk10[0];
    s1 ^= rk10[1];
    s2 ^= rk10[2];
    s3 ^= rk10[3];

    // Inverse final round (no InvMixColumns): aes32dsmi
    asm volatile("aes32dsmi %0, %1, 0" : "=r"(s0) : "r"(s3));
    asm volatile("aes32dsmi %0, %1, 1" : "=r"(s1) : "r"(s0));
    asm volatile("aes32dsmi %0, %1, 2" : "=r"(s2) : "r"(s1));
    asm volatile("aes32dsmi %0, %1, 3" : "=r"(s3) : "r"(s2));

    // AddRoundKey for round 9
    uint32_t *rk9 = (uint32_t *)round_keys[9];
    s0 ^= rk9[0];
    s1 ^= rk9[1];
    s2 ^= rk9[2];
    s3 ^= rk9[3];

    // Inverse rounds 8-1: aes32dsi (InvSubBytes, InvShiftRows, InvMixColumns, AddRoundKey)
    for (int round = 8; round >= 1; round--) {
      // RISC-V inverse AES round instructions
      asm volatile("aes32dsi %0, %1, 0" : "=r"(s0) : "r"(s3));
      asm volatile("aes32dsi %0, %1, 1" : "=r"(s1) : "r"(s0));
      asm volatile("aes32dsi %0, %1, 2" : "=r"(s2) : "r"(s1));
      asm volatile("aes32dsi %0, %1, 3" : "=r"(s3) : "r"(s2));

      // AddRoundKey with proper round key
      uint32_t *rk = (uint32_t *)round_keys[round];
      s0 ^= rk[0];
      s1 ^= rk[1];
      s2 ^= rk[2];
      s3 ^= rk[3];
    }

    // Final AddRoundKey (round 0 key)
    uint32_t *rk0 = (uint32_t *)round_keys[0];
    s0 ^= rk0[0];
    s1 ^= rk0[1];
    s2 ^= rk0[2];
    s3 ^= rk0[3];

    uint64_t plain_lo = ((uint64_t)s1 << 32) | s0;
    uint64_t plain_hi = ((uint64_t)s3 << 32) | s2;
#else
    // No RISC-V crypto extensions available - fall back to software
    return aes_decrypt_sw(ctx, ciphertext, len, plaintext, iv);
#endif

    // XOR with IV (CBC mode)
    plain_lo ^= iv_lo;
    plain_hi ^= iv_hi;

    memcpy(decrypted_block, &plain_lo, 8);
    memcpy(decrypted_block + 8, &plain_hi, 8);

    size_t copy_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;
    memcpy(plaintext + block_start, decrypted_block, copy_len);

    // Update IV for next block
    iv_lo = cipher_lo;
    iv_hi = cipher_hi;
  }

  return 0;
}
#endif

// Software AES-128 round operations
static void aes_sub_bytes(uint8_t state[16]) {
  for (int i = 0; i < 16; i++) {
    state[i] = aes_sbox[state[i]];
  }
}

static void aes_inv_sub_bytes(uint8_t state[16]) {
  for (int i = 0; i < 16; i++) {
    state[i] = aes_inv_sbox[state[i]];
  }
}

static void aes_shift_rows(uint8_t state[16]) {
  uint8_t temp;
  // Row 1: shift left by 1
  temp = state[1];
  state[1] = state[5];
  state[5] = state[9];
  state[9] = state[13];
  state[13] = temp;
  // Row 2: shift left by 2
  temp = state[2];
  state[2] = state[10];
  state[10] = temp;
  temp = state[6];
  state[6] = state[14];
  state[14] = temp;
  // Row 3: shift left by 3 (or right by 1)
  temp = state[3];
  state[3] = state[15];
  state[15] = state[11];
  state[11] = state[7];
  state[7] = temp;
}

static void aes_inv_shift_rows(uint8_t state[16]) {
  uint8_t temp;
  // Row 1: shift right by 1
  temp = state[13];
  state[13] = state[9];
  state[9] = state[5];
  state[5] = state[1];
  state[1] = temp;
  // Row 2: shift right by 2
  temp = state[2];
  state[2] = state[10];
  state[10] = temp;
  temp = state[6];
  state[6] = state[14];
  state[14] = temp;
  // Row 3: shift right by 3 (or left by 1)
  temp = state[3];
  state[3] = state[7];
  state[7] = state[11];
  state[11] = state[15];
  state[15] = temp;
}

// Galois field multiplication for MixColumns
static uint8_t gf_mul2(uint8_t a) {
  return (a & 0x80) ? ((a << 1) ^ 0x1B) : (a << 1);
}

static uint8_t gf_mul3(uint8_t a) {
  return gf_mul2(a) ^ a;
}

static void aes_mix_columns(uint8_t state[16]) {
  for (int col = 0; col < 4; col++) {
    uint8_t s0 = state[col * 4 + 0];
    uint8_t s1 = state[col * 4 + 1];
    uint8_t s2 = state[col * 4 + 2];
    uint8_t s3 = state[col * 4 + 3];

    state[col * 4 + 0] = gf_mul2(s0) ^ gf_mul3(s1) ^ s2 ^ s3;
    state[col * 4 + 1] = s0 ^ gf_mul2(s1) ^ gf_mul3(s2) ^ s3;
    state[col * 4 + 2] = s0 ^ s1 ^ gf_mul2(s2) ^ gf_mul3(s3);
    state[col * 4 + 3] = gf_mul3(s0) ^ s1 ^ s2 ^ gf_mul2(s3);
  }
}

static uint8_t gf_mul9(uint8_t a) {
  return gf_mul2(gf_mul2(gf_mul2(a))) ^ a;
}

static uint8_t gf_mul11(uint8_t a) {
  return gf_mul2(gf_mul2(gf_mul2(a))) ^ gf_mul2(a) ^ a;
}

static uint8_t gf_mul13(uint8_t a) {
  return gf_mul2(gf_mul2(gf_mul2(a))) ^ gf_mul2(gf_mul2(a)) ^ a;
}

static uint8_t gf_mul14(uint8_t a) {
  return gf_mul2(gf_mul2(gf_mul2(a))) ^ gf_mul2(gf_mul2(a)) ^ gf_mul2(a);
}

static void aes_inv_mix_columns(uint8_t state[16]) {
  for (int col = 0; col < 4; col++) {
    uint8_t s0 = state[col * 4 + 0];
    uint8_t s1 = state[col * 4 + 1];
    uint8_t s2 = state[col * 4 + 2];
    uint8_t s3 = state[col * 4 + 3];

    state[col * 4 + 0] = gf_mul14(s0) ^ gf_mul11(s1) ^ gf_mul13(s2) ^ gf_mul9(s3);
    state[col * 4 + 1] = gf_mul9(s0) ^ gf_mul14(s1) ^ gf_mul11(s2) ^ gf_mul13(s3);
    state[col * 4 + 2] = gf_mul13(s0) ^ gf_mul9(s1) ^ gf_mul14(s2) ^ gf_mul11(s3);
    state[col * 4 + 3] = gf_mul11(s0) ^ gf_mul13(s1) ^ gf_mul9(s2) ^ gf_mul14(s3);
  }
}

static void aes_add_round_key(uint8_t state[16], const uint8_t round_key[16]) {
  for (int i = 0; i < 16; i++) {
    state[i] ^= round_key[i];
  }
}

// Software AES-128 block encryption
static void aes_encrypt_block_sw(const uint8_t plaintext[16], uint8_t ciphertext[16],
                                 const uint8_t round_keys[11][16]) {
  // Copy input to state
  memcpy(ciphertext, plaintext, 16);

  // Initial AddRoundKey (round 0)
  aes_add_round_key(ciphertext, round_keys[0]);

  // Rounds 1-9: SubBytes, ShiftRows, MixColumns, AddRoundKey
  for (int round = 1; round <= 9; round++) {
    aes_sub_bytes(ciphertext);
    aes_shift_rows(ciphertext);
    aes_mix_columns(ciphertext);
    aes_add_round_key(ciphertext, round_keys[round]);
  }

  // Final round 10: SubBytes, ShiftRows, AddRoundKey (no MixColumns)
  aes_sub_bytes(ciphertext);
  aes_shift_rows(ciphertext);
  aes_add_round_key(ciphertext, round_keys[10]);
}

// Software AES-128 block decryption
static void aes_decrypt_block_sw(const uint8_t ciphertext[16], uint8_t plaintext[16],
                                 const uint8_t round_keys[11][16]) {
  // Copy input to state
  memcpy(plaintext, ciphertext, 16);

  // Initial AddRoundKey (round 10)
  aes_add_round_key(plaintext, round_keys[10]);

  // Inverse final round: InvShiftRows, InvSubBytes, AddRoundKey (no InvMixColumns)
  aes_inv_shift_rows(plaintext);
  aes_inv_sub_bytes(plaintext);
  aes_add_round_key(plaintext, round_keys[9]);

  // Inverse rounds 8-1: InvMixColumns, InvShiftRows, InvSubBytes, AddRoundKey
  for (int round = 8; round >= 1; round--) {
    aes_inv_mix_columns(plaintext);
    aes_inv_shift_rows(plaintext);
    aes_inv_sub_bytes(plaintext);
    aes_add_round_key(plaintext, round_keys[round]);
  }

  // Final inverse round 0: InvMixColumns, AddRoundKey (no InvShiftRows, InvSubBytes)
  aes_inv_mix_columns(plaintext);
  aes_add_round_key(plaintext, round_keys[0]);
}

// Multi-architecture hardware-accelerated AES encryption
int aes_encrypt_hw(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext, uint8_t *iv) {
  check_aes_hw_support();

  if (!ctx || !ctx->initialized || !plaintext || !ciphertext || !iv) {
    return -1;
  }

  if (!aes_hw_available) {
    return aes_encrypt_sw(ctx, plaintext, len, ciphertext, iv);
  }

#ifdef ARCH_ARM64
  return aes_arm_encrypt(ctx, plaintext, len, ciphertext, iv);
#elif defined(ARCH_X86_64) || defined(ARCH_X86_32)
  return aes_x86_encrypt(ctx, plaintext, len, ciphertext, iv);
#elif defined(ARCH_PPC64)
  return aes_ppc64_encrypt(ctx, plaintext, len, ciphertext, iv);
#elif defined(ARCH_RISCV64)
  return aes_riscv64_encrypt(ctx, plaintext, len, ciphertext, iv);
#else
  return aes_encrypt_sw(ctx, plaintext, len, ciphertext, iv);
#endif
}

#ifdef ARCH_ARM64
// ARM AES hardware decryption implementation
static int aes_arm_decrypt(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                           const uint8_t *iv) {
  uint8x16_t iv_vec = vld1q_u8(iv);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t decrypted_block[AES_BLOCK_SIZE];

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;

    uint8x16_t cipher_vec = vld1q_u8(ciphertext + block_start);

    // ARM AES decryption using crypto extensions (full AES-128)
    uint8_t round_keys[11][16];
    aes_key_expansion(ctx->key, round_keys);

    uint8x16_t plain_vec = cipher_vec;

    // Initial AddRoundKey (round 10 key)
    plain_vec = veorq_u8(plain_vec, vld1q_u8(round_keys[10]));

    // Inverse final round: InvSubBytes, InvShiftRows, AddRoundKey (no InvMixColumns)
    plain_vec = veorq_u8(vaesdq_u8(plain_vec, vdupq_n_u8(0)), vld1q_u8(round_keys[9]));

    // Inverse rounds 9-1: InvSubBytes, InvShiftRows, InvMixColumns, AddRoundKey
    for (int round = 8; round >= 0; round--) {
      plain_vec = vaesimcq_u8(vaesdq_u8(plain_vec, vld1q_u8(round_keys[round])));
    }

    // XOR with IV (CBC mode)
    plain_vec = veorq_u8(plain_vec, iv_vec);

    vst1q_u8(decrypted_block, plain_vec);

    size_t copy_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;
    memcpy(plaintext + block_start, decrypted_block, copy_len);

    iv_vec = cipher_vec; // Update IV for next block
  }

  return 0;
}
#endif

#if defined(ARCH_X86_64) || defined(ARCH_X86_32)
// Intel/AMD AES hardware decryption implementation (works on both x86_64 and x86_32)
static int aes_x86_decrypt(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                           const uint8_t *iv) {
  __m128i key_vec = _mm_loadu_si128((__m128i *)ctx->key);
  __m128i iv_vec = _mm_loadu_si128((__m128i *)iv);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t decrypted_block[AES_BLOCK_SIZE];

  for (size_t i = 0; i < blocks; i++) {
    size_t block_start = i * AES_BLOCK_SIZE;

    __m128i cipher_vec = _mm_loadu_si128((__m128i *)(ciphertext + block_start));

    // Full AES-128 decryption using AES-NI
    uint8_t round_keys[11][16];
    aes_key_expansion(ctx->key, round_keys);

    __m128i plain_vec = cipher_vec;

    // Initial AddRoundKey (round 10 key)
    plain_vec = _mm_xor_si128(plain_vec, _mm_loadu_si128((__m128i *)round_keys[10]));

    // Inverse rounds 9-1: InvSubBytes, InvShiftRows, InvMixColumns, AddRoundKey
    for (int round = 9; round >= 1; round--) {
      plain_vec = _mm_aesdec_si128(plain_vec, _mm_loadu_si128((__m128i *)round_keys[round]));
    }

    // Final round 0: InvSubBytes, InvShiftRows, AddRoundKey (no InvMixColumns)
    plain_vec = _mm_aesdeclast_si128(plain_vec, _mm_loadu_si128((__m128i *)round_keys[0]));

    // XOR with IV (CBC mode)
    plain_vec = _mm_xor_si128(plain_vec, iv_vec);

    _mm_storeu_si128((__m128i *)decrypted_block, plain_vec);

    size_t copy_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;
    memcpy(plaintext + block_start, decrypted_block, copy_len);

    iv_vec = cipher_vec; // Update IV for next block
  }

  return 0;
}
#endif

// Multi-architecture hardware-accelerated AES decryption
int aes_decrypt_hw(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                   const uint8_t *iv) {
  check_aes_hw_support();

  if (!ctx || !ctx->initialized || !ciphertext || !plaintext || !iv) {
    return -1;
  }

  if (!aes_hw_available) {
    return aes_decrypt_sw(ctx, ciphertext, len, plaintext, iv);
  }

#ifdef ARCH_ARM64
  return aes_arm_decrypt(ctx, ciphertext, len, plaintext, iv);
#elif defined(ARCH_X86_64) || defined(ARCH_X86_32)
  return aes_x86_decrypt(ctx, ciphertext, len, plaintext, iv);
#elif defined(ARCH_PPC64)
  return aes_ppc64_decrypt(ctx, ciphertext, len, plaintext, iv);
#elif defined(ARCH_RISCV64)
  return aes_riscv64_decrypt(ctx, ciphertext, len, plaintext, iv);
#else
  return aes_decrypt_sw(ctx, ciphertext, len, plaintext, iv);
#endif
}

// Software fallback using full AES-128 implementation
int aes_encrypt_sw(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext, uint8_t *iv) {
  if (!ctx || !ctx->initialized || !plaintext || !ciphertext || !iv) {
    return -1;
  }

  // Generate random IV
  for (int i = 0; i < AES_IV_SIZE; i++) {
    iv[i] = (uint8_t)(rand() & 0xFF);
  }

  // Generate round keys
  uint8_t round_keys[11][16];
  aes_key_expansion(ctx->key, round_keys);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t current_iv[AES_BLOCK_SIZE];
  memcpy(current_iv, iv, AES_BLOCK_SIZE);

  for (size_t block = 0; block < blocks; block++) {
    size_t block_start = block * AES_BLOCK_SIZE;
    size_t block_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;

    uint8_t padded_block[AES_BLOCK_SIZE];
    memset(padded_block, 0, AES_BLOCK_SIZE);
    memcpy(padded_block, plaintext + block_start, block_len);

    // XOR with IV (CBC mode)
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
      padded_block[i] ^= current_iv[i];
    }

    // Software AES-128 encryption
    aes_encrypt_block_sw(padded_block, ciphertext + block_start, round_keys);

    // Update IV for next block
    memcpy(current_iv, ciphertext + block_start, AES_BLOCK_SIZE);
  }

  return 0;
}

int aes_decrypt_sw(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                   const uint8_t *iv) {
  if (!ctx || !ctx->initialized || !ciphertext || !plaintext || !iv) {
    return -1;
  }

  // Generate round keys
  uint8_t round_keys[11][16];
  aes_key_expansion(ctx->key, round_keys);

  size_t blocks = (len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE;
  uint8_t current_iv[AES_BLOCK_SIZE];
  memcpy(current_iv, iv, AES_BLOCK_SIZE);

  for (size_t block = 0; block < blocks; block++) {
    size_t block_start = block * AES_BLOCK_SIZE;
    size_t block_len = (block_start + AES_BLOCK_SIZE <= len) ? AES_BLOCK_SIZE : len - block_start;

    uint8_t decrypted_block[AES_BLOCK_SIZE];

    // Software AES-128 decryption
    aes_decrypt_block_sw(ciphertext + block_start, decrypted_block, round_keys);

    // XOR with IV (CBC mode)
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
      decrypted_block[i] ^= current_iv[i];
    }

    // Copy to output (only the valid length)
    memcpy(plaintext + block_start, decrypted_block, block_len);

    // Update IV for next block (previous ciphertext)
    memcpy(current_iv, ciphertext + block_start, AES_BLOCK_SIZE);
  }

  return 0;
}

// Derive AES key from passphrase using simple SHA-256-like hash
void aes_derive_key(const char *passphrase, uint8_t key[AES_KEY_SIZE]) {
  if (!passphrase || !key)
    return;

  // Simple key derivation - hash passphrase multiple times
  // Production would use PBKDF2 or Argon2
  size_t passlen = strlen(passphrase);
  uint32_t hash = 0x811c9dc5; // FNV-1a initial value

  // Iterate multiple times for key stretching
  for (int round = 0; round < 1000; round++) {
    for (size_t i = 0; i < passlen; i++) {
      hash ^= (uint8_t)passphrase[i];
      hash *= 0x01000193; // FNV-1a prime
    }
    // Add round number to prevent identical rounds
    hash ^= round;
    hash *= 0x01000193;
  }

  // Expand hash to fill key
  for (int i = 0; i < AES_KEY_SIZE; i++) {
    key[i] = (uint8_t)((hash >> ((i % 4) * 8)) & 0xFF);
    if (i % 4 == 3) {
      // Mix hash for next 4 bytes
      hash ^= hash << 13;
      hash ^= hash >> 17;
      hash ^= hash << 5;
    }
  }
}

// Initialize AES context with passphrase
int aes_init_context(aes_context_t *ctx, const char *passphrase) {
  if (!ctx || !passphrase) {
    return -1;
  }

  memset(ctx, 0, sizeof(aes_context_t));
  aes_derive_key(passphrase, ctx->key);
  ctx->initialized = true;
  ctx->hw_available = aes_hw_is_available();

  log_info("AES encryption initialized (hardware: %s)", ctx->hw_available ? "enabled" : "disabled");

  return 0;
}

// Generate key verification hash for handshake
uint32_t aes_key_verification_hash(const uint8_t key[AES_KEY_SIZE]) {
  if (!key)
    return 0;

  uint32_t hash = 0x811c9dc5;
  for (int i = 0; i < AES_KEY_SIZE; i++) {
    hash ^= key[i];
    hash *= 0x01000193;
  }
  return hash;
}
