#include "http_renderer.h"

#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/time.h>
#include <string.h>

struct http_renderer {
  http_renderer_config_t config;
  mutex_t lock;
  http_renderer_job_t *jobs;
  unsigned active_jobs;
  bool stopping;
};

static void *http_renderer_service_main(void *argument);

static bool http_renderer_cancel_requested(void *context) {
  return context && atomic_load_bool((atomic_t *)context);
}

static void *http_renderer_job_main(void *argument) {
  http_renderer_job_t *job = argument;
  if (job->renderer)
    (void)http_renderer_service_main(job);
  else
    job->execute(job->context, &job->cancelled);
  atomic_store_bool(&job->done, true);
  return NULL;
}

asciichat_error_t http_renderer_job_start(http_renderer_job_t *job, const char *name,
                                          http_renderer_job_fn execute, void *context) {
  if (!job || !execute || job->started)
    return SET_ERRNO(ERROR_INVALID_STATE, "Renderer job is already active");
  memset(job, 0, sizeof(*job));
  job->execute = execute;
  job->context = context;
  atomic_store_bool(&job->cancelled, false);
  atomic_store_bool(&job->done, false);
  if (acthread_create(&job->thread, name ? name : "http-render", http_renderer_job_main, job))
    return SET_ERRNO(ERROR_THREAD, "Cannot start renderer job");
  job->started = true;
  return ASCIICHAT_OK;
}

void http_renderer_job_cancel(http_renderer_job_t *job) {
  if (job && job->started)
    atomic_store_bool(&job->cancelled, true);
}

bool http_renderer_job_is_done(const http_renderer_job_t *job) {
  return job && job->started && atomic_load_bool((atomic_t *)&job->done);
}

void http_renderer_job_join(http_renderer_job_t *job) {
  if (job && job->started) {
    thread_join(&job->thread, NULL);
    job->started = false;
  }
}

void http_renderer_job_reset(http_renderer_job_t *job) {
  if (job) {
    http_renderer_job_join(job);
    memset(job, 0, sizeof(*job));
  }
}

static void *http_renderer_service_main(void *argument) {
  http_renderer_job_t *job = argument;
  http_renderer_t *renderer = job->renderer;
  asciichat_error_t error = ASCIICHAT_OK;
  memset(&job->result, 0, sizeof(job->result));
  job->result.mime_type[0] = '\0';
  if (atomic_load_bool(&job->cancelled)) {
    job->result.cancelled = true;
    error = SET_ERRNO(ERROR_CANCELLED, "HTTP render cancelled");
  } else if (job->request.source.type != HTTP_RENDER_SOURCE_UPLOAD &&
             job->request.source.type != HTTP_RENDER_SOURCE_FILE) {
    error = SET_ERRNO(ERROR_INVALID_PARAM, "HTTP renderer source is not supported");
  } else if (renderer->config.max_input_bytes && job->request.source.body_length > renderer->config.max_input_bytes) {
    error = SET_ERRNO(ERROR_BUFFER_OVERFLOW, "HTTP render input exceeds the configured limit");
  } else {
    error = http_renderer_render_file(job->request.source.path, &job->request, &job->result, &job->cancelled);
  }
  job->result.error = error;
  if (error == ASCIICHAT_OK) {
    if (renderer->config.on_complete)
      renderer->config.on_complete(job, &job->result, renderer->config.user_data);
  } else if (renderer->config.on_error) {
    renderer->config.on_error(job, error, renderer->config.user_data);
  }
  return NULL;
}

asciichat_error_t http_renderer_init(http_renderer_t **out, const http_renderer_config_t *config) {
  if (!out || !config)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing HTTP renderer configuration");
  *out = NULL;
  http_renderer_t *renderer = SAFE_CALLOC(1, sizeof(*renderer), http_renderer_t *);
  renderer->config = *config;
  if (!renderer->config.max_jobs)
    renderer->config.max_jobs = 1;
  if (!renderer->config.max_output_bytes)
    renderer->config.max_output_bytes = HTTP_RENDERER_MAX_OUTPUT;
  if (mutex_init(&renderer->lock, "http_renderer") != 0) {
    SAFE_FREE(renderer);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize HTTP renderer lock");
  }
  *out = renderer;
  return ASCIICHAT_OK;
}

