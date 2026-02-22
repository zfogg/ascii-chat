#!/usr/bin/env bash

set -euo pipefail

timeout -k 1 3 ./build/bin/ascii-chat mirror --test-pattern \
  --render-file /tmp/f.png \
  -S -D 0 \
  -x 500 -y 500 >/dev/null

