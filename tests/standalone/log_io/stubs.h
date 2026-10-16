#pragma once
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define platform_fileno _fileno
#define platform_dup _dup
#define platform_close _close
#else
#include <unistd.h>
#define platform_fileno fileno
#define platform_dup dup
#define platform_close close
#endif
#define platform_tmpfile tmpfile
int capture_test_dup2(int from, int to);
#define ASCIICHAT_OK 0
#define platform_dup2(from, to) (capture_test_dup2(from, to) < 0 ? 1 : ASCIICHAT_OK)
void capture_test_log(const char *format, ...);
#define log_debug capture_test_log
