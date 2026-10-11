#include <ascii-chat/platform/event.h>
#include <ascii-chat/common.h>
#include <uv.h>
#include <string.h>
#include <ascii-chat/buffer.h>

#define EVENT_COMMAND_LIMIT 1024
#define EVENT_WRITE_LIMIT (16U * 1024U * 1024U)
#define EVENT_WRITE_COUNT_LIMIT 4096
#define EVENT_READ_SIZE 65536

typedef struct {
  platform_event_task_fn fn;
  void *arg;
} event_command_t;

struct platform_event_loop {
  uv_loop_t uv;
  uv_async_t wake;
  uv_timer_t tick;
  platform_event_task_fn tick_fn;
  void *tick_arg;
  bool tick_initialized;
  size_t streams;
  uv_mutex_t mutex;
  event_command_t commands[EVENT_COMMAND_LIMIT];
  size_t head, count;
  bool stopping;
};

struct platform_event_stream {
  bool listener;
  platform_event_loop_t *owner;
  uv_tcp_t tcp;
  uv_timer_t timer;
  uv_timer_t output_timer;
  uv_async_t output;
  uv_mutex_t mutex;
  struct event_write *first, *last, *active;
  bool zerocopy;
  platform_event_read_fn read;
  platform_event_close_fn close;
  void *arg;
  size_t queued;
  size_t writes;
  unsigned handles;
  bool closing;
  bool working;
  bool close_requested;
  asciichat_error_t requested_reason;
  asciichat_error_t reason;
  char input[EVENT_READ_SIZE];
};

struct platform_event_listener {
  bool listener;
  uv_tcp_t tcp;
  platform_event_accept_fn accept;
  void *arg;
};

typedef struct event_write {
  uv_write_t request;
  platform_event_stream_t *stream;
  platform_event_write_fn done;
  void *arg;
  size_t size;
  struct event_write *next;
  socket_send_buffer_t storage;
  size_t offset;
  uint64_t started;
  bool waiting;
} event_write_t;

static asciichat_error_t event_error(int status, const char *operation) {
  return SET_ERRNO(ERROR_NETWORK, "%s: %s", operation, uv_strerror(status));
}

static void flush_output(uv_async_t *output);
static void write_complete(uv_write_t *request, int status);

static void stream_closed(uv_handle_t *handle) {
  platform_event_stream_t *stream = handle->data;
  if (--stream->handles)
    return;
  if (stream->close)
    stream->close(stream->arg, stream->reason);
  uv_mutex_destroy(&stream->mutex);
  --stream->owner->streams;
  SAFE_FREE(stream);
}

void platform_event_stream_close(platform_event_stream_t *stream, asciichat_error_t reason) {
  if (!stream)
    return;
  uv_mutex_lock(&stream->mutex);
  if (stream->closing) {
    uv_mutex_unlock(&stream->mutex);
    return;
  }
  stream->closing = true;
  event_write_t *pending = stream->first;
  stream->first = stream->last = NULL;
  uv_mutex_unlock(&stream->mutex);
  while (pending) {
    event_write_t *next = pending->next;
    uv_mutex_lock(&stream->mutex);
    stream->queued -= pending->size;
    --stream->writes;
    uv_mutex_unlock(&stream->mutex);
    if (pending->done)
      pending->done(pending->arg, ERROR_NETWORK);
    socket_send_buffer_free(&pending->storage);
    SAFE_FREE(pending);
    pending = next;
  }
  stream->reason = reason;
  // Submitted uv_write requests complete during TCP close. A raw zerocopy
  // submission has no libuv request; discard its mapping without recycling it.
  if (stream->active && stream->active->waiting)
    write_complete(&stream->active->request, UV_ECANCELED);
  uv_timer_stop(&stream->output_timer);
  uv_close((uv_handle_t *)&stream->output_timer, stream_closed);
  uv_read_stop((uv_stream_t *)&stream->tcp);
  uv_timer_stop(&stream->timer);
  uv_close((uv_handle_t *)&stream->timer, stream_closed);
  uv_close((uv_handle_t *)&stream->tcp, stream_closed);
  uv_close((uv_handle_t *)&stream->output, stream_closed);
}

