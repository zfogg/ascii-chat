/*
 * Deterministic yt-dlp stand-in for media integration tests. It exercises how
 * ascii-chat invokes the extractor, handles prompts and cancellation, and
 * consumes stream metadata. It does not run real yt-dlp or verify its output,
 * authentication behavior, or YouTube extraction end to end.
 *
 * From the repository root, with BUILD_TESTS enabled:
 *   cmake --build build --target ascii-chat test-yt-dlp-fixture test-extractor-audio
 *   ctest --test-dir build -R "^media-extractor$" --output-on-failure
 *   uv run --project tests/ascii_chat_tests python tests/integration/media_extractor.py `
 *     --binary build/bin/ascii-chat.exe --fixture build/test-fixtures/fake-yt-dlp.exe `
 *     --audio-probe build/bin/test-extractor-audio.exe MediaExtractor.test_combined_stream_is_used_for_both_decoders
 *
 * Run the fixture directly in PowerShell to inspect its combined-stream JSON:
 *   $env:FAKE_YTDLP_VIDEO = 'http://127.0.0.1/video.mp4'
 *   .\build\test-fixtures\fake-yt-dlp.exe --print json https://example.com/watch
 *   Remove-Item Env:FAKE_YTDLP_VIDEO
 *
 * Set FAKE_YTDLP_MODE=split and FAKE_YTDLP_AUDIO to a second URL to emit
 * separate video and audio streams. The fixture does not fetch either URL.
 */
#include <stdio.h>
#include <stdbool.h>
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
    if (*s == '"' || *s == '\\')
      putchar('\\');
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
#ifdef _WIN32
  bool delayed_child = argc == 2 && strcmp(argv[1], "--delayed-child") == 0;
  if (delayed_child)
    Sleep(300);
#endif
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--version") == 0) {
      if (getenv("FAKE_YTDLP_VERSION_FAIL"))
        return 1;
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
#ifdef _WIN32
  if (mode && strcmp(mode, "interactive") == 0) {
    char answer[64];
    fputs("fixture prompt: ", stderr);
    fflush(stderr);
    if (!fgets(answer, sizeof(answer), stdin) || strcmp(answer, "yes\n") != 0)
      return 6;
  }
#endif
#ifndef _WIN32
  if (mode && (strcmp(mode, "interactive") == 0 || strcmp(mode, "interactive-hang") == 0)) {
    char answer[64];
    const char *extractor_pid_path = getenv("FAKE_YTDLP_EXTRACTOR_PID");
    if (extractor_pid_path) {
      FILE *pid_file = fopen(extractor_pid_path, "w");
      if (!pid_file)
        return 9;
      fprintf(pid_file, "%ld\n", (long)getpid());
      fclose(pid_file);
    }
    fputs("fixture prompt: ", stderr);
    fflush(stderr);
    if (!fgets(answer, sizeof(answer), stdin) || strcmp(answer, "yes\n") != 0)
      return 6;
    if (strcmp(mode, "interactive-hang") == 0) {
      pid_t child = fork();
      if (child < 0)
        return 7;
      if (child == 0) {
        for (;;)
          pause();
      }
      const char *pid_path = getenv("FAKE_YTDLP_CHILD_PID");
      FILE *pid_file = pid_path ? fopen(pid_path, "w") : NULL;
      if (!pid_file)
        return 8;
      fprintf(pid_file, "%ld\n", (long)child);
      fclose(pid_file);
      for (;;)
        pause();
    }
  }
#endif
#ifdef _WIN32
  if (mode && strcmp(mode, "delayed") == 0 && !delayed_child) {
    char executable[MAX_PATH], command[MAX_PATH + 32];
    if (!GetModuleFileNameA(NULL, executable, sizeof(executable)))
      return 4;
    snprintf(command, sizeof(command), "\"%s\" --delayed-child", executable);
    STARTUPINFOA startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process))
      return 5;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
  }
#endif
  if (getenv("FAKE_YTDLP_DIAGNOSTIC"))
    fputs("fixture diagnostic on stderr\n", stderr);
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
    fputs("{\"base\":{},\"formats\":[", stdout);
    json_format(audio);
    putchar(',');
    json_format(video);
    puts("]}");
  } else if (mode && strcmp(mode, "split") == 0 && audio) {
    fputs("{\"base\":{},\"formats\":[", stdout);
    json_format(video);
    putchar(',');
    json_format(audio);
    puts("]}");
  } else {
    fputs("{\"base\":", stdout);
    json_format(video);
    puts(",\"formats\":null}");
  }
  return 0;
}
