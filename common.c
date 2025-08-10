#include "common.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Memory Debugging (Debug builds only)
 * ============================================================================
 */

#ifdef DEBUG_MEMORY

typedef struct mem_block {
  void *ptr;
  size_t size;
  char file[256];
  int line;
  struct mem_block *next;
} mem_block_t;

static struct {
  mem_block_t *head;
  size_t total_allocated;
  size_t total_freed;
  size_t current_usage;
  size_t peak_usage;
  pthread_mutex_t mutex;
} g_mem = {.head = NULL,
           .total_allocated = 0,
           .total_freed = 0,
           .current_usage = 0,
           .peak_usage = 0,
           .mutex = PTHREAD_MUTEX_INITIALIZER};

/* Use real libc allocators inside debug allocator to avoid recursion */
#undef malloc
#undef free
#undef calloc
#undef realloc

void *debug_malloc(size_t size, const char *file, int line) {
  void *ptr = malloc(size);
  if (!ptr)
    return NULL;

  pthread_mutex_lock(&g_mem.mutex);

  mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
  if (block) {
    block->ptr = ptr;
    block->size = size;
    SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
    block->line = line;
    block->next = g_mem.head;
    g_mem.head = block;

    g_mem.total_allocated += size;
    g_mem.current_usage += size;
    if (g_mem.current_usage > g_mem.peak_usage) {
      g_mem.peak_usage = g_mem.current_usage;
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);

  return ptr;
}

void debug_free(void *ptr, const char *file, int line) {
  if (!ptr)
    return;

  pthread_mutex_lock(&g_mem.mutex);

  mem_block_t *prev = NULL;
  mem_block_t *curr = g_mem.head;

  while (curr) {
    if (curr->ptr == ptr) {
      if (prev) {
        prev->next = curr->next;
      } else {
        g_mem.head = curr->next;
      }

      g_mem.total_freed += curr->size;
      g_mem.current_usage -= curr->size;

      free(curr);
      break;
    }
    prev = curr;
    curr = curr->next;
  }

  if (!curr) {
    log_warn("Freeing untracked pointer %p at %s:%d", ptr, file, line);
  }

  pthread_mutex_unlock(&g_mem.mutex);

  free(ptr);
}

void *debug_calloc(size_t count, size_t size, const char *file, int line) {
  size_t total = count * size;
  void *ptr = calloc(count, size);
  if (!ptr)
    return NULL;

  pthread_mutex_lock(&g_mem.mutex);

  mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
  if (block) {
    block->ptr = ptr;
    block->size = total;
    SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
    block->file[sizeof(block->file) - 1] = '\0';
    block->line = line;
    block->next = g_mem.head;
    g_mem.head = block;

    g_mem.total_allocated += total;
    g_mem.current_usage += total;
    if (g_mem.current_usage > g_mem.peak_usage) {
      g_mem.peak_usage = g_mem.current_usage;
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);

  return ptr;
}

void *debug_realloc(void *ptr, size_t size, const char *file, int line) {
  if (ptr == NULL) {
    return debug_malloc(size, file, line);
  }
  if (size == 0) {
    debug_free(ptr, file, line);
    return NULL;
  }

  pthread_mutex_lock(&g_mem.mutex);

  mem_block_t *prev = NULL;
  mem_block_t *curr = g_mem.head;
  while (curr && curr->ptr != ptr) {
    prev = curr;
    curr = curr->next;
  }

  (void)prev; // Unused variable (for now 🤔 what could i do with this...)

  void *new_ptr = realloc(ptr, size);
  if (!new_ptr) {
    pthread_mutex_unlock(&g_mem.mutex);
    return NULL;
  }

  if (curr) {
    if (size >= curr->size) {
      size_t delta = size - curr->size;
      g_mem.total_allocated += delta;
      g_mem.current_usage += delta;
      if (g_mem.current_usage > g_mem.peak_usage) {
        g_mem.peak_usage = g_mem.current_usage;
      }
    } else {
      size_t delta = curr->size - size;
      g_mem.total_freed += delta;
      g_mem.current_usage -= delta;
    }
    curr->ptr = new_ptr;
    curr->size = size;
    SAFE_STRNCPY(curr->file, file, sizeof(curr->file) - 1);
    curr->file[sizeof(curr->file) - 1] = '\0';
    curr->line = line;
  } else {
    mem_block_t *block = (mem_block_t *)malloc(sizeof(mem_block_t));
    if (block) {
      block->ptr = new_ptr;
      block->size = size;
      SAFE_STRNCPY(block->file, file, sizeof(block->file) - 1);
      block->file[sizeof(block->file) - 1] = '\0';
      block->line = line;
      block->next = g_mem.head;
      g_mem.head = block;

      g_mem.total_allocated += size;
      g_mem.current_usage += size;
      if (g_mem.current_usage > g_mem.peak_usage) {
        g_mem.peak_usage = g_mem.current_usage;
      }
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);

  return new_ptr;
}

void debug_memory_report(void) {
  pthread_mutex_lock(&g_mem.mutex);

  /* Write directly to stderr in case logging is already destroyed at exit */
  fprintf(stderr, "\n=== Memory Report ===\n");
  fprintf(stderr, "Total allocated: %zu bytes\n", g_mem.total_allocated);
  fprintf(stderr, "Total freed: %zu bytes\n", g_mem.total_freed);
  fprintf(stderr, "Current usage: %zu bytes\n", g_mem.current_usage);
  fprintf(stderr, "Peak usage: %zu bytes\n", g_mem.peak_usage);

  if (g_mem.head) {
    fprintf(stderr, "\nLeaked allocations:\n");
    mem_block_t *curr = g_mem.head;
    while (curr) {
      fprintf(stderr, "  - %zu bytes at %s:%d\n", curr->size, curr->file, curr->line);
      curr = curr->next;
    }
  }

  pthread_mutex_unlock(&g_mem.mutex);
}

#endif /* DEBUG_MEMORY */