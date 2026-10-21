#pragma once

// Informational actions and intentionally blocked test fixtures bypass atexit.
// Preserve their real execution counters before the caller terminates them.
#ifdef COVERAGE_BUILD
#ifdef __cplusplus
extern "C" {
#endif
void __gcov_dump(void);
#ifdef __cplusplus
}
#endif
#endif

static inline void asciichat_coverage_flush(void) {
#ifdef COVERAGE_BUILD
  __gcov_dump();
#endif
}
