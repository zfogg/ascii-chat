/**
 * @file util/format.c
 * @ingroup util
 * @brief 📊 Byte size formatting utilities for human-readable output (B, KB, MB, GB, TB, PB, EB)
 */

#include <ascii-chat/util/format.h>
#include <ascii-chat/platform/system.h>

void format_bytes_pretty(size_t bytes, char *out, size_t out_capacity) {
  const double KB = 1024.0;
  const double MB = KB * 1024.0;
  const double GB = MB * 1024.0;
  const double TB = GB * 1024.0;
  const double PB = TB * 1024.0;
  const double EB = PB * 1024.0;

  const double THRESHOLD = 0.8;

  double byte_val = (double)bytes;

  if (byte_val < THRESHOLD * KB) {
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%zu B", bytes));
  } else if (byte_val < THRESHOLD * MB) {
    double value = byte_val / KB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f KB", value));
  } else if (byte_val < THRESHOLD * GB) {
    double value = byte_val / MB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f MB", value));
  } else if (byte_val < THRESHOLD * TB) {
    double value = byte_val / GB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f GB", value));
  } else if (byte_val < THRESHOLD * PB) {
    double value = byte_val / TB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f TB", value));
  } else if (byte_val < THRESHOLD * EB) {
    double value = byte_val / PB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f PB", value));
  } else {
    double value = byte_val / EB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f EB", value));
  }
}
