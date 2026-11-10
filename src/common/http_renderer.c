#include "http_renderer.h"

#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static void *http_renderer_job_main(void *argument) {
  http_renderer_job_t *job = argument;
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
                                            http_render_result_t *result) {
  if (!path || !request || !result)
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTP render requires a source path, request, and result");
  memset(result, 0, sizeof(*result));
  uint64_t started = time_get_ns();
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  if (!decoder)
    return SET_ERRNO(ERROR_MEDIA_INIT, "Cannot decode uploaded image");
  image_t *image = ffmpeg_decoder_read_video_frame(decoder);
  if (!image) {
    ffmpeg_decoder_destroy(decoder);
    return SET_ERRNO(ERROR_MEDIA_INIT, "Uploaded data does not contain a decodable image");
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
  char *ascii = image_print(scaled, request->options.palette[0] ? request->options.palette : NULL);
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
