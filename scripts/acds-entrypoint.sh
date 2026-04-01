#!/usr/bin/env bash

set -e

/usr/local/bin/ascii-chat
  --log-file /tmp/acds.log --log-level debug \
  discovery-service 0.0.0.0 :: --port 27225 --websocket-port 27227 \
  --database /data/acds.db \
  --key /acds/key --key /acds/key.gpg \
  --status-screen=false
