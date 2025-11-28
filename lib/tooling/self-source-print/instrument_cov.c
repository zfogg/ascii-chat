// SPDX-License-Identifier: MIT
// SanitizerCoverage hooks for ascii-chat instrumentation runtime

#include "tooling/source_print/instrument_log.h"
#include "common.h"

#include <stdint.h>

void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
  if (guard == NULL || *guard == 0U) {
    return;
  }

  ascii_instr_log_pc((uintptr_t)__builtin_return_address(0));
}

void __sanitizer_cov_trace_pc_guard_init(uint32_t *start, uint32_t *stop) {
  if (start == NULL || stop == NULL || start == stop) {
    return;
  }

  if (*start != 0U) {
    return;
  }

  uint32_t counter = 1U;
  for (uint32_t *it = start; it < stop; ++it) {
    if (*it == 0U) {
      *it = counter++;
    }
  }
}
