#include "worker.h"

#include <ascii-chat/atomic.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>

struct acip_quic_worker {
  acip_quic_udp_endpoint_t *endpoint;
  mutex_t *lane;
  acip_quic_worker_failure_fn failure;
  void *context;
  asciichat_thread_t thread;
  atomic_t stopping;
  atomic_t wake_pending;
  asciichat_error_t result;
};

static void *quic_worker_main(void *arg) {
  acip_quic_worker_t *worker = arg;
  while (!atomic_load_u64(&worker->stopping)) {
    uint64_t wait_ns = NS_PER_SEC_INT;
    bool want_write = false;
    acip_quic_connection_t *failed = NULL;
    mutex_lock(worker->lane);
    uint64_t now = time_get_ns();
    asciichat_error_t result = acip_quic_udp_pump(worker->endpoint, now, &failed);
    atomic_store_u64(&worker->wake_pending, 0);
    if (result == ASCIICHAT_OK)
      result = acip_quic_udp_wait_parameters(worker->endpoint, now, NS_PER_SEC_INT, &wait_ns, &want_write);
    mutex_unlock(worker->lane);
    if (result != ASCIICHAT_OK) {
      if (failed && worker->failure && worker->failure(failed, result, worker->context)) {
        mutex_lock(worker->lane);
        bool still_present = acip_quic_udp_contains(worker->endpoint, failed);
        mutex_unlock(worker->lane);
        if (!still_present)
          continue;
      }
      worker->result = result;
      break;
    }
    if (atomic_load_u64(&worker->stopping))
      break;
    bool ready = false;
    result = acip_quic_udp_wait(worker->endpoint, wait_ns, want_write, &ready);
    if (result != ASCIICHAT_OK) {
      worker->result = result;
      break;
    }
  }
  return NULL;
}

asciichat_error_t acip_quic_worker_start(acip_quic_udp_endpoint_t *endpoint, mutex_t *lane,
                                         acip_quic_worker_failure_fn failure, void *context, acip_quic_worker_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC worker output");
  *out = NULL;
  if (!endpoint || !lane)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC worker endpoint or lane");
  acip_quic_worker_t *worker = SAFE_CALLOC(1, sizeof(*worker), acip_quic_worker_t *);
  if (!worker)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC worker");
  worker->endpoint = endpoint;
  worker->lane = lane;
  worker->failure = failure;
  worker->context = context;
  if (acthread_create(&worker->thread, "quic_udp_worker", quic_worker_main, worker) != 0) {
    SAFE_FREE(worker);
    return SET_ERRNO(ERROR_THREAD, "Cannot start QUIC worker");
  }
  *out = worker;
  return ASCIICHAT_OK;
}

void acip_quic_worker_wake(void *arg) {
  acip_quic_worker_t *worker = arg;
  if (worker && atomic_exchange_u64(&worker->wake_pending, 1) == 0 &&
      acip_quic_udp_wake(worker->endpoint) != ASCIICHAT_OK)
    atomic_store_u64(&worker->wake_pending, 0);
}

asciichat_error_t acip_quic_worker_stop(acip_quic_worker_t **worker) {
  if (!worker || !*worker)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC worker stop");
  atomic_store_u64(&(*worker)->stopping, 1);
  acip_quic_worker_wake(*worker);
  if (thread_join(&(*worker)->thread, NULL) != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot join QUIC worker");
  asciichat_error_t result = (*worker)->result;
  SAFE_FREE(*worker);
  *worker = NULL;
  return result;
}
