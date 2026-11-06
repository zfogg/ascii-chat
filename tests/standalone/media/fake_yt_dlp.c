/* A deterministic yt-dlp stand-in for media integration tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--version") == 0) {
      puts("fixture");
      return 0;
    }
  }
  const char *trace = getenv("FAKE_YTDLP_TRACE");
  if (trace) {
    FILE *file = fopen(trace, "a");
    if (!file)
      return 2;
    for (int i = 1; i < argc; ++i)
      fprintf(file, "%s\n", argv[i]);
    fputs("--END--\n", file);
    fclose(file);
  }
  const char *mode = getenv("FAKE_YTDLP_MODE");
  if (mode && strcmp(mode, "error") == 0) {
    fputs("fixture extraction failed\n", stderr);
    return 1;
  }
  const char *video = getenv("FAKE_YTDLP_VIDEO");
  const char *audio = getenv("FAKE_YTDLP_AUDIO");
  if (!video)
    return 3;
  puts(video);
  if (mode && strcmp(mode, "split") == 0 && audio)
    puts(audio);
  return 0;
}
