"""Compile the production audio finalization path against a backpressured codec.

Run with Python and Clang; no Criterion or FFmpeg installation is needed.
An optional source path allows verifying the regression against an older revision.
"""

from pathlib import Path
import subprocess
import sys
import tempfile


source = Path(sys.argv[1] if len(sys.argv) > 1 else "lib/media/ffmpeg_encoder.c").read_text()
helper_start = source.find("static asciichat_error_t finish_audio_frame(")
helper = source[helper_start:source.index("asciichat_error_t ffmpeg_encoder_destroy(")] if helper_start >= 0 else ""
start = source.index("  // Flush audio encoder if present", source.index("asciichat_error_t ffmpeg_encoder_destroy("))
block = source[start:source.index("  // Set stream duration", start)]

fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#define AVERROR(e) (-(e))
#define AVERROR_EOF (-1000)
#define ASCIICHAT_OK 0
#define ERROR_MEDIA_INIT 1
#define ERROR_FILE_OPERATION 2
#define SET_ERRNO(code, ...) (code)
static int log_depth;
#define LOG_IO(name, ...) do { ++log_depth; __VA_ARGS__ --log_depth; } while (0)
typedef int asciichat_error_t;
typedef struct { int pending, draining, pad_calls, flush_calls, writes, unrefs;
                 int reject_flush, fail_write, fail_send, fail_receive; int time_base; } AVCodecContext;
typedef struct { unsigned char *data[1]; int linesize[1], nb_samples, pts; } AVFrame;
typedef struct { int time_base, index; } AVStream;
typedef struct { int stream_index; } AVPacket;
typedef struct { AVCodecContext *audio_codec_ctx, *fmt_ctx; AVFrame *audio_frame;
                 AVStream *audio_stream; AVPacket *pkt; float *audio_partial_buf;
                 int has_audio_stream, audio_partial_len, audio_frame_size, audio_pts; } ffmpeg_encoder_t;
static AVCodecContext *active;
static int avcodec_send_frame(AVCodecContext *ctx, const AVFrame *frame) {
  if (ctx->fail_send) return -2000;
  if (frame) ++ctx->pad_calls; else ++ctx->flush_calls;
  assert(ctx->pad_calls + ctx->flush_calls < 10);
  if (!frame && ctx->reject_flush) { ctx->reject_flush = 0; ++ctx->pending; }
  if (ctx->pending) return AVERROR(EAGAIN);
  if (frame) {
    assert(frame->nb_samples == 4 && frame->pts == 12);
    const float *samples = (const float *)frame->data[0];
    assert(samples[0] == 0.5f && samples[1] == 0 && samples[2] == 0 && samples[3] == 0);
    ctx->pending = 2;
  } else { ctx->draining = 1; ctx->pending = 1; }
  return 0;
}
static int avcodec_receive_packet(AVCodecContext *ctx, AVPacket *pkt) {
  (void)pkt;
  if (ctx->fail_receive) return -2000;
  if (ctx->pending) { --ctx->pending; return 0; }
  return ctx->draining ? AVERROR_EOF : AVERROR(EAGAIN);
}
static void av_packet_rescale_ts(AVPacket *pkt, int from, int to) { (void)pkt; (void)from; (void)to; }
static int av_interleaved_write_frame(AVCodecContext *ctx, AVPacket *pkt) {
  assert(pkt->stream_index == 7); ++ctx->writes;
  return ctx->fail_write ? -1 : 0;
}
static void av_packet_unref(AVPacket *pkt) { (void)pkt; ++active->unrefs; }
'''
main = r'''
int main(void) {
  for (int scenario = 0; scenario < 6; ++scenario) {
    AVCodecContext ctx = {0};
    ctx.pending = scenario == 1;
    ctx.reject_flush = scenario == 2;
    ctx.fail_write = scenario == 3;
    ctx.fail_send = scenario == 4;
    ctx.fail_receive = scenario == 5;
    active = &ctx;
    AVFrame frame = {0}; AVPacket packet = {0}; AVStream stream = {1, 7};
    float samples[4] = {0.5f, 1, 1, 1};
    ffmpeg_encoder_t enc = {&ctx, &ctx, &frame, &stream, &packet, samples, 1, 1, 4, 12};
    int result = finalize(&enc);
    assert(log_depth == 0);
    if (scenario >= 4) { assert(result == ERROR_MEDIA_INIT); continue; }
    assert(result == (scenario == 3 ? ERROR_FILE_OPERATION : ASCIICHAT_OK));
    assert(ctx.draining && ctx.pending == 0);
    assert(ctx.pad_calls == (scenario == 1 ? 2 : 1));
    assert(ctx.flush_calls == (scenario == 2 ? 2 : 1));
    assert(ctx.writes == (scenario == 1 || scenario == 2 ? 4 : 3));
    assert(ctx.unrefs == ctx.writes);
  }
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="audio-backpressure-") as directory:
    root = Path(directory)
    test = root / "test.c"
    test.write_text(fixture + helper + "\nstatic asciichat_error_t finalize(ffmpeg_encoder_t *enc) {\n"
                    "asciichat_error_t result = ASCIICHAT_OK;\n" + block + "return result;\n}\n" + main)
    binary = root / "test.exe"
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
print("PASS: padded frame drain, padded send retry, flush retry, and mux error propagation")
