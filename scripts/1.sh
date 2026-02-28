unset CLAUDECODE
pkill -9 -x ascii-chat || true
sleep 1
./build/bin/ascii-chat server --websocket-port 27226 :: 0.0.0.0 >/dev/null 2>/dev/null &
sleep 0.1
(sleep 6 && pkill -9 -x ascii-chat) &
sleep_pid=$!
timeout -k1 5 ./build/bin/ascii-chat client ws://localhost:27226 2>/dev/null
wait $sleep_pid || true
