#pragma once
/**
 * @file asciichat_errno.h
 * @brief Synchronized per-thread pending error stacks and value snapshots.
 * SET_ERRNO pushes, CLEAR_ERRNO pops. Recovery scopes resolve one attempt's
 * errors. See docs/topics/errno.dox for ownership and capacity contracts.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "log/log.h"

typedef struct {
  void *ptrs[32];
  char **symbols;
  int count;
  bool tried_symbolize;
} backtrace_t;

#define ASCIICHAT_ERRNO_MESSAGE_MAX 1024
#define ASCIICHAT_ERRNO_MAX_DEPTH 64
#define ASCIICHAT_ERRNO_MAX_THREADS 256
#define ASCIICHAT_ERRNO_MAX_FRAMES 1024
#define ASCIICHAT_ERRNO_HISTORY_SIZE 128

/** A self-contained value snapshot. Source locations refer to static compiler
 * strings; messages and raw addresses are copied. Printing symbolizes and frees
 * a private backtrace. Struct assignment is safe; no destroy call is needed.
 */
typedef struct {
  asciichat_error_t code;
  const char *file;
  int line;
  const char *function;
  char context_message[ASCIICHAT_ERRNO_MESSAGE_MAX];
  uint64_t timestamp;  ///< Microseconds since epoch, for logs.
  uint64_t created_ns; ///< Monotonic time, for age.
  uint64_t error_id;
  uint64_t cause_id;
  uint64_t thread_id;
  uint64_t generation;
  int system_errno;
  int wsa_error;
  backtrace_t backtrace;
  bool has_system_error;
  bool has_wsa_error;
  bool message_truncated;
} asciichat_error_context_t;

typedef enum {
  ASCIICHAT_ERRNO_HANDLED,
  ASCIICHAT_ERRNO_DISMISSED,
  ASCIICHAT_ERRNO_THREAD_EXIT,
  ASCIICHAT_ERRNO_OVERFLOW,
} asciichat_errno_outcome_t;

typedef struct {
  asciichat_error_context_t context;
  uint64_t resolved_ns;
  asciichat_errno_outcome_t outcome;
} asciichat_errno_history_t;

/** Checkpoints use IDs, not depths, so concurrent removals cannot invalidate them. */
typedef struct {
  uint64_t thread_id;
  uint64_t generation;
  uint64_t after_id;
  uint64_t previous_after_id;
} asciichat_errno_scope_t;

typedef struct {
  uint64_t error_counts[256];
  uint64_t total_errors;
  uint64_t last_error_time;
  asciichat_error_t last_error_code;
  uint64_t dropped_errors;
  uint64_t overwritten_history;
  size_t pending_errors;
  size_t registered_threads;
} asciichat_error_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

asciichat_error_t asciichat_set_errno(asciichat_error_t code, const char *file, int line, const char *function,
                                      const char *message);
asciichat_error_t asciichat_set_errno_with_message(asciichat_error_t code, const char *file, int line,
                                                   const char *function, const char *format, ...);
asciichat_error_t asciichat_set_errno_with_system_error(asciichat_error_t code, const char *file, int line,
                                                        const char *function, int sys_errno);
asciichat_error_t asciichat_set_errno_with_system_error_and_message(asciichat_error_t code, const char *file, int line,
                                                                    const char *function, int sys_errno,
                                                                    const char *format, ...);
asciichat_error_t asciichat_set_errno_with_wsa_error(asciichat_error_t code, const char *file, int line,
                                                     const char *function, int wsa_error);

// Arguments evaluate once. Capture errno before evaluating message arguments.
#define SET_ERRNO(code, ...) asciichat_set_errno_with_message((code), __FILE__, __LINE__, __func__, __VA_ARGS__)
#define SET_ERRNO_SYS(code, ...)                                                                                       \
  ({                                                                                                                   \
    int _asciichat_saved_errno = platform_get_last_error();                                                            \
    asciichat_set_errno_with_system_error_and_message((code), __FILE__, __LINE__, __func__, _asciichat_saved_errno,    \
                                                      __VA_ARGS__);                                                    \
  })

