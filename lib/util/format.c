/**
 * @file util/format.c
 * @ingroup util
 * @brief 📊 Byte size formatting utilities for human-readable output (B, KB, MB, GB, TB)
 */

#include "format.h"
#include "platform/system.h"

void format_bytes_pretty(size_t bytes, char *out, size_t out_capacity) {
  const double KB = 1024.0;
  const double MB = KB * 1024.0;
  const double GB = MB * 1024.0;
  const double TB = GB * 1024.0;

  if ((double)bytes < KB) {
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%zu B", bytes));
  } else if ((double)bytes < MB) {
    double value = (double)bytes / KB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f KB", value));
  } else if ((double)bytes < GB) {
    double value = (double)bytes / MB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f MB", value));
  } else if ((double)bytes < TB) {
    double value = (double)bytes / GB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f GB", value));
  } else {
    double value = (double)bytes / TB;
    SAFE_IGNORE_PRINTF_RESULT(safe_snprintf(out, out_capacity, "%.2f TB", value));
  }
}
