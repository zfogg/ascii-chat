#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "ringbuffer.h"
#include "common.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static inline bool __attribute__((unused)) is_power_of_two(size_t n) {
    return n && !(n & (n - 1));
}

static inline size_t next_power_of_two(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    if (sizeof(size_t) > 4) {
        n |= n >> 32;
    }
    return n + 1;
}

/* ============================================================================
 * Ring Buffer Implementation
 * ============================================================================ */

ringbuffer_t* ringbuffer_create(size_t element_size, size_t capacity) {
    if (element_size == 0 || capacity == 0) {
        log_error("Invalid ring buffer parameters: element_size=%zu, capacity=%zu", 
                  element_size, capacity);
        return NULL;
    }
    
    ringbuffer_t* rb = (ringbuffer_t*)calloc(1, sizeof(ringbuffer_t));
    if (!rb) {
        log_error("Failed to allocate ring buffer structure");
        return NULL;
    }
    
    /* Round capacity up to power of 2 for optimization */
    size_t actual_capacity = next_power_of_two(capacity);
    
    rb->buffer = (char*)calloc(actual_capacity, element_size);
    if (!rb->buffer) {
        log_error("Failed to allocate ring buffer memory: %zu bytes", 
                  actual_capacity * element_size);
        free(rb);
        return NULL;
    }
    
    rb->element_size = element_size;
    rb->capacity = actual_capacity;
    rb->is_power_of_two = true;
    rb->capacity_mask = actual_capacity - 1;
    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);
    atomic_init(&rb->size, 0);
    
    log_debug("Created ring buffer: capacity=%zu, element_size=%zu", 
              actual_capacity, element_size);
    
    return rb;
}

void ringbuffer_destroy(ringbuffer_t* rb) {
    if (rb) {
        free(rb->buffer);
        free(rb);
        log_debug("Destroyed ring buffer");
    }
}

bool ringbuffer_write(ringbuffer_t* rb, const void* data) {
    if (!rb || !data) return false;
    
    size_t current_size = atomic_load(&rb->size);
    if (current_size >= rb->capacity) {
        return false; /* Buffer full */
    }
    
    size_t head = atomic_load(&rb->head);
    size_t next_head = (head + 1) & rb->capacity_mask;
    
    /* Copy data */
    memcpy(rb->buffer + (head * rb->element_size), data, rb->element_size);
    
    /* Update head and size atomically */
    atomic_store(&rb->head, next_head);
    atomic_fetch_add(&rb->size, 1);
    
    return true;
}

bool ringbuffer_read(ringbuffer_t* rb, void* data) {
    if (!rb || !data) return false;
    
    size_t current_size = atomic_load(&rb->size);
    if (current_size == 0) {
        return false; /* Buffer empty */
    }
    
    size_t tail = atomic_load(&rb->tail);
    size_t next_tail = (tail + 1) & rb->capacity_mask;
    
    /* Copy data */
    memcpy(data, rb->buffer + (tail * rb->element_size), rb->element_size);
    
    /* Update tail and size atomically */
    atomic_store(&rb->tail, next_tail);
    atomic_fetch_sub(&rb->size, 1);
    
    return true;
}

bool ringbuffer_peek(ringbuffer_t* rb, void* data) {
    if (!rb || !data) return false;
    
    size_t current_size = atomic_load(&rb->size);
    if (current_size == 0) {
        return false; /* Buffer empty */
    }
    
    size_t tail = atomic_load(&rb->tail);
    
    /* Copy data without updating tail */
    memcpy(data, rb->buffer + (tail * rb->element_size), rb->element_size);
    
    return true;
}

size_t ringbuffer_size(const ringbuffer_t* rb) {
    return rb ? atomic_load(&rb->size) : 0;
}

bool ringbuffer_is_empty(const ringbuffer_t* rb) {
    return ringbuffer_size(rb) == 0;
}

bool ringbuffer_is_full(const ringbuffer_t* rb) {
    return rb ? ringbuffer_size(rb) >= rb->capacity : true;
}

void ringbuffer_clear(ringbuffer_t* rb) {
    if (rb) {
        atomic_store(&rb->head, 0);
        atomic_store(&rb->tail, 0);
        atomic_store(&rb->size, 0);
        log_debug("Cleared ring buffer");
    }
}

/* ============================================================================
 * Frame Buffer Implementation
 * ============================================================================ */

framebuffer_t* framebuffer_create(size_t capacity, size_t max_frame_size) {
    if (capacity == 0 || max_frame_size == 0) {
        log_error("Invalid frame buffer parameters");
        return NULL;
    }
    
    framebuffer_t* fb = (framebuffer_t*)calloc(1, sizeof(framebuffer_t));
    if (!fb) {
        log_error("Failed to allocate frame buffer structure");
        return NULL;
    }
    
    fb->max_frame_size = max_frame_size;
    fb->rb = ringbuffer_create(max_frame_size, capacity);
    
    if (!fb->rb) {
        free(fb);
        return NULL;
    }
    
    log_info("Created frame buffer: capacity=%zu frames, max_frame_size=%zu", 
             capacity, max_frame_size);
    
    return fb;
}

void framebuffer_destroy(framebuffer_t* fb) {
    if (fb) {
        ringbuffer_destroy(fb->rb);
        free(fb);
        log_info("Destroyed frame buffer");
    }
}

bool framebuffer_write_frame(framebuffer_t* fb, const char* frame) {
    if (!fb || !frame) return false;
    
    size_t frame_len = strlen(frame);
    if (frame_len >= fb->max_frame_size) {
        log_warn("Frame too large: %zu bytes (max: %zu)", 
                 frame_len, fb->max_frame_size - 1);
        return false;
    }
    
    /* Create a temporary buffer to ensure null termination */
    char* temp_frame = (char*)calloc(fb->max_frame_size, 1);
    if (!temp_frame) {
        log_error("Failed to allocate temporary frame buffer");
        return false;
    }
    
    strncpy(temp_frame, frame, fb->max_frame_size - 1);
    bool result = ringbuffer_write(fb->rb, temp_frame);
    
    free(temp_frame);
    
    if (!result) {
        log_debug("Frame buffer full, dropping frame");
    }
    
    return result;
}

bool framebuffer_read_frame(framebuffer_t* fb, char* frame) {
    if (!fb || !frame) return false;
    
    return ringbuffer_read(fb->rb, frame);
} 