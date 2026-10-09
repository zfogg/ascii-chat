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
#define ERROR_PLATFORM_INIT 3
#define ERROR_MEMORY 4
#define SET_ERRNO(code, ...) (code)
#define SET_ERRNO_SYS(code, ...) (code)
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
asciichat_error_t platform_process_title_set_args(const char *mode, int argc, char **argv, int mode_arg_index);
void platform_process_title_destroy(void);
"""

HARNESS = r"""
#include <ascii-chat/platform/process.h>
#ifdef __linux__
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif
char **g_argv;
int allocations;
int main(int argc, char **argv) {
  assert(argc >= 3);
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
  char native_name[64] = {0};
#ifdef __linux__
  assert(prctl(PR_GET_NAME, native_name, 0UL, 0UL, 0UL) == 0);
#elif defined(__APPLE__)
  assert(pthread_getname_np(pthread_self(), native_name, sizeof(native_name)) == 0);
#endif
  assert(strcmp(native_name, "temporary") == 0);
  assert(platform_process_title_set(title) == ASCIICHAT_OK);
  assert(strcmp(argv[1], mode) == 0);
  assert(strcmp(argv[2], "argument-preserved") == 0);
  assert(strcmp(getenv("TITLE_TEST_ORIGINAL"), "environment-preserved") == 0);
  assert(setenv("TITLE_TEST_CHANGED", "changed", 1) == 0);
  assert(setenv("TITLE_TEST_NEW", "new", 1) == 0);
  assert(unsetenv("TITLE_TEST_REMOVED") == 0);
  char *large_argument = calloc(65536, 1);
  assert(large_argument);
  memset(large_argument, 'x', 65535);
  char *large_argv[] = {argv[0], "--password", "do-not-display", large_argument};
  assert(platform_process_title_set_args(mode, 4, large_argv, -1) == ASCIICHAT_OK);
  free(large_argument);
  assert(platform_process_title_set_args(mode, argc, argv, 1) == ASCIICHAT_OK);
  assert(strcmp(argv[6], "split-secret") == 0);
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
        for name in ("platform/process.h", "platform/terminal.h", "common.h", "asciichat_errno.h"):
            (work / "ascii-chat" / name).write_text('#include "support.h"\n')
        wasm = "--wasm" in sys.argv
        noop_harness = r'''
#include <ascii-chat/platform/process.h>
int allocations;
int main(int argc, char **argv) {
  char **original = argv;
  assert(platform_process_title_init(argc, &argv) == ASCIICHAT_OK);
  assert(argv == original);
  assert(platform_process_title_set("ascii-chat: mirror mode") == ASCIICHAT_OK);
  assert(platform_process_title_set_args("mirror", argc, argv, -1) == ASCIICHAT_OK);
  assert(platform_process_title_set(NULL) == ERROR_INVALID_PARAM);
  platform_process_title_destroy();
  puts("PASS: WASM no-op backend");
  return 0;
}
'''
        (work / "main.c").write_text(noop_harness if wasm else HARNESS)
        executable = work / ("title-test.js" if wasm else "title-test")
        native_sources = []
        if not wasm:
            native_os = "macos" if sys.platform == "darwin" else "linux"
            native_sources = [str(ROOT / f"lib/platform/{native_os}/process_title.c")]
            if sys.platform == "darwin":
                native_sources += ["-framework", "CoreFoundation"]
        subprocess.run(
            shlex.split(os.environ.get("CC", "clang"))
            + ["-std=gnu11", "-Wall", "-Wextra", "-Werror"]
            + shlex.split(os.environ.get("CFLAGS", ""))
            + ["-I", str(work), str(ROOT / "lib/platform/process_title.c"),
               str(work / "main.c"), "-o", str(executable)] + native_sources,
            check=True,
        )
        if wasm:
            subprocess.run(["node", str(executable)], check=True, timeout=10)
            return
        name_probe = work / "name-probe"
        if sys.platform == "darwin":
            # Read the public application name from a separate process, rather
            # than merely trusting the private Launch Services setter's status.
            (work / "name-probe.m").write_text(r'''
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  @autoreleasepool {
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:atoi(argv[1])];
    if (!app || !app.localizedName) return 2;
    if (app.activationPolicy == NSApplicationActivationPolicyRegular) return 3;
    puts(app.localizedName.UTF8String);
  }
  return 0;
}
''')
            subprocess.run(shlex.split(os.environ.get("CC", "clang"))
                           + shlex.split(os.environ.get("CFLAGS", ""))
                           + [str(work / "name-probe.m"), "-framework", "AppKit", "-o", str(name_probe)],
                           check=True)
        environment = dict(os.environ, TITLE_TEST_ORIGINAL="environment-preserved",
                           TITLE_TEST_CHANGED="original", TITLE_TEST_REMOVED="remove")
        arguments = ["argument-preserved", "--port", "27224", "--password", "split-secret",
                     "--password=inline-secret", "--websocket-tls-key-password", "tls-secret",
                     "--turn-credential=turn-secret", "--turn-secret", "shared-secret",
                     "--key", "/keys/id_ed25519", "--key", "-----BEGIN OPENSSH PRIVATE KEY-----",
                     "two words", "", "line\nbreak"]
        expected_args = ('argument-preserved --port 27224 --password [redacted] '
                         '--password=[redacted] --websocket-tls-key-password [redacted] '
                         '--turn-credential=[redacted] --turn-secret [redacted] '
                         '--key /keys/id_ed25519 --key [redacted] "two words" "" "line\\x0abreak"')
        for mode in ("server", "client", "mirror", "discovery-service", "discovery"):
            with subprocess.Popen([str(executable), mode] + arguments,
                                  env=environment, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True) as child:
                try:
                    ready = child.stdout.readline()
                    assert ready == "ready\n", (ready, child.stderr.read())
                    observed = subprocess.check_output(
                        ["ps", "-ww", "-p", str(child.pid), "-o", "args="], text=True
                    ).strip()
                    assert observed == f"ascii-chat: {mode} mode - {expected_args}", repr(observed)
                    if sys.platform.startswith("linux"):
                        expected_comm = "ascii:" + ("acds" if mode == "discovery-service" else mode)
                        assert Path(f"/proc/{child.pid}/comm").read_text().strip() == expected_comm
                        comm = subprocess.check_output(
                            ["ps", "-p", str(child.pid), "-o", "comm="], text=True).strip()
                        assert comm == expected_comm, comm
                    elif sys.platform == "darwin":
                        display_name = subprocess.check_output([str(name_probe), str(child.pid)], text=True).strip()
                        assert display_name == observed, repr(display_name)
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
