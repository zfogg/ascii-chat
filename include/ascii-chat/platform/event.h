#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>

/** Native asynchronous networking. Call handle operations on the loop thread.
 * post() and stop() may be called by other threads. The caller owns the loop
 * thread and must join it before destroy(). No application callback may block.
 * WASM uses browser transports and does not build this backend.
 */
typedef struct platform_event_loop platform_event_loop_t;
typedef struct platform_event_stream platform_event_stream_t;
typedef struct platform_event_listener platform_event_listener_t;
typedef void (*platform_event_task_fn)(void *arg);
typedef void (*platform_event_read_fn)(platform_event_stream_t *, const void *, size_t, void *);
typedef void (*platform_event_close_fn)(void *, asciichat_error_t);
typedef void (*platform_event_write_fn)(void *, asciichat_error_t);
typedef void (*platform_event_queue_fn)(void *, size_t);
typedef void (*platform_event_accept_fn)(platform_event_stream_t *, void *);
typedef void (*platform_event_work_fn)(void *);
typedef void (*platform_event_work_done_fn)(platform_event_stream_t *, void *, bool closing);

asciichat_error_t platform_event_loop_create(platform_event_loop_t **out);
asciichat_error_t platform_event_loop_run(platform_event_loop_t *loop);
asciichat_error_t platform_event_loop_post(platform_event_loop_t *loop, platform_event_task_fn fn, void *arg);
void platform_event_loop_stop(platform_event_loop_t *loop);
asciichat_error_t platform_event_loop_destroy(platform_event_loop_t *loop);
const char *platform_event_backend(void);
asciichat_error_t platform_event_loop_tick(platform_event_loop_t *, uint64_t, platform_event_task_fn, void *);
asciichat_error_t platform_event_loop_work(platform_event_loop_t *, platform_event_task_fn, platform_event_task_fn,
                                           void *);

/** Sets *socket to INVALID_SOCKET_VALUE when ownership transfers, including if
 * listen subsequently fails. Accepted streams are owned by the
 * loop until their close callback returns. Set callbacks in the accept callback.
 */
asciichat_error_t platform_event_listen(platform_event_loop_t *, socket_t *, platform_event_accept_fn, void *,
                                        platform_event_listener_t **);
void platform_event_listener_close(platform_event_listener_t *);
asciichat_error_t platform_event_stream_start(platform_event_stream_t *, platform_event_read_fn,
                                              platform_event_close_fn, void *);
/** Adopt an already-connected socket, setting it invalid on ownership transfer. */
asciichat_error_t platform_event_stream_adopt(platform_event_loop_t *, socket_t *, platform_event_stream_t **);
/** Copies input before returning. Completion means local write completion, not
 * peer acknowledgement. Rejected writes never invoke completion. Accepted writes
 * invoke completion exactly once, including during shutdown, before on_close.
 */
asciichat_error_t platform_event_stream_write(platform_event_stream_t *, const socket_buffer_t *, size_t,
                                              platform_event_write_fn, void *);
void platform_event_stream_pause(platform_event_stream_t *);
asciichat_error_t platform_event_stream_resume(platform_event_stream_t *);
void platform_event_stream_close(platform_event_stream_t *, asciichat_error_t);
/** Reject new writes, drain accepted output, then close; write deadlines still apply. */
void platform_event_stream_request_close(platform_event_stream_t *, asciichat_error_t);
/** Configure copy avoidance before starting producers. The loop always owns I/O. */
void platform_event_stream_set_zerocopy(platform_event_stream_t *, bool enabled);
/** Relative timeout converted to an absolute deadline; zero disables it. */
asciichat_error_t platform_event_stream_deadline(platform_event_stream_t *, uint64_t timeout_ms);
asciichat_error_t platform_event_stream_peer(platform_event_stream_t *, struct sockaddr_storage *, socklen_t *);
size_t platform_event_stream_queued_bytes(const platform_event_stream_t *);
/** Publish queued-byte changes without polling the stream. Configure before
 * producers start. Invoked under the stream lock; only publish atomic state,
 * never call stream/transport APIs. Context remains alive through on_close. */
void platform_event_stream_observe_queue(platform_event_stream_t *, platform_event_queue_fn, void *);
/** Run blocking application work outside the loop. One job per stream at a time;
 * reads pause until done runs on the loop. Stream lifetime includes this job.
 * write(), request_close(), and queued_bytes() are thread-safe while the caller
 * holds a live stream reference; other stream operations are loop-only.
 */
asciichat_error_t platform_event_stream_work(platform_event_stream_t *, platform_event_work_fn,
                                             platform_event_work_done_fn, void *);
