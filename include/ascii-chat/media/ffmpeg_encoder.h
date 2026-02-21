/**
 * @file media/ffmpeg_encoder.h
 * @ingroup media
 * @brief FFmpeg video/image file encoder — codec selected from file extension
 */
#pragma once
#ifndef _WIN32
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct ffmpeg_encoder_s ffmpeg_encoder_t;

// Extension → codec mapping:
//   .mp4 / .mov  →  H.264   + YUV420P
//   .webm        →  VP9     + YUV420P
//   .avi         →  MPEG-4  + YUV420P
//   .gif         →  GIF     + PAL8
//   .png         →  PNG     + RGB24   (single frame / snapshot)
//   .jpg / .jpeg →  MJPEG   + YUVJ420P (single frame)
asciichat_error_t ffmpeg_encoder_create(const char *output_path,
                                        int width_px, int height_px,
                                        int fps, ffmpeg_encoder_t **out);

asciichat_error_t ffmpeg_encoder_write_frame(ffmpeg_encoder_t *enc,
                                             const uint8_t *rgb, int pitch);

asciichat_error_t ffmpeg_encoder_destroy(ffmpeg_encoder_t *enc);
#endif
