#!/usr/bin/env bash

set -euo pipefail

pkill -f "ascii-chat.*server" && sleep 0.5 || true
cmake --build build && ./build/bin/ascii-chat server >/dev/null &
timeout -k1 8 ./build/bin/ascii-chat --log-level debug --log-file client.log --sync-state 3 client --matrix 2>/dev/null
echo $?; echo 137=deadlock 124=GOOD 1=idk
pkill -f "ascii-chat.*server" && sleep 0.5 || true
