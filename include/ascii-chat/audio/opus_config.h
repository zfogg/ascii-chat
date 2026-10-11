#pragma once
#include <ascii-chat/asciichat_errno.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Configure continuous VBR output consistently for native encoders. */
asciichat_error_t audio_opus_configure_encoder(void *encoder, int bitrate);
#ifdef __cplusplus
}
#endif
