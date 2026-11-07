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
