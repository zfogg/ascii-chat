#!/bin/bash
set -e

pkill -9 ascii-chat || true
sleep 1
rm -f /tmp/server.log
cmake --build build --target ascii-chat
./build/bin/ascii-chat --log-file /tmp/server.log server --no-status-screen 2>&1 &
echo "Server started in background"