asciichat_error_t http_renderer_submit(http_renderer_t *renderer, const http_render_request_t *request,
                                        http_renderer_job_t **out_job) {
  if (!renderer || !request || !out_job)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP renderer submission");
  *out_job = NULL;
  mutex_lock(&renderer->lock);
  if (renderer->stopping || renderer->active_jobs >= renderer->config.max_jobs) {
    mutex_unlock(&renderer->lock);
    return SET_ERRNO(ERROR_BUFFER_FULL, "HTTP renderer has no available job capacity");
  }
  http_renderer_job_t *job = SAFE_CALLOC(1, sizeof(*job), http_renderer_job_t *);
  job->renderer = renderer;
  job->request = *request;
  if (!job->request.timeout_ms)
    job->request.timeout_ms = renderer->config.timeout_ms;
  if (!job->request.max_output_bytes)
    job->request.max_output_bytes = renderer->config.max_output_bytes;
  atomic_store_bool(&job->cancelled, false);
  atomic_store_bool(&job->done, false);
  if (acthread_create(&job->thread, "http-render", http_renderer_job_main, job)) {
    SAFE_FREE(job);
    mutex_unlock(&renderer->lock);
    return SET_ERRNO(ERROR_THREAD, "Cannot start HTTP renderer job");
  }
  job->started = true;
  job->next = renderer->jobs;
  renderer->jobs = job;
  ++renderer->active_jobs;
  mutex_unlock(&renderer->lock);
  *out_job = job;
  return ASCIICHAT_OK;
}

asciichat_error_t http_renderer_cancel(http_renderer_job_t *job) {
  if (!job || !job->renderer)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP renderer job");
  http_renderer_job_cancel(job);
  return ASCIICHAT_OK;
}

void http_renderer_job_wait(http_renderer_job_t *job) {
  http_renderer_job_join(job);
}

void http_renderer_reap(http_renderer_t *renderer, bool all) {
  if (!renderer)
    return;
  for (;;) {
    mutex_lock(&renderer->lock);
    http_renderer_job_t **link = &renderer->jobs;
    http_renderer_job_t *job = NULL;
    while (*link) {
      if (all || http_renderer_job_is_done(*link)) {
        job = *link;
        *link = job->next;
        --renderer->active_jobs;
        break;
      }
      link = &(*link)->next;
    }
    mutex_unlock(&renderer->lock);
    if (!job)
      return;
    http_renderer_job_join(job);
    http_render_result_destroy(&job->result);
    SAFE_FREE(job);
  }
}

void http_renderer_stop(http_renderer_t *renderer) {
  if (!renderer)
    return;
  mutex_lock(&renderer->lock);
  renderer->stopping = true;
  for (http_renderer_job_t *job = renderer->jobs; job; job = job->next)
    http_renderer_job_cancel(job);
  mutex_unlock(&renderer->lock);
}

void http_renderer_destroy(http_renderer_t **renderer) {
  if (!renderer || !*renderer)
    return;
  http_renderer_stop(*renderer);
  http_renderer_reap(*renderer, true);
  mutex_destroy(&(*renderer)->lock);
  SAFE_FREE(*renderer);
}
#include <stdlib.h>

void http_render_request_init(http_render_request_t *request) {
  if (!request)
    return;
  memset(request, 0, sizeof(*request));
  request->source.type = HTTP_RENDER_SOURCE_UPLOAD;
  request->output_type = HTTP_RENDER_OUTPUT_TEXT;
}

