#!/usr/bin/env python3
"""Compile the production title backend with isolated error/allocation shims.

Run on Linux/macOS with Python 3 and Clang. Set CC/CFLAGS to test static musl
or sanitizers. Verifies real ps output, not just the backend's return value.
"""

import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r"""
#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef int asciichat_error_t;
#define ASCIICHAT_OK 0
#define ERROR_INVALID_PARAM 1
#define ERROR_INVALID_STATE 2
#define SET_ERRNO(code, ...) (code)
extern char **g_argv;
extern int allocations;
static inline void *test_alloc(size_t count, size_t size) {
  void *p = calloc(count, size);
  assert(p);
  ++allocations;
  return p;
}
#define SAFE_MALLOC(size, cast) ((cast)test_alloc(1, size))
#define SAFE_CALLOC(count, size, cast) ((cast)test_alloc(count, size))
#define SAFE_FREE(p) do { if (p) { free(p); --allocations; (p) = NULL; } } while (0)
asciichat_error_t platform_process_title_init(int argc, char ***argv);
asciichat_error_t platform_process_title_set(const char *title);
void platform_process_title_destroy(void);
"""

HARNESS = r"""
#include <ascii-chat/platform/process.h>
char **g_argv;
int allocations;
int main(int argc, char **argv) {
  assert(argc == 3);
  char **original = argv;
  char mode[64], title[128];
  snprintf(mode, sizeof(mode), "%s", argv[1]);
  snprintf(title, sizeof(title), "ascii-chat: %s mode", mode);
  assert(platform_process_title_set(title) == ERROR_INVALID_STATE);
  assert(platform_process_title_init(0, &argv) == ERROR_INVALID_PARAM);
  assert(platform_process_title_init(argc, &argv) == ASCIICHAT_OK);
  assert(argv != original);
  g_argv = argv;
  assert(platform_process_title_init(argc, &argv) == ERROR_INVALID_STATE);
  assert(platform_process_title_set(NULL) == ERROR_INVALID_PARAM);
  char *too_long = calloc(1024 * 1024, 1);
  assert(too_long);
  memset(too_long, 'x', 1024 * 1024 - 1);
  assert(platform_process_title_set(too_long) == ERROR_INVALID_PARAM);
  free(too_long);
  assert(platform_process_title_set("temporary") == ASCIICHAT_OK);
  assert(platform_process_title_set(title) == ASCIICHAT_OK);
  assert(strcmp(argv[1], mode) == 0);
  assert(strcmp(argv[2], "argument-preserved") == 0);
  assert(strcmp(getenv("TITLE_TEST_ORIGINAL"), "environment-preserved") == 0);
  assert(setenv("TITLE_TEST_CHANGED", "changed", 1) == 0);
  assert(setenv("TITLE_TEST_NEW", "new", 1) == 0);
  assert(unsetenv("TITLE_TEST_REMOVED") == 0);
  puts("ready");
  fflush(stdout);
  assert(getchar() == '\n');
  platform_process_title_destroy();
  platform_process_title_destroy();
  assert(allocations == 0);
  assert(g_argv == original);
  assert(strcmp(original[1], mode) == 0);
  assert(strcmp(original[2], "argument-preserved") == 0);
  assert(strcmp(getenv("TITLE_TEST_ORIGINAL"), "environment-preserved") == 0);
  assert(strcmp(getenv("TITLE_TEST_CHANGED"), "changed") == 0);
  assert(strcmp(getenv("TITLE_TEST_NEW"), "new") == 0);
  assert(getenv("TITLE_TEST_REMOVED") == NULL);
  assert(platform_process_title_set(title) == ERROR_INVALID_STATE);
  // A second lifecycle must also work after libc has replaced its env vector.
  argv = original;
  assert(platform_process_title_init(argc, &argv) == ASCIICHAT_OK);
  g_argv = argv;
  assert(platform_process_title_set(title) == ASCIICHAT_OK);
  platform_process_title_destroy();
  assert(allocations == 0);
  puts("passed");
  return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory(prefix="ascii-chat-title-") as temporary:
        work = Path(temporary)
        headers = work / "ascii-chat" / "platform"
        headers.mkdir(parents=True)
        (work / "support.h").write_text(SUPPORT)
        for name in ("platform/process.h", "platform/terminal.h", "common.h"):
            (work / "ascii-chat" / name).write_text('#include "support.h"\n')
        wasm = "--wasm" in sys.argv
        noop_harness = r'''
#include <ascii-chat/platform/process.h>
int main(int argc, char **argv) {
  char **original = argv;
  assert(platform_process_title_init(argc, &argv) == ASCIICHAT_OK);
  assert(argv == original);
  assert(platform_process_title_set("ascii-chat: mirror mode") == ASCIICHAT_OK);
  assert(platform_process_title_set(NULL) == ERROR_INVALID_PARAM);
  platform_process_title_destroy();
  puts("PASS: WASM no-op backend");
  return 0;
}
'''
        (work / "main.c").write_text(noop_harness if wasm else HARNESS)
        executable = work / ("title-test.js" if wasm else "title-test")
        subprocess.run(
            shlex.split(os.environ.get("CC", "clang"))
            + ["-std=gnu11", "-Wall", "-Wextra", "-Werror"]
            + shlex.split(os.environ.get("CFLAGS", ""))
            + ["-I", str(work), str(ROOT / "lib/platform/process_title.c"),
               str(work / "main.c"), "-o", str(executable)],
            check=True,
        )
        if wasm:
            subprocess.run(["node", str(executable)], check=True, timeout=10)
            return
        environment = dict(os.environ, TITLE_TEST_ORIGINAL="environment-preserved",
                           TITLE_TEST_CHANGED="original", TITLE_TEST_REMOVED="remove")
        for mode in ("server", "client", "mirror", "discovery-service", "discovery"):
            with subprocess.Popen([str(executable), mode, "argument-preserved"],
                                  env=environment, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True) as child:
                try:
                    ready = child.stdout.readline()
                    assert ready == "ready\n", (ready, child.stderr.read())
                    observed = subprocess.check_output(
                        ["ps", "-ww", "-p", str(child.pid), "-o", "args="], text=True
                    ).strip()
                    assert observed == f"ascii-chat: {mode} mode", repr(observed)
                    stdout, stderr = child.communicate("\n", timeout=10)
                    assert child.returncode == 0, (child.returncode, stderr)
                    assert stdout == "passed\n", repr(stdout)
                    assert stderr == "", stderr
                    print(f"PASS: {observed}; argv/env preserved; allocations freed")
                finally:
                    if child.poll() is None:
                        child.kill()


if __name__ == "__main__":
    main()
