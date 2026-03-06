/**
 * @file instrument_cov.c
 * @ingroup tooling
 * @brief SanitizerCoverage runtime hooks for code coverage tracking
 *
 * Implements the runtime interface for Clang's SanitizerCoverage instrumentation.
 * Provides two hooks that are automatically called by the compiler:
 * - `__sanitizer_cov_trace_pc_guard()` - Logs execution of each covered instruction
 * - `__sanitizer_cov_trace_pc_guard_init()` - Initializes coverage tracking state
 *
 * Coverage data is logged via the instrumentation logging system for analysis
 * in post-processing and coverage report generation.
 *
 * @see include/ascii-chat/tooling/panic/instrument_log.h
 */

// SPDX-License-Identifier: MIT
// SanitizerCoverage hooks for ascii-chat instrumentation runtime

#include <ascii-chat/tooling/panic/instrument_log.h>
#include <ascii-chat/common.h>

#include <stdint.h>

void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
  if (guard == NULL || *guard == 0U) {
    return;
  }

  asciichat_instr_log_pc((uintptr_t)__builtin_return_address(0));
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
