/**
 * @file util/format.c
 * @ingroup util
 * @brief 📊 Byte size formatting utilities for human-readable output (B, KB, MB, GB, TB, PB, EB)
 */

#include <ascii-chat/util/format.h>
#include <ascii-chat/platform/system.h>
#include <inttypes.h>

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

void format_number_compact(uint64_t value, char *out, size_t out_capacity) {
  if (!out || !out_capacity)
    return;
  static const char suffixes[] = "kmbtpe";
  if (value < 1000) {
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%" PRIu64, value));
    return;
  }
  uint64_t scale = 1000;
  size_t unit = 0;
  while (unit + 1 < sizeof(suffixes) - 1 && value / scale >= 1000) {
    scale *= 1000;
    ++unit;
  }
  uint64_t whole = value / scale;
  uint64_t step = scale / 10;
  uint64_t fraction = (value % scale + step / 2) / step;
  if (fraction == 10) {
    ++whole;
    fraction = 0;
  }
  if (whole == 1000 && unit + 1 < sizeof(suffixes) - 1) {
    whole = 1;
    ++unit;
  }
  if (fraction)
    SAFE_IGNORE_PRINTF_RESULT(
        safe_snprintf(out, out_capacity, "%" PRIu64 ".%" PRIu64 "%c", whole, fraction, suffixes[unit]));
  else
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%" PRIu64 "%c", whole, suffixes[unit]));
}
