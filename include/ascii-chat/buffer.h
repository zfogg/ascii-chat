#pragma once
#include <ascii-chat/buffer/budget.h>

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Owned, aligned payloads for synchronous and asynchronous media pipelines.
 *
 * A returned payload pointer is an ownership token. Only pointers returned by
 * this API may be released or queried; interior pointers are borrowed views.
 * Capacity, allocation origin and domain lifetime are private implementation
 * details. Ordinary objects continue to use SAFE_MALLOC/SAFE_FREE.
 *
 * Buffers are exclusive until buffer_share() is called. Shared buffers must be
 * treated as immutable. Each owner releases exactly once. Transfer a pointer
 * by clearing the source after a successful handoff; failure preserves it.
 *
 * Domains serialize allocation, return and trim. No allocator operation is
 * suitable for a real-time audio callback: reserve and touch storage first.
 */
typedef struct buffer_domain buffer_domain_t;

#define BUFFER_DEFAULT_ALIGNMENT 64u
#define BUFFER_PROCESS_LIMIT ((size_t)512 * 1024 * 1024)
#define BUFFER_CONTROL_RESERVE ((size_t)8 * 1024 * 1024)
#define BUFFER_DOMAIN_LIMIT ((size_t)337 * 1024 * 1024)

typedef struct {
  size_t limit_bytes; /**< Includes live and cached storage and headers. Zero uses the default. */
  size_t cache_bytes; /**< Maximum idle storage; zero disables caching. */
  uint64_t idle_ns;   /**< Minimum idle age for maintenance trimming. */
  bool control;       /**< May consume the process reserve for control traffic. */
} buffer_domain_config_t;

typedef struct {
  size_t reserved_bytes;
  size_t live_bytes;
  size_t requested_bytes;
  size_t cached_bytes;
  size_t peak_bytes;
  size_t outstanding;
  uint64_t hits;
  uint64_t allocations;
  uint64_t failures;
} buffer_stats_t;

/** Create a domain under the shared process budget. */
asciichat_error_t buffer_domain_create(const buffer_domain_config_t *config, buffer_domain_t **out);
/** Stop admissions and trim cached storage. Outstanding buffers keep the domain alive.
 * Join users of the domain pointer before closing; outstanding payloads may be
 * released afterwards, from any thread. Clears the caller's domain pointer. */
void buffer_domain_close(buffer_domain_t **domain);
/** Maintenance operation; never called implicitly from the steady-state release path. */
void buffer_domain_trim(buffer_domain_t *domain);
void buffer_domain_stats(buffer_domain_t *domain, buffer_stats_t *out);
void buffer_domain_log_stats(buffer_domain_t *domain, const char *name);
size_t buffer_process_reserved(void);

/** Budget lease for platform storage that cannot be recycled (e.g. pinned mappings).
 * Zero-initialize, reserve before allocation, release only after disposing storage.
 * A failed reservation preserves the lease. Never copy a live lease. */
asciichat_error_t buffer_budget_reserve(buffer_budget_t *budget, size_t bytes);
void buffer_budget_release(buffer_budget_t *budget);

/** NULL domain selects the default process domain. Failure leaves *out NULL. */
asciichat_error_t buffer_acquire(buffer_domain_t *domain, size_t size, size_t alignment, void **out);
/** Convenience for typed C callers; failures set asciichat_errno and return NULL. */
void *buffer_alloc(buffer_domain_t *domain, size_t size);
/** Query only a live owning payload pointer (NULL returns zero). */
size_t buffer_capacity(const void *data);
size_t buffer_size(const void *data);
/** Change logical length within capacity. Requires exclusive ownership. */
asciichat_error_t buffer_set_size(void *data, size_t size);
/** Acquire another immutable reference while an existing reference is held. */
const void *buffer_share(const void *data);
/** Low-level release for ownership passed by value. Prefer buffer_release(). */
void buffer_unref(const void *data);
/** Release and clear a typed pointer lvalue; evaluates the lvalue once. */
#define buffer_release(data)                                                                                           \
  do {                                                                                                                 \
    __typeof__(&(data)) buffer_slot__ = &(data);                                                                       \
    buffer_unref(*buffer_slot__);                                                                                      \
    *buffer_slot__ = NULL;                                                                                             \
  } while (0)

/** Worker-owned reusable storage. Zero-initialize; never share a workspace. */
typedef struct {
  void *data;
  size_t length;
} buffer_workspace_t;
/** Preserves the previous workspace on failure; retains capacity between frames. */
asciichat_error_t buffer_workspace_reserve(buffer_workspace_t *workspace, buffer_domain_t *domain, size_t capacity);
void buffer_workspace_destroy(buffer_workspace_t *workspace);

/** Application startup/shutdown, serialized against starting/stopping workers. */
void buffer_system_init(void);
void buffer_system_cleanup(void);
buffer_domain_t *buffer_default_domain(void);
/** Small, bounded control messages may use the reserved process allowance. */
buffer_domain_t *buffer_control_domain(void);
