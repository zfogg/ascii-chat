#!/usr/bin/env zsh


cmake --build build --target man1 \
  && mandoc -Thtml build/share/man/man1/ascii-chat.1 > ../ascii-chat.com/public/ascii-chat-man.html