static void listener_closed(uv_handle_t *handle) {
  platform_event_listener_t *listener = handle->data;
  SAFE_FREE(listener);
}

void platform_event_listener_close(platform_event_listener_t *listener) {
  if (listener && !uv_is_closing((uv_handle_t *)&listener->tcp))
    uv_close((uv_handle_t *)&listener->tcp, listener_closed);
}

static void stop_handle(uv_handle_t *handle, void *arg) {
  platform_event_loop_t *loop = arg;
  if (uv_is_closing(handle))
    return;
  if (handle->type == UV_TCP) {
    if (*(bool *)handle->data)
      platform_event_listener_close(handle->data);
    else
      platform_event_stream_close(handle->data, ERROR_NETWORK);
  } else if (handle == (uv_handle_t *)&loop->wake || handle == (uv_handle_t *)&loop->tick) {
    uv_close(handle, NULL);
  }
}

static void wake_loop(uv_async_t *wake) {
  platform_event_loop_t *loop = wake->data;
  // Bound each batch so a producer cannot starve socket events and deadlines.
  for (size_t i = 0; i < EVENT_COMMAND_LIMIT; ++i) {
    uv_mutex_lock(&loop->mutex);
    if (!loop->count) {
      bool stopping = loop->stopping;
      uv_mutex_unlock(&loop->mutex);
      if (stopping)
        uv_walk(&loop->uv, stop_handle, loop);
      return;
    }
    event_command_t command = loop->commands[loop->head];
    loop->head = (loop->head + 1) % EVENT_COMMAND_LIMIT;
    --loop->count;
    uv_mutex_unlock(&loop->mutex);
    command.fn(command.arg);
  }
  uv_async_send(wake);
}

asciichat_error_t platform_event_loop_create(platform_event_loop_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing event loop output");
  *out = NULL;
  platform_event_loop_t *loop = SAFE_CALLOC(1, sizeof(*loop), platform_event_loop_t *);
  int status = uv_loop_init(&loop->uv);
  if (status < 0) {
    SAFE_FREE(loop);
    return event_error(status, "Initialize event loop");
  }
  status = uv_mutex_init(&loop->mutex);
  if (status < 0) {
    uv_loop_close(&loop->uv);
    SAFE_FREE(loop);
    return event_error(status, "Initialize event queue");
  }
  status = uv_async_init(&loop->uv, &loop->wake, wake_loop);
  if (status < 0) {
    uv_mutex_destroy(&loop->mutex);
    uv_loop_close(&loop->uv);
    SAFE_FREE(loop);
    return event_error(status, "Initialize event wakeup");
  }
  loop->wake.data = loop;
  loop->uv.data = loop;
  *out = loop;
  return ASCIICHAT_OK;
}

asciichat_error_t platform_event_loop_run(platform_event_loop_t *loop) {
  if (!loop)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing event loop");
  uv_run(&loop->uv, UV_RUN_DEFAULT);
  return ASCIICHAT_OK;
}

