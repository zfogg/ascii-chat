#include "http_renderer.h"

#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/simd/cells.h>
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
  if (job->renderer) {
    if (job->result.error == ASCIICHAT_OK) {
      if (job->renderer->config.on_complete)
        job->renderer->config.on_complete(job, &job->result, job->renderer->config.user_data);
    } else if (job->renderer->config.on_error) {
      job->renderer->config.on_error(job, job->result.error, job->renderer->config.user_data);
    }
  }
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
                                        void *user_data, http_renderer_job_t **out_job) {
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
  job->user_data = user_data;
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
      if (all || (job->user_data == NULL && http_renderer_job_is_done(*link))) {
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
  for (size_t i = 0; i < result->frame_count; ++i)
    SAFE_FREE(result->frames[i].text);
  SAFE_FREE(result->frames);
  result->frame_count = 0;
  result->frame_capacity = 0;
  result->ascii_length = 0;
  result->output_path[0] = '\0';
  result->mime_type[0] = '\0';
}

static asciichat_error_t http_render_result_reserve_frame(http_render_result_t *result) {
  if (result->frame_count >= HTTP_RENDERER_MAX_FRAMES)
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Rendered frame count exceeds the configured limit");
  if (result->frame_count == result->frame_capacity) {
    size_t capacity = result->frame_capacity ? result->frame_capacity * 2 : 4;
    if (capacity > HTTP_RENDERER_MAX_FRAMES) capacity = HTTP_RENDERER_MAX_FRAMES;
    result->frames = realloc(result->frames, capacity * sizeof(*result->frames));
    if (!result->frames)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate rendered frame list");
    memset(result->frames + result->frame_capacity, 0,
           (capacity - result->frame_capacity) * sizeof(*result->frames));
    result->frame_capacity = capacity;
  }
  return ASCIICHAT_OK;
}

asciichat_error_t http_render_result_add_text_frame(http_render_result_t *result, const char *text,
                                                    size_t length, uint64_t timestamp_ms) {
  if (!result || (!text && length)) return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid text frame");
  asciichat_error_t err = http_render_result_reserve_frame(result);
  if (err != ASCIICHAT_OK) return err;
  http_render_frame_t *frame = &result->frames[result->frame_count++];
  frame->text = SAFE_MALLOC(length + 1, char *);
  SAFE_MEMCPY(frame->text, length + 1, text, length);
  frame->text[length] = '\0'; frame->text_length = length; frame->timestamp_ms = timestamp_ms;
  return ASCIICHAT_OK;
}

asciichat_error_t http_render_result_add_file_frame(http_render_result_t *result, const char *path,
                                                    uint64_t timestamp_ms) {
  if (!result || !path || !*path) return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid file frame");
  asciichat_error_t err = http_render_result_reserve_frame(result);
  if (err != ASCIICHAT_OK) return err;
  http_render_frame_t *frame = &result->frames[result->frame_count++];
  SAFE_STRNCPY(frame->path, path, sizeof(frame->path)); frame->timestamp_ms = timestamp_ms;
  return ASCIICHAT_OK;
}

static char *http_renderer_render_image(const image_t *image, const http_render_request_t *request) {
  size_t width = request->options.width > 0 ? (size_t)request->options.width : 80;
  size_t height = request->options.height > 0 ? (size_t)request->options.height
                                               : (size_t)((double)width * image->h / image->w / 2.0);
  if (!height) height = 1;
  image_t *scaled = image_new(width, height);
  if (!scaled) return NULL;
  image_resize(image, scaled);
  const char *palette = request->options.palette_chars[0] ? request->options.palette_chars : NULL;
  if (!palette && request->options.palette[0]) {
    const struct { const char *name; palette_type_t type; } palettes[] = {
      {"standard", PALETTE_STANDARD}, {"blocks", PALETTE_BLOCKS}, {"digital", PALETTE_DIGITAL},
      {"minimal", PALETTE_MINIMAL}, {"cool", PALETTE_COOL}
    };
    for (size_t i = 0; i < sizeof(palettes) / sizeof(palettes[0]); ++i)
      if (!strcmp(request->options.palette, palettes[i].name)) { palette = get_builtin_palette(palettes[i].type)->chars; break; }
    if (!palette) palette = request->options.palette;
  }
  if (!palette) palette = DEFAULT_ASCII_PALETTE;
  char *ascii;
  if (!strcmp(request->options.render_theme, "braille")) ascii = NULL;
  else if (!strcmp(request->options.render_theme, "block") || !strcmp(request->options.render_theme, "half-block")) {
    ascii_color_t color = request->options.color_mode == TERM_COLOR_TRUECOLOR ? ASCII_COLOR_TRUE :
                          request->options.color_mode == TERM_COLOR_256 ? ASCII_COLOR_256 :
                          request->options.color_mode == TERM_COLOR_16 ? ASCII_COLOR_16 : ASCII_COLOR_NONE;
    ascii = ascii_render_halfblocks((const uint8_t *)scaled->pixels, scaled->w, scaled->h, 0, color, simd_cells_backend());
  } else if (request->options.color_mode == TERM_COLOR_TRUECOLOR)
    ascii = image_print_color_simd(scaled, false, false, palette);
  else if (request->options.color_mode == TERM_COLOR_256)
    ascii = ascii_render(scaled, palette, ASCII_COLOR_256, false, false, simd_cells_backend());
  else if (request->options.color_mode == TERM_COLOR_16)
    ascii = ascii_render(scaled, palette, ASCII_COLOR_16, false, false, simd_cells_backend());
  else ascii = image_print(scaled, palette);
  image_destroy(scaled);
  return ascii;
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
  size_t total = 0;
  size_t limit = request->max_output_bytes ? request->max_output_bytes : 8u * 1024u * 1024u;
  for (;;) {
    if (image->w > 8192 || image->h > 8192) { image_destroy(image); ffmpeg_decoder_destroy(decoder); return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Decoded image dimensions exceed the configured limit"); }
    char *ascii = http_renderer_render_image(image, request);
    image_destroy(image);
    if (!ascii) { ffmpeg_decoder_destroy(decoder); return SET_ERRNO(ERROR_MEDIA_INIT, "Cannot convert image to ASCII"); }
    size_t length = strlen(ascii);
    if (length > limit - (total > limit ? limit : total)) { free(ascii); ffmpeg_decoder_destroy(decoder); return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Rendered output exceeds the configured limit"); }
    if (http_render_result_add_text_frame(result, ascii, length, (time_get_ns() - started) / 1000000) != ASCIICHAT_OK) {
      free(ascii); ffmpeg_decoder_destroy(decoder); return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Cannot store rendered frame");
    }
    if (!result->ascii) { result->ascii = SAFE_MALLOC(length + 1, char *); SAFE_MEMCPY(result->ascii, length + 1, ascii, length + 1); result->ascii_length = length; }
    total += length;
    free(ascii);
    if (result->frame_count >= HTTP_RENDERER_MAX_FRAMES) break;
    if (cancelled && atomic_load_bool(cancelled)) { result->cancelled = true; ffmpeg_decoder_destroy(decoder); return SET_ERRNO(ERROR_CANCELLED, "HTTP image render cancelled"); }
    asciichat_error_context_t failure = {0};
    image = NULL;
    ffmpeg_frame_status_t status = ffmpeg_decoder_poll_video(decoder, &image, &failure);
    if (status == FFMPEG_FRAME_EOF) break;
    if (status == FFMPEG_FRAME_FAILED) { ffmpeg_decoder_destroy(decoder); return SET_ERRNO(ERROR_MEDIA_INIT, "Video frame decoding failed"); }
    while (status == FFMPEG_FRAME_WAITING) {
      if (time_get_ns() >= deadline) { ffmpeg_decoder_destroy(decoder); return SET_ERRNO(ERROR_TIMEOUT, "Image decoding timed out"); }
      platform_sleep_ms(1); status = ffmpeg_decoder_poll_video(decoder, &image, &failure);
    }
    if (!image) break;
  }
  ffmpeg_decoder_destroy(decoder);
  result->elapsed_ms = (time_get_ns() - started) / 1000000;
  return result->frame_count ? ASCIICHAT_OK : SET_ERRNO(ERROR_MEDIA_INIT, "Renderer returned no frames");
}
