#!/usr/bin/env bash

set -euo pipefail

pkill -f "ascii-chat.*client" || true
pkill -f "ascii-chat.*server" || true
sleep 0.2

timeout -k 1 6 ./build/bin/ascii-chat --log-level debug --sync-state 1 server &
SERVER_PID=$!

timeout -k 1 4 ./build/bin/ascii-chat --log-level debug --sync-state 1 client || true
CLIENT_PID=$!

kill $SERVER_PID
kill $CLIENT_PID
wait $SERVER_PID $CLIENT_PID

echo "the client either printed a frame or two and deadlocked or printed no ascii art"
echo "now grep the logs"
