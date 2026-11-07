/* A deterministic yt-dlp stand-in for media integration tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static void json_string(const char *s) {
  putchar('"');
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') putchar('\\');
    putchar(*s);
  }
  putchar('"');
}

static void json_format(const char *url) {
  fputs("{\"url\":", stdout);
  json_string(url);
  fputs(",\"http_headers\":{\"X-Test\":\"fixture\"},\"cookies\":", stdout);
  json_string(getenv("FAKE_YTDLP_COOKIE") ? getenv("FAKE_YTDLP_COOKIE") : "");
  putchar('}');
}

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
  if (mode && strcmp(mode, "hang") == 0) {
#ifdef _WIN32
    Sleep(30000);
#else
    sleep(30);
#endif
  }
  if (mode && strcmp(mode, "error") == 0) {
    fputs("fixture extraction failed\n", stderr);
    return 1;
  }
  const char *video = getenv("FAKE_YTDLP_VIDEO");
  const char *audio = getenv("FAKE_YTDLP_AUDIO");
  if (!video)
    return 3;
  if (mode && strcmp(mode, "reversed") == 0 && audio) {
    fputs("{\"requested_formats\":[", stdout);
    json_format(audio);
    putchar(',');
    json_format(video);
    puts("]}");
  } else if (mode && strcmp(mode, "split") == 0 && audio) {
    fputs("{\"requested_formats\":[", stdout);
    json_format(video);
    putchar(',');
    json_format(audio);
    puts("]}");
  } else {
    json_format(video);
    putchar('\n');
  }
  return 0;
}
