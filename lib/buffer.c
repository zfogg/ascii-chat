#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <stdint.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define BUFFER_POISON(p, n) __asan_poison_memory_region((p), (n))
#define BUFFER_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#endif
#endif
#ifndef BUFFER_POISON
#define BUFFER_POISON(p, n) ((void)0)
#define BUFFER_UNPOISON(p, n) ((void)0)
#endif

/* Power-of-two bins through 4 MiB; larger payloads are budgeted but not cached. */
#define BUFFER_BINS 17
#define BUFFER_MIN_CAPACITY 64u
#define BUFFER_MAX_CACHED ((size_t)4 * 1024 * 1024)

typedef struct buffer_header {
  struct buffer_header *next;
  buffer_domain_t *domain;
  void *allocation;
  void *data;
  size_t capacity;
  size_t length;
  size_t charge;
  size_t alignment;
  _Atomic(size_t) references;
  uint64_t returned_at;
  unsigned bin;
} buffer_header_t;

struct buffer_domain {
  mutex_t mutex;
  buffer_domain_config_t config;
  buffer_stats_t stats;
  buffer_header_t *bins[BUFFER_BINS];
  bool closing;
};

static atomic_t process_bytes;
static buffer_domain_t *default_domain;
static buffer_domain_t *control_domain;
static lifecycle_t default_lifecycle = LIFECYCLE_INIT;

static bool process_reserve(size_t bytes, bool control) {
  uint64_t current = atomic_load_u64(&process_bytes);
  size_t limit = control ? BUFFER_PROCESS_LIMIT : BUFFER_PROCESS_LIMIT - BUFFER_CONTROL_RESERVE;
  while (current <= limit && bytes <= limit - current) {
    if (atomic_cas_u64(&process_bytes, &current, current + bytes))
      return true;
  }
  return false;
}

size_t buffer_process_reserved(void) {
  return (size_t)atomic_load_u64(&process_bytes);
}

asciichat_error_t buffer_budget_reserve(buffer_budget_t *budget, size_t bytes) {
  if (!budget || budget->bytes || !bytes)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Expected an empty buffer budget lease");
  if (!process_reserve(bytes, false))
    return SET_ERRNO(ERROR_MEMORY, "Process buffer budget exhausted reserving platform storage");
  budget->bytes = bytes;
  return ASCIICHAT_OK;
}

void buffer_budget_release(buffer_budget_t *budget) {
  if (budget && budget->bytes) {
    atomic_fetch_sub_u64(&process_bytes, budget->bytes);
    budget->bytes = 0;
  }
}

/* The back pointer lives immediately before the aligned payload. It is never
 * inspected for foreign allocations; those use their original deallocator. */
static buffer_header_t *header(const void *data) {
  return data ? ((buffer_header_t *const *)data)[-1] : NULL;
}

static void dispose(buffer_domain_t *domain, buffer_header_t *node) {
  domain->stats.reserved_bytes -= node->charge;
  atomic_fetch_sub_u64(&process_bytes, node->charge);
  void *allocation = node->allocation;
  SAFE_FREE(allocation);
}

static void trim_locked(buffer_domain_t *domain, bool all) {
  uint64_t now = time_get_ns();
  for (unsigned i = 0; i < BUFFER_BINS; i++) {
    buffer_header_t **link = &domain->bins[i];
    while (*link) {
      buffer_header_t *node = *link;
      if (all || now - node->returned_at >= domain->config.idle_ns) {
        *link = node->next;
        domain->stats.cached_bytes -= node->charge;
        dispose(domain, node);
      } else {
        link = &node->next;
      }
    }
  }
}

asciichat_error_t buffer_domain_create(const buffer_domain_config_t *config, buffer_domain_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer domain output is NULL");
  *out = NULL;
  buffer_domain_config_t defaults = {BUFFER_DOMAIN_LIMIT, 32 * 1024 * 1024, 5000000000ULL, false};
  if (!process_reserve(sizeof(buffer_domain_t), config && config->control))
    return SET_ERRNO(ERROR_MEMORY, "Process buffer budget exhausted creating domain");
  buffer_domain_t *domain = ALLOC_CALLOC(1, sizeof(*domain));
  if (!domain) {
    atomic_fetch_sub_u64(&process_bytes, sizeof(*domain));
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate buffer domain");
  }
  domain->config = config ? *config : defaults;
  if (!domain->config.limit_bytes)
    domain->config.limit_bytes = BUFFER_DOMAIN_LIMIT;
  if (mutex_init(&domain->mutex,
                 domain->config.control ? "reserved_control_buffer_pool" : "shared_media_buffer_pool") != 0) {
    atomic_fetch_sub_u64(&process_bytes, sizeof(*domain));
    SAFE_FREE(domain);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize buffer domain mutex");
  }
  *out = domain;
  return ASCIICHAT_OK;
}

static void domain_dispose(buffer_domain_t *domain) {
  mutex_destroy(&domain->mutex);
  atomic_fetch_sub_u64(&process_bytes, sizeof(*domain));
  SAFE_FREE(domain);
}

