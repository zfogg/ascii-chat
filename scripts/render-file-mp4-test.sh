#!/usr/bin/env bash

set -euo pipefail

delay=3.0

f=/tmp/z.mp4
rm -f "$f"
./build/bin/ascii-chat --color mirror --color-mode truecolor --matrix --render-file="$f" -S -D "$delay" 2>/dev/null \
    | tee file.log >/dev/null

ffprobe "$f" 2>&1 | grep -i duration
echo "Duration should be 3.0"