void http_render_result_destroy(http_render_result_t *result) {
  if (!result)
    return;
  SAFE_FREE(result->ascii);
  result->ascii_length = 0;
  result->output_path[0] = '\0';
  result->mime_type[0] = '\0';
}

asciichat_error_t http_renderer_render_file(const char *path, const http_render_request_t *request,
                                            http_render_result_t *result, atomic_t *cancelled) {
  if (!path || !request || !result)
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTP render requires a source path, request, and result");
  memset(result, 0, sizeof(*result));
  uint64_t started = time_get_ns();
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create_interruptible(path, http_renderer_cancel_requested, cancelled);
  if (!decoder)
    return SET_ERRNO(ERROR_MEDIA_INIT, "Cannot decode uploaded image");
  if (ffmpeg_decoder_start_prefetch(decoder) != ASCIICHAT_OK) {
    ffmpeg_decoder_destroy(decoder);
    return SET_ERRNO(ERROR_MEDIA_INIT, "Cannot start image decoder");
  }
  image_t *image = NULL;
  uint64_t deadline = started + (request->timeout_ms ? request->timeout_ms : 10000) * NS_PER_MS_INT;
  for (;;) {
    if (cancelled && atomic_load_bool(cancelled)) {
      ffmpeg_decoder_destroy(decoder);
      result->cancelled = true;
      return SET_ERRNO(ERROR_CANCELLED, "HTTP image render cancelled");
    }
    asciichat_error_context_t failure = {0};
    ffmpeg_frame_status_t status = ffmpeg_decoder_poll_video(decoder, &image, &failure);
    if (status == FFMPEG_FRAME_READY)
      break;
    if (status == FFMPEG_FRAME_FAILED || status == FFMPEG_FRAME_EOF) {
      errno_import(&failure);
      ffmpeg_decoder_destroy(decoder);
      return SET_ERRNO(ERROR_MEDIA_INIT, "Uploaded data does not contain a decodable image");
    }
    if (time_get_ns() >= deadline) {
      ffmpeg_decoder_destroy(decoder);
      return SET_ERRNO(ERROR_TIMEOUT, "Image decoding timed out");
    }
    platform_sleep_ms(1);
  }
  if (!image) {
    ffmpeg_decoder_destroy(decoder);
    return SET_ERRNO(ERROR_MEDIA_INIT, "Uploaded data does not contain a decodable image");
  }
  if (image->w > 8192 || image->h > 8192) {
    image_destroy(image);
    ffmpeg_decoder_destroy(decoder);
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Decoded image dimensions exceed the configured limit");
  }
  size_t width = request->options.width > 0 ? (size_t)request->options.width : 80;
  size_t height = request->options.height > 0 ? (size_t)request->options.height
                                               : (size_t)((double)width * image->h / image->w / 2.0);
  if (!height)
    height = 1;
  image_t *scaled = image_new(width, height);
  if (!scaled) {
    ffmpeg_decoder_destroy(decoder);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP render image");
  }
  image_resize(image, scaled);
  const char *palette = request->options.palette_chars[0] ? request->options.palette_chars
                                                           : request->options.palette[0] ? request->options.palette
                                                                                         : DEFAULT_ASCII_PALETTE;
  char *ascii = image_print(scaled, palette);
  image_destroy(scaled);
  ffmpeg_decoder_destroy(decoder);
  if (!ascii)
    return SET_ERRNO(ERROR_MEDIA_INIT, "Cannot convert image to ASCII");
  size_t length = strlen(ascii);
  size_t limit = request->max_output_bytes ? request->max_output_bytes : 8u * 1024u * 1024u;
  if (length > limit) {
    free(ascii);
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Rendered output exceeds the configured limit");
  }
  result->ascii = SAFE_MALLOC(length + 1, char *);
  SAFE_MEMCPY(result->ascii, length + 1, ascii, length + 1);
  result->ascii_length = length;
  result->elapsed_ms = (time_get_ns() - started) / 1000000;
  free(ascii);
  return ASCIICHAT_OK;
}