void buffer_domain_close(buffer_domain_t **slot) {
  if (!slot || !*slot)
    return;
  buffer_domain_t *domain = *slot;
  *slot = NULL;
  mutex_lock(&domain->mutex);
  domain->closing = true;
  trim_locked(domain, true);
  bool done = domain->stats.outstanding == 0;
  mutex_unlock(&domain->mutex);
  if (done)
    domain_dispose(domain);
}

void buffer_domain_trim(buffer_domain_t *domain) {
  if (!domain)
    return;
  mutex_lock(&domain->mutex);
  trim_locked(domain, false);
  mutex_unlock(&domain->mutex);
}

asciichat_error_t buffer_acquire(buffer_domain_t *domain, size_t size, size_t alignment, void **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer output is NULL");
  *out = NULL;
  if (!alignment)
    alignment = BUFFER_DEFAULT_ALIGNMENT;
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)) || alignment > 4096)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer alignment must be a power of two between pointer size and 4096");
  if (!domain)
    domain = buffer_default_domain();
  if (!domain)
    return SET_ERRNO(ERROR_INVALID_STATE, "Buffer system is unavailable");

  size_t capacity = BUFFER_MIN_CAPACITY;
  unsigned bin = 0;
  while (capacity < size && bin + 1 < BUFFER_BINS) {
    capacity *= 2;
    bin++;
  }
  if (capacity < size)
    capacity = size;
  size_t overhead = sizeof(buffer_header_t) + sizeof(buffer_header_t *) + alignment - 1;
  if (capacity > SIZE_MAX - overhead)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer capacity overflow");
  size_t charge = capacity + overhead;

  mutex_lock(&domain->mutex);
  if (domain->closing) {
    mutex_unlock(&domain->mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Buffer domain is closing");
  }
  buffer_header_t **link = &domain->bins[bin];
  buffer_header_t *node = NULL;
  if (capacity <= BUFFER_MAX_CACHED) {
    while (*link) {
      if ((*link)->alignment >= alignment) {
        node = *link;
        *link = node->next;
        domain->stats.cached_bytes -= node->charge;
        domain->stats.hits++;
        break;
      }
      link = &(*link)->next;
    }
  }
  if (!node) {
    if (charge > domain->config.limit_bytes || domain->stats.reserved_bytes > domain->config.limit_bytes - charge) {
      /* A miss may reclaim idle capacity, but a release never scans a pool. */
      trim_locked(domain, true);
    }
    bool fits =
        charge <= domain->config.limit_bytes && domain->stats.reserved_bytes <= domain->config.limit_bytes - charge;
    bool reserved = fits && process_reserve(charge, domain->config.control);
    if (fits && !reserved && domain->stats.cached_bytes) {
      trim_locked(domain, true);
      reserved = process_reserve(charge, domain->config.control);
    }
    if (!reserved) {
      domain->stats.failures++;
      mutex_unlock(&domain->mutex);
      return SET_ERRNO(ERROR_MEMORY, "Buffer budget exhausted requesting %zu bytes", size);
    }
    node = ALLOC_MALLOC(charge);
    if (!node) {
      atomic_fetch_sub_u64(&process_bytes, charge);
      domain->stats.failures++;
      mutex_unlock(&domain->mutex);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate %zu buffer bytes", charge);
    }
    memset(node, 0, sizeof(*node));
    node->allocation = node;
    uintptr_t start = (uintptr_t)(node + 1) + sizeof(buffer_header_t *);
    node->data = (void *)((start + alignment - 1) & ~(uintptr_t)(alignment - 1));
    ((buffer_header_t **)node->data)[-1] = node;
    node->domain = domain;
    node->capacity = capacity;
    node->charge = charge;
    node->alignment = alignment;
    node->bin = bin;
    domain->stats.reserved_bytes += charge;
    domain->stats.allocations++;
  }
  node->next = NULL;
  BUFFER_UNPOISON(node->data, node->capacity);
  node->references = 1;
  node->length = size;
  domain->stats.outstanding++;
  domain->stats.live_bytes += node->charge;
  domain->stats.requested_bytes += size;
  if (domain->stats.reserved_bytes > domain->stats.peak_bytes)
    domain->stats.peak_bytes = domain->stats.reserved_bytes;
  *out = node->data;
  mutex_unlock(&domain->mutex);
  return ASCIICHAT_OK;
}

void *buffer_alloc(buffer_domain_t *domain, size_t size) {
  void *data = NULL;
  (void)buffer_acquire(domain, size, BUFFER_DEFAULT_ALIGNMENT, &data);
  return data;
}

size_t buffer_capacity(const void *data) {
  return data ? header(data)->capacity : 0;
}
size_t buffer_size(const void *data) {
  return data ? header(data)->length : 0;
}