asciichat_error_t platform_event_loop_post(platform_event_loop_t *loop, platform_event_task_fn fn, void *arg) {
  if (!loop || !fn)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing event command");
  uv_mutex_lock(&loop->mutex);
  if (loop->stopping || loop->count == EVENT_COMMAND_LIMIT) {
    uv_mutex_unlock(&loop->mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Event queue stopped or full");
  }
  loop->commands[(loop->head + loop->count) % EVENT_COMMAND_LIMIT] = (event_command_t){fn, arg};
  ++loop->count;
  // Keep the wake handle alive until this send has finished.
  uv_async_send(&loop->wake);
  uv_mutex_unlock(&loop->mutex);
  return ASCIICHAT_OK;
}

void platform_event_loop_stop(platform_event_loop_t *loop) {
  if (!loop)
    return;
  uv_mutex_lock(&loop->mutex);
  if (!loop->stopping) {
    loop->stopping = true;
    uv_async_send(&loop->wake);
  }
  uv_mutex_unlock(&loop->mutex);
}

asciichat_error_t platform_event_loop_destroy(platform_event_loop_t *loop) {
  if (!loop)
    return ASCIICHAT_OK;
  int status = uv_loop_close(&loop->uv);
  if (status < 0)
    return event_error(status, "Destroy event loop (stop and join first)");
  uv_mutex_destroy(&loop->mutex);
  SAFE_FREE(loop);
  return ASCIICHAT_OK;
}

const char *platform_event_backend(void) {
#ifdef _WIN32
  return "iocp";
#elif defined(__linux__)
  return "epoll";
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  return "kqueue";
#else
  return "libuv";
#endif
}

static void tick_loop(uv_timer_t *timer) {
  platform_event_loop_t *loop = timer->data;
  loop->tick_fn(loop->tick_arg);
}

asciichat_error_t platform_event_loop_tick(platform_event_loop_t *loop, uint64_t interval, platform_event_task_fn fn,
                                           void *arg) {
  if (!loop || !interval || !fn || loop->tick_initialized)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid event loop tick");
  int status = uv_timer_init(&loop->uv, &loop->tick);
  if (status < 0)
    return event_error(status, "Initialize loop timer");
  loop->tick_initialized = true;
  loop->tick.data = loop;
  loop->tick_fn = fn;
  loop->tick_arg = arg;
  status = uv_timer_start(&loop->tick, tick_loop, interval, interval);
  return status < 0 ? event_error(status, "Start loop timer") : ASCIICHAT_OK;
}

static void allocate_read(uv_handle_t *handle, size_t suggested, uv_buf_t *buffer) {
  (void)suggested;
  platform_event_stream_t *stream = handle->data;
  *buffer = uv_buf_init(stream->input, sizeof(stream->input));
}

static void read_stream(uv_stream_t *handle, ssize_t count, const uv_buf_t *buffer) {
  platform_event_stream_t *stream = handle->data;
  if (count < 0) {
    platform_event_stream_close(stream, count == UV_EOF ? ASCIICHAT_OK : ERROR_NETWORK);
  } else if (count && !stream->closing && stream->read) {
    stream->read(stream, buffer->base, (size_t)count, stream->arg);
  }
}

asciichat_error_t platform_event_stream_start(platform_event_stream_t *stream, platform_event_read_fn read,
                                              platform_event_close_fn close, void *arg) {
  if (!stream || stream->closing || !read)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid stream callbacks");
  stream->read = read;
  stream->close = close;
  stream->arg = arg;
  return platform_event_stream_resume(stream);
}

void platform_event_stream_pause(platform_event_stream_t *stream) {
  if (stream && !stream->closing)
    uv_read_stop((uv_stream_t *)&stream->tcp);
}

asciichat_error_t platform_event_stream_resume(platform_event_stream_t *stream) {
  if (!stream || stream->closing || !stream->read)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stream cannot resume");
  int status = uv_read_start((uv_stream_t *)&stream->tcp, allocate_read, read_stream);
  return status < 0 ? event_error(status, "Start stream read") : ASCIICHAT_OK;
}

static void deadline_expired(uv_timer_t *timer) {
  platform_event_stream_close(timer->data, ERROR_NETWORK_TIMEOUT);
}

asciichat_error_t platform_event_stream_deadline(platform_event_stream_t *stream, uint64_t timeout_ms) {
  if (!stream || stream->closing)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stream is closing");
  uv_timer_stop(&stream->timer);
  int status = timeout_ms ? uv_timer_start(&stream->timer, deadline_expired, timeout_ms, 0) : 0;
  return status < 0 ? event_error(status, "Set stream deadline") : ASCIICHAT_OK;
}

static platform_event_stream_t *create_stream(uv_loop_t *loop) {
  platform_event_stream_t *stream = SAFE_CALLOC(1, sizeof(*stream), platform_event_stream_t *);
  int status = uv_mutex_init(&stream->mutex);
  if (status < 0) {
    SAFE_FREE(stream);
    return NULL;
  }
  status = uv_tcp_init(loop, &stream->tcp);
  if (status < 0) {
    uv_mutex_destroy(&stream->mutex);
    SAFE_FREE(stream);
    return NULL;
  }
  stream->tcp.data = stream;
  stream->owner = loop->data;
  stream->zerocopy = true;
  ++stream->owner->streams;
  stream->handles = 1;
  status = uv_timer_init(loop, &stream->timer);
  if (status < 0) {
    uv_close((uv_handle_t *)&stream->tcp, stream_closed);
    return NULL;
  }
  stream->timer.data = stream;
  ++stream->handles;
  status = uv_timer_init(loop, &stream->output_timer);
  if (status < 0) {
    uv_close((uv_handle_t *)&stream->timer, stream_closed);
    uv_close((uv_handle_t *)&stream->tcp, stream_closed);
    return NULL;
  }
  stream->output_timer.data = stream;
  ++stream->handles;
  status = uv_async_init(loop, &stream->output, flush_output);
  if (status < 0) {
    uv_close((uv_handle_t *)&stream->output_timer, stream_closed);
    uv_close((uv_handle_t *)&stream->timer, stream_closed);
    uv_close((uv_handle_t *)&stream->tcp, stream_closed);
    return NULL;
  }
  stream->output.data = stream;
  ++stream->handles;
  return stream;
}

asciichat_error_t platform_event_stream_adopt(platform_event_loop_t *loop, socket_t *socket,
                                              platform_event_stream_t **out) {
  if (!loop || !socket || !out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket adoption");
  *out = NULL;
  platform_event_stream_t *stream = create_stream(&loop->uv);
  if (!stream)
    return SET_ERRNO(ERROR_MEMORY, "Cannot initialize event stream");
  int status = uv_tcp_open(&stream->tcp, (uv_os_sock_t)*socket);
  if (status < 0) {
    platform_event_stream_close(stream, ERROR_NETWORK);
    return event_error(status, "Adopt TCP stream");
  }
  *socket = INVALID_SOCKET_VALUE;
  uv_tcp_nodelay(&stream->tcp, 1);
  uv_tcp_keepalive(&stream->tcp, 1, 60);
  *out = stream;
  return ASCIICHAT_OK;
}

static void accepted(uv_stream_t *handle, int status) {
  if (status < 0)
    return;
  platform_event_listener_t *listener = handle->data;
  platform_event_stream_t *stream = create_stream(handle->loop);
  if (!stream)
    return;
  status = uv_accept(handle, (uv_stream_t *)&stream->tcp);
  if (status < 0) {
    platform_event_stream_close(stream, ERROR_NETWORK);
    return;
  }
  uv_tcp_nodelay(&stream->tcp, 1);
  uv_tcp_keepalive(&stream->tcp, 1, 60);
  if (stream->owner->streams > 512) {
    platform_event_stream_close(stream, ERROR_NETWORK_SIZE);
    return;
  }
  listener->accept(stream, listener->arg);
}

asciichat_error_t platform_event_listen(platform_event_loop_t *loop, socket_t *socket, platform_event_accept_fn accept,
                                        void *arg, platform_event_listener_t **out) {
  if (!loop || !socket || !accept || !out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid event listener");
  *out = NULL;
  platform_event_listener_t *listener = SAFE_CALLOC(1, sizeof(*listener), platform_event_listener_t *);
  int status = uv_tcp_init(&loop->uv, &listener->tcp);
  if (status < 0) {
    SAFE_FREE(listener);
    return event_error(status, "Initialize listener");
  }
  listener->tcp.data = listener;
  listener->listener = true;
  listener->accept = accept;
  listener->arg = arg;
  status = uv_tcp_open(&listener->tcp, (uv_os_sock_t)*socket);
  if (status < 0) {
    uv_close((uv_handle_t *)&listener->tcp, listener_closed);
    return event_error(status, "Adopt listener");
  }
  *socket = INVALID_SOCKET_VALUE;
  status = uv_listen((uv_stream_t *)&listener->tcp, 128, accepted);
  if (status < 0) {
    // Adoption has succeeded: the loop owns the descriptor even if listen fails.
    uv_close((uv_handle_t *)&listener->tcp, listener_closed);
    return event_error(status, "Listen");
  }
  *out = listener;
  return ASCIICHAT_OK;
}

static void write_complete(uv_write_t *request, int status) {
  event_write_t *write = request->data;
  platform_event_stream_t *stream = write->stream;
  uv_timer_stop(&stream->output_timer);
  stream->active = NULL;
  uv_mutex_lock(&stream->mutex);
  stream->queued -= write->size;
  --stream->writes;
  uv_mutex_unlock(&stream->mutex);
  if (write->done)
    write->done(write->arg, status < 0 ? ERROR_NETWORK : ASCIICHAT_OK);
  socket_send_buffer_free(&write->storage);
  SAFE_FREE(write);
  if (status < 0)
    platform_event_stream_close(stream, ERROR_NETWORK);
  else if (!stream->closing)
    uv_async_send(&stream->output);
}

static socket_t stream_socket(platform_event_stream_t *stream) {
  uv_os_fd_t descriptor;
  if (uv_fileno((uv_handle_t *)&stream->tcp, &descriptor) < 0)
    return INVALID_SOCKET_VALUE;
  return (socket_t)(uintptr_t)descriptor;
}

static void submit_output(platform_event_stream_t *stream) {
  event_write_t *write = stream->active;
  uv_mutex_lock(&stream->mutex);
  bool zerocopy = stream->zerocopy;
  uv_mutex_unlock(&stream->mutex);
  if (zerocopy && write->storage.mapped && write->size - write->offset >= SOCKET_ZEROCOPY_THRESHOLD) {
    size_t sent = 0;
    bool supported;
    asciichat_error_t result = socket_zerocopy_try(stream_socket(stream), &write->storage, write->offset,
                                                   write->size - write->offset, &sent, &supported);
    if (result != ASCIICHAT_OK) {
      write_complete(&write->request, UV_EIO);
      return;
    }
    if (!supported) {
      uv_mutex_lock(&stream->mutex);
      stream->zerocopy = false;
      uv_mutex_unlock(&stream->mutex);
    }
    if (sent) {
      write->offset += sent;
      write->waiting = true;
      return;
    }
  }
  // No kernel-pinned submission is outstanding here. libuv handles readiness,
  // partial writes, and completion on every platform for the remaining bytes.
  uv_buf_t buffer =
      uv_buf_init((char *)write->storage.data + write->offset, (unsigned int)(write->size - write->offset));
  int status = uv_write(&write->request, (uv_stream_t *)&stream->tcp, &buffer, 1, write_complete);
  if (status < 0)
    write_complete(&write->request, status);
}

static void output_progress(uv_timer_t *timer) {
  platform_event_stream_t *stream = timer->data;
  event_write_t *write = stream->active;
  if (!write || stream->closing)
    return;
  if (uv_now(stream->tcp.loop) - write->started >= 30000) {
    platform_event_stream_close(stream, ERROR_NETWORK_TIMEOUT);
    return;
  }
  if (!write->waiting)
    return;
  bool completed, copied;
  asciichat_error_t result = socket_zerocopy_poll(stream_socket(stream), &completed, &copied);
  if (result != ASCIICHAT_OK) {
    platform_event_stream_close(stream, result);
    return;
  }
  if (!completed)
    return;
  write->waiting = false;
  if (copied) {
    uv_mutex_lock(&stream->mutex);
    stream->zerocopy = false;
    uv_mutex_unlock(&stream->mutex);
  }
  if (write->offset == write->size)
    write_complete(&write->request, 0);
  else
    submit_output(stream);
}

static void flush_output(uv_async_t *output) {
  platform_event_stream_t *stream = output->data;
  uv_mutex_lock(&stream->mutex);
  if (stream->close_requested) {
    asciichat_error_t reason = stream->requested_reason;
    uv_mutex_unlock(&stream->mutex);
    platform_event_stream_close(stream, reason);
    return;
  }
  if (stream->closing || stream->active || !stream->first) {
    uv_mutex_unlock(&stream->mutex);
    return;
  }
  event_write_t *write = stream->first;
  stream->first = write->next;
  if (!stream->first)
    stream->last = NULL;
  stream->active = write;
  uv_mutex_unlock(&stream->mutex);
  write->started = uv_now(stream->tcp.loop);
  // Only copy-avoidance submissions need completion probing. Ordinary writes
  // use this timer solely for their deadline.
  uint64_t interval = write->storage.mapped ? 5 : 30000;
  int status = uv_timer_start(&stream->output_timer, output_progress, interval, interval);
  if (status < 0) {
    write_complete(&write->request, status);
    return;
  }
  submit_output(stream);
}

void platform_event_stream_set_zerocopy(platform_event_stream_t *stream, bool enabled) {
  if (!stream)
    return;
  uv_mutex_lock(&stream->mutex);
  stream->zerocopy = enabled;
  uv_mutex_unlock(&stream->mutex);
}

void platform_event_stream_request_close(platform_event_stream_t *stream, asciichat_error_t reason) {
  if (!stream)
    return;
  uv_mutex_lock(&stream->mutex);
  if (!stream->closing && !stream->close_requested) {
    stream->close_requested = true;
    stream->requested_reason = reason;
    uv_async_send(&stream->output);
  }
  uv_mutex_unlock(&stream->mutex);
}

asciichat_error_t platform_event_stream_write(platform_event_stream_t *stream, const socket_buffer_t *buffers,
                                              size_t count, platform_event_write_fn done, void *arg) {
  if (!stream || !buffers || !count || count > SOCKET_IOV_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid asynchronous write");
  size_t size = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((!buffers[i].data && buffers[i].len) || buffers[i].len > EVENT_WRITE_LIMIT - size)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Asynchronous write too large");
    size += buffers[i].len;
  }
  uv_mutex_lock(&stream->mutex);
  if (stream->closing || stream->close_requested || !size || stream->writes >= EVENT_WRITE_COUNT_LIMIT ||
      size > EVENT_WRITE_LIMIT - stream->queued) {
    uv_mutex_unlock(&stream->mutex);
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Asynchronous output queue full");
  }
  event_write_t *write = SAFE_MALLOC(sizeof(*write), event_write_t *);
  *write = (event_write_t){.stream = stream, .done = done, .arg = arg, .size = size};
  if (stream->zerocopy && size >= SOCKET_ZEROCOPY_THRESHOLD) {
    if (socket_send_buffer_alloc(size, &write->storage) != ASCIICHAT_OK) {
      SAFE_FREE(write);
      uv_mutex_unlock(&stream->mutex);
      return SET_ERRNO(ERROR_MEMORY, "Async copy avoidance exceeds process buffer budget");
    }
  } else {
    write->storage.data = buffer_alloc(NULL, size);
    write->storage.capacity = size;
  }
  if (!write->storage.data) {
    SAFE_FREE(write);
    uv_mutex_unlock(&stream->mutex);
    return SET_ERRNO(ERROR_MEMORY, "Async output exceeds process buffer budget");
  }
  write->request.data = write;
  size_t offset = 0;
  for (size_t i = 0; i < count; ++i) {
    if (buffers[i].len)
      memcpy((char *)write->storage.data + offset, buffers[i].data, buffers[i].len);
    offset += buffers[i].len;
  }
  stream->queued += size;
  ++stream->writes;
  if (stream->last)
    stream->last->next = write;
  else
    stream->first = write;
  stream->last = write;
  uv_async_send(&stream->output);
  uv_mutex_unlock(&stream->mutex);
  return ASCIICHAT_OK;
}

size_t platform_event_stream_queued_bytes(const platform_event_stream_t *stream) {
  if (!stream)
    return 0;
  uv_mutex_lock((uv_mutex_t *)&stream->mutex);
  size_t bytes = stream->queued;
  uv_mutex_unlock((uv_mutex_t *)&stream->mutex);
  return bytes;
}

typedef struct {
  uv_work_t work;
  platform_event_stream_t *stream;
  platform_event_work_fn run;
  platform_event_work_done_fn done;
  void *arg;
} event_work_t;

typedef struct {
  uv_work_t request;
  platform_event_task_fn work, done;
  void *arg;
} loop_work_t;

static void loop_work_run(uv_work_t *request) {
  loop_work_t *work = request->data;
  work->work(work->arg);
}
static void loop_work_done(uv_work_t *request, int status) {
  (void)status;
  loop_work_t *work = request->data;
  work->done(work->arg);
  SAFE_FREE(work);
}
asciichat_error_t platform_event_loop_work(platform_event_loop_t *loop, platform_event_task_fn run,
                                           platform_event_task_fn done, void *arg) {
  if (!loop || !run || !done)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid event worker");
  loop_work_t *work = SAFE_CALLOC(1, sizeof(*work), loop_work_t *);
  work->request.data = work;
  work->work = run;
  work->done = done;
  work->arg = arg;
  int status = uv_queue_work(&loop->uv, &work->request, loop_work_run, loop_work_done);
  if (status < 0) {
    SAFE_FREE(work);
    return event_error(status, "Queue loop work");
  }
  return ASCIICHAT_OK;
}

static void run_work(uv_work_t *request) {
  event_work_t *work = request->data;
  work->run(work->arg);
}

static void finish_work(uv_work_t *request, int status) {
  (void)status;
  event_work_t *work = request->data;
  platform_event_stream_t *stream = work->stream;
  stream->working = false;
  work->done(stream, work->arg, stream->closing);
  SAFE_FREE(work);
  // The extra reference keeps application state alive through worker completion.
  stream_closed((uv_handle_t *)&stream->tcp);
}

asciichat_error_t platform_event_stream_work(platform_event_stream_t *stream, platform_event_work_fn run,
                                             platform_event_work_done_fn done, void *arg) {
  if (!stream || stream->closing || stream->working || !run || !done)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stream work already active or closing");
  event_work_t *work = SAFE_CALLOC(1, sizeof(*work), event_work_t *);
  work->stream = stream;
  work->run = run;
  work->done = done;
  work->arg = arg;
  work->work.data = work;
  platform_event_stream_pause(stream);
  int status = uv_queue_work(stream->tcp.loop, &work->work, run_work, finish_work);
  if (status < 0) {
    SAFE_FREE(work);
    return event_error(status, "Queue stream work");
  }
  stream->working = true;
  ++stream->handles;
  return ASCIICHAT_OK;
}

asciichat_error_t platform_event_stream_peer(platform_event_stream_t *stream, struct sockaddr_storage *address,
                                             socklen_t *length) {
  if (!stream || !address || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer address output");
  int size = (int)*length;
  int status = uv_tcp_getpeername(&stream->tcp, (struct sockaddr *)address, &size);
  *length = (socklen_t)size;
  return status < 0 ? event_error(status, "Get stream peer") : ASCIICHAT_OK;
}
