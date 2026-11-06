/* Verify that a resolved media source supplies real audio samples. */
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/options/rcu.h>

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  log_init(NULL, LOG_ERROR, false, false);
  if (named_init() != ASCIICHAT_OK || options_state_init() != ASCIICHAT_OK)
    return 2;
  options_t options = options_t_new();
  if (options_state_set(&options) != ASCIICHAT_OK)
    return 3;
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, argv[1]);
  if (!source || !media_source_has_video(source) || !media_source_has_audio(source))
    return 4;
  float samples[1024] = {0};
  size_t count = media_source_read_audio(source, samples, ARRAY_SIZE(samples));
  bool nonzero = false;
  for (size_t i = 0; i < count; ++i)
    nonzero |= samples[i] != 0.0f;
  media_source_destroy(source);
  options_state_destroy();
  return count > 0 && nonzero ? 0 : 5;
}