bool asciichat_has_errno(asciichat_error_context_t *context);
bool asciichat_has_thread_errno(uint64_t thread_id, asciichat_error_context_t *context);
bool asciichat_has_errno_code(asciichat_error_t code);
bool asciichat_has_errno_code_tid(uint64_t thread_id, asciichat_error_t code);
bool asciichat_has_errno_code_since(asciichat_errno_scope_t scope, asciichat_error_t code);
bool asciichat_has_wsa_error(void);
bool asciichat_errno_scope_is_clean(asciichat_errno_scope_t scope);
void asciichat_errno_print_chain(uint64_t thread_id, uint64_t error_id);
asciichat_error_t asciichat_get_errno(void);
asciichat_error_t asciichat_get_thread_error(uint64_t thread_id);
void asciichat_clear_errno(void);
void asciichat_clear_errno_all(void);
void asciichat_clear_thread_error(uint64_t thread_id);
void asciichat_clear_thread_errors(uint64_t thread_id);
bool asciichat_clear_errno_if_top(uint64_t thread_id, uint64_t generation, uint64_t error_id);
// Observe a suboperation without severing its causal link to the caller.
asciichat_errno_scope_t asciichat_errno_checkpoint(void);
asciichat_errno_scope_t asciichat_errno_scope_begin(void);
void asciichat_errno_scope_end(asciichat_errno_scope_t scope, asciichat_errno_outcome_t outcome);

/** Copy newest-first up to capacity; return total depth so truncation is visible. */
size_t asciichat_errno_snapshot(uint64_t thread_id, asciichat_error_context_t *out, size_t capacity);
size_t asciichat_errno_history_snapshot(asciichat_errno_history_t *out, size_t capacity);
void asciichat_errno_print_stacks(void);
void asciichat_errno_print_history(void);
void asciichat_errno_print_hash_stats(void);
// Per-thread automatic logging suppression; storage is never suppressed.
void asciichat_errno_suppress(bool suppress);
// Idempotent current-thread cleanup. Does not disable other threads.
void asciichat_errno_destroy(void);
// Process shutdown, after producers and readers have joined.
void asciichat_errno_shutdown(void);
void asciichat_errno_request_exit(asciichat_error_t code);
asciichat_error_t asciichat_errno_exit_code(void);

void asciichat_error_stats_init(void);
void asciichat_error_stats_record(asciichat_error_t code);
void asciichat_error_stats_print(void);
void asciichat_error_stats_reset(void);
asciichat_error_stats_t asciichat_error_stats_get(void);
void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...);
void asciichat_print_error_context(const asciichat_error_context_t *context);

#define HAS_ERRNO(context) asciichat_has_errno(context)
#define HAS_ERRNO_CODE(code) asciichat_has_errno_code(code)
#define HAS_ERRNO_CODE_TID(tid, code) asciichat_has_errno_code_tid((tid), (code))
#define HAS_ERRNO_CODE_SINCE(scope, code) asciichat_has_errno_code_since((scope), (code))
#define GET_ERRNO() asciichat_get_errno()
#define CLEAR_ERRNO() asciichat_clear_errno()
#define CLEAR_ERRNO_ALL() asciichat_clear_errno_all()
#define CLEAR_ERRNO_TID(tid) asciichat_clear_thread_error(tid)
#define CLEAR_ERRNO_ALL_TID(tid) asciichat_clear_thread_errors(tid)
#define PRINT_ERRNO_CONTEXT(context) asciichat_print_error_context(context)
#define LOG_ERRNO_IF_SET(message)                                                                                      \
  do {                                                                                                                 \
    asciichat_error_context_t _ctx;                                                                                    \
    if (HAS_ERRNO(&_ctx)) {                                                                                            \
      log_error("%s", (message));                                                                                      \
      asciichat_errno_print_chain(_ctx.thread_id, _ctx.error_id);                                                      \
    }                                                                                                                  \
  } while (0)
#define LOG_ERRNO_IF_CODE(error_code, message)                                                                         \
  do {                                                                                                                 \
    asciichat_error_context_t _ctx;                                                                                    \
    if (HAS_ERRNO(&_ctx) && _ctx.code == (error_code)) {                                                               \
      log_error("%s", (message));                                                                                      \
      asciichat_errno_print_chain(_ctx.thread_id, _ctx.error_id);                                                      \
    }                                                                                                                  \
  } while (0)
#define PRINT_ERRNO_IF_ERROR() LOG_ERRNO_IF_SET("Pending error")
#ifndef NDEBUG
#define ASSERT_NO_ERRNO_SINCE(scope)                                                                                   \
  do {                                                                                                                 \
    if (!asciichat_errno_scope_is_clean(scope))                                                                        \
      FATAL(ERROR_INVALID_STATE, "Successful operation left unresolved errors");                                       \
  } while (0)
#define ASSERT_NO_ERRNO()                                                                                              \
  do {                                                                                                                 \
    asciichat_error_context_t _ctx;                                                                                    \
    if (HAS_ERRNO(&_ctx)) {                                                                                            \
      asciichat_print_error_context(&_ctx);                                                                            \
      FATAL(_ctx.code, "Unresolved error at operation boundary");                                                      \
    }                                                                                                                  \
  } while (0)
#else
#define ASSERT_NO_ERRNO_SINCE(scope) ((void)(scope))
#define ASSERT_NO_ERRNO() ((void)0)
#endif

#ifdef __cplusplus
}
#endif