asciichat_error_t buffer_set_size(void *data, size_t size) {
  if (!data || size > buffer_capacity(data))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer length exceeds capacity");
  buffer_header_t *node = header(data);
  buffer_domain_t *domain = node->domain;
  mutex_lock(&domain->mutex);
  if (node->references != 1) {
    mutex_unlock(&domain->mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot resize shared buffer");
  }
  domain->stats.requested_bytes -= node->length;
  node->length = size;
  domain->stats.requested_bytes += size;
  mutex_unlock(&domain->mutex);
  return ASCIICHAT_OK;
}

const void *buffer_share(const void *data) {
  if (!data)
    return NULL;
  buffer_header_t *node = header(data);
  size_t references = atomic_load(&node->references);
  while (references && references != SIZE_MAX) {
    if (atomic_compare_exchange_weak(&node->references, &references, references + 1))
      return data;
  }
  SET_ERRNO(ERROR_INVALID_STATE, "Invalid buffer reference count");
  return NULL;
}

bool buffer_is_unique(const void *data) {
  return data && atomic_load(&header(data)->references) == 1;
}

void buffer_unref(const void *data) {
  if (!data)
    return;
  buffer_header_t *node = header(data);
  buffer_domain_t *domain = node->domain;
  size_t references = atomic_load(&node->references);
  while (references && !atomic_compare_exchange_weak(&node->references, &references, references - 1)) {
  }
  if (!references) {
    SET_ERRNO(ERROR_INVALID_STATE, "Buffer has already been released");
    return;
  }
  if (references > 1) {
    return;
  }
  mutex_lock(&domain->mutex);
  domain->stats.outstanding--;
  domain->stats.live_bytes -= node->charge;
  domain->stats.requested_bytes -= node->length;
  bool done = domain->closing && !domain->stats.outstanding;
  if (!domain->closing && node->capacity <= BUFFER_MAX_CACHED && node->charge <= domain->config.cache_bytes &&
      domain->stats.cached_bytes <= domain->config.cache_bytes - node->charge) {
    node->returned_at = time_get_ns();
    BUFFER_POISON(node->data, node->capacity);
    node->next = domain->bins[node->bin];
    domain->bins[node->bin] = node;
    domain->stats.cached_bytes += node->charge;
  } else {
    dispose(domain, node);
  }
  mutex_unlock(&domain->mutex);
  if (done)
    domain_dispose(domain);
}

asciichat_error_t buffer_workspace_reserve(buffer_workspace_t *workspace, buffer_domain_t *domain, size_t capacity) {
  if (!workspace)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Workspace is NULL");
  if (workspace->length > buffer_capacity(workspace->data))
    return SET_ERRNO(ERROR_INVALID_STATE, "Workspace length exceeds its storage");
  if (capacity <= buffer_capacity(workspace->data))
    return ASCIICHAT_OK;
  void *next = NULL;
  asciichat_error_t result = buffer_acquire(domain, capacity, BUFFER_DEFAULT_ALIGNMENT, &next);
  if (result != ASCIICHAT_OK)
    return result;
  if (workspace->data && workspace->length)
    memcpy(next, workspace->data, workspace->length);
  buffer_release(workspace->data);
  workspace->data = next;
  return ASCIICHAT_OK;
}

void buffer_workspace_destroy(buffer_workspace_t *workspace) {
  if (workspace) {
    buffer_release(workspace->data);
    workspace->length = 0;
  }
}

void buffer_domain_stats(buffer_domain_t *domain, buffer_stats_t *out) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  if (!domain)
    return;
  mutex_lock(&domain->mutex);
  *out = domain->stats;
  mutex_unlock(&domain->mutex);
}

void buffer_domain_log_stats(buffer_domain_t *domain, const char *name) {
  buffer_stats_t stats;
  buffer_domain_stats(domain, &stats);
  log_debug("Buffers %s: reserved=%zu live=%zu requested=%zu cached=%zu outstanding=%zu hits=%llu failures=%llu",
            name ? name : "default", stats.reserved_bytes, stats.live_bytes, stats.requested_bytes, stats.cached_bytes,
            stats.outstanding, (unsigned long long)stats.hits, (unsigned long long)stats.failures);
}

void buffer_system_init(void) {
  if (lifecycle_init_once(&default_lifecycle)) {
    if (buffer_domain_create(NULL, &default_domain) != ASCIICHAT_OK) {
      lifecycle_init_abort(&default_lifecycle);
      return;
    }
    buffer_domain_config_t control = {BUFFER_CONTROL_RESERVE, 1024 * 1024, 5000000000ULL, true};
    if (buffer_domain_create(&control, &control_domain) != ASCIICHAT_OK) {
      buffer_domain_close(&default_domain);
      lifecycle_init_abort(&default_lifecycle);
      return;
    }
    NAMED_REGISTER_ATOMIC(&process_bytes, "buffer.process_reserved_bytes", NULL);
    lifecycle_init_commit(&default_lifecycle);
  }
}

buffer_domain_t *buffer_default_domain(void) {
  buffer_system_init();
  return default_domain;
}

void buffer_system_cleanup(void) {
  if (lifecycle_shutdown(&default_lifecycle)) {
    NAMED_UNREGISTER(&process_bytes);
    buffer_domain_close(&default_domain);
    buffer_domain_close(&control_domain);
  }
}

buffer_domain_t *buffer_control_domain(void) {
  buffer_system_init();
  return control_domain;
}
