#include <libavformat/avformat.h>
#include <stdio.h>

int main() {
    AVFormatContext *fmt_ctx = NULL;
    avformat_alloc_output_context2(&fmt_ctx, NULL, "mp4", "test.mp4");
    
    AVStream *stream = avformat_new_stream(fmt_ctx, NULL);
    
    printf("Default stream->time_base: %d/%d\n", stream->time_base.num, stream->time_base.den);
    
    AVCodecContext *codec_ctx = avcodec_alloc_context3(NULL);
    codec_ctx->time_base = (AVRational){1, 60};
    printf("Codec time_base: %d/%d\n", codec_ctx->time_base.num, codec_ctx->time_base.den);
    
    avcodec_parameters_from_context(stream->codecpar, codec_ctx);
    printf("After copy, stream->time_base: %d/%d\n", stream->time_base.num, stream->time_base.den);
    
    avformat_free_context(fmt_ctx);
    avcodec_free_context(&codec_ctx);
    return 0;
}
