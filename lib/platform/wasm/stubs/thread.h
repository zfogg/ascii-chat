#pragma once

/**
 * @file platform/wasm/stubs/thread.h
 * @brief WASM stub for thread.h - avoids including pthread.h which has Emscripten-incompatible attributes
 *
 * In WASM, we don't support actual threads. This provides type definitions
 * that allow the code to compile without including the system pthread.h,
 * which has attributes like 'regparm' that Emscripten doesn't support.
 */

#include <stdbool.h>
#include <stdint.h>

/* WASM doesn't have real threads, so we use void* placeholders */
typedef void* asciichat_thread_t;
typedef void* thread_id_t;
typedef void* tls_key_t;

/* Stub functions that do nothing in WASM */
#define asciichat_thread_create(thread_ptr, attr, start_routine, arg) ASCIICHAT_OK
#define asciichat_thread_join(thread, timeout_ms) ASCIICHAT_OK
#define asciichat_thread_detach(thread) ASCIICHAT_OK
#define asciichat_thread_self() ((asciichat_thread_t)NULL)
#define asciichat_thread_equal(t1, t2) (t1 == t2)
#define asciichat_thread_exit(status) ((void)0)
#define asciichat_thread_initialized(thread) false

#define thread_id_self() ((thread_id_t)NULL)
#define thread_id_equal(id1, id2) (id1 == id2)

#define tls_key_create(key_ptr) ASCIICHAT_OK
#define tls_key_delete(key) ((void)0)
#define tls_getspecific(key) ((void*)NULL)
#define tls_setspecific(key, value) ASCIICHAT_OK
