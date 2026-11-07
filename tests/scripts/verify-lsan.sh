#!/bin/sh
# Verify the sanitizer can detect leaks inside this container before running tests.
set -eu
probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/ascii-chat-lsan.XXXXXX")
trap 'rm -rf "$probe_dir"' EXIT HUP INT TERM
cat > "$probe_dir/probe.c" <<'EOF'
#include <stdlib.h>
int main(void) {
  volatile unsigned char *allocation = malloc(1234);
  if (!allocation)
    return 2;
  allocation[0] = 1;
  return 0;
}
EOF
"${CC:-clang}" -O0 -g -fsanitize=address -fno-omit-frame-pointer "$probe_dir/probe.c" -o "$probe_dir/probe"
status=0
ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=detect_leaks=1:exitcode=23 "$probe_dir/probe" > "$probe_dir/report" 2>&1 || status=$?
if [ "$status" -ne 23 ] || ! grep -q 'LeakSanitizer: detected memory leaks' "$probe_dir/report" ||
   ! grep -q '1234 byte(s)' "$probe_dir/report"; then
  cat "$probe_dir/report" >&2
  echo "LeakSanitizer positive control failed (exit $status)" >&2
  exit 1
fi
echo "LeakSanitizer positive control passed"

# Criterion may return success even when a worker reports a leak. Check that
# the same CTest policy used by the real suites catches that report.
cat > "$probe_dir/criterion.c" <<'EOF'
#include <criterion/criterion.h>
#include <stdlib.h>
Test(lsan_probe, leaks) {
  volatile unsigned char *allocation = malloc(1234);
  if (allocation) allocation[0] = 1;
}
EOF
"${CC:-clang}" -O0 -g -fsanitize=address -fno-omit-frame-pointer "$probe_dir/criterion.c" -lcriterion -o "$probe_dir/criterion-probe"
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../cmake/test" && pwd)
cat > "$probe_dir/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(lsan_probe NONE)
enable_testing()
include("$module_dir/LeakSanitizer.cmake")
add_test(criterion-lsan "$probe_dir/criterion-probe" --jobs 1)
asciichat_fail_on_lsan_report(criterion-lsan)
EOF
cmake -S "$probe_dir" -B "$probe_dir/build" > /dev/null
status=0
ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=detect_leaks=1:exitcode=23 ctest --test-dir "$probe_dir/build" --output-on-failure > "$probe_dir/ctest-report" 2>&1 || status=$?
if [ "$status" -eq 0 ] || ! grep -q 'LeakSanitizer: detected memory leaks' "$probe_dir/ctest-report"; then
  cat "$probe_dir/ctest-report" >&2
  echo "Criterion LeakSanitizer positive control failed (exit $status)" >&2
  exit 1
fi
echo "Criterion LeakSanitizer positive control passed"
