#!/bin/bash


set -euo pipefail

cd  "$(dirname "$0")/.."

# Build ascii-chat and generate manpages
# Now we're in web/ascii-chat.com, so root is ../../
REPO_ROOT="$(cd ../.. && pwd)"
if [ -d "$REPO_ROOT" ]; then
  man1_just_edited=$(perl -e 'exit(time - (stat($ARGV[0]))[9] <= 2 ? 0 : 1)' "$REPO_ROOT/share/man/man1/ascii-chat.1.in"; echo $?)
  man5_just_edited=$(perl -e 'exit(time - (stat($ARGV[0]))[9] <= 2 ? 0 : 1)' "$REPO_ROOT/share/man/man5/ascii-chat.5.in"; echo $?)
  if [ "$man1_just_edited" -eq "0" ] || [ "$man5_just_edited" -eq "0" ]; then
    echo "TOUCHING options.c"
    touch "$REPO_ROOT/lib/options/options.c"
  fi
  cmake --build "$REPO_ROOT/build_release" --target man1 man5

  # Generate man(1) page from troff format and convert to HTML
  #$REPO_ROOT/build_release/bin/ascii-chat --man-page-create | mandoc -Thtml > public/ascii-chat-man1.html
  mandoc -Thtml "$REPO_ROOT/build_release/share/man/man1/ascii-chat.1" > public/ascii-chat-man1.html
  echo "📜 man(1) page updated"

  # Generate man(5) page from built manpage and convert to HTML
  mandoc -Thtml "$REPO_ROOT/build_release/share/man/man5/ascii-chat.5" > public/ascii-chat-man5.html
  echo "📜 man(5) page updated"
fi

