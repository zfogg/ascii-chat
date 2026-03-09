#!/bin/bash


set -euo pipefail

cd  "$(dirname "$0")/.."

# Build ascii-chat and generate manpages
# Now we're in web/www so root is ../../
REPO_ROOT="$(cd ../.. && pwd)"
if [ -d "$REPO_ROOT" ]; then
  man1_just_edited=$(perl -e 'exit(time - (stat($ARGV[0]))[9] <= 2 ? 0 : 1)' "$REPO_ROOT/share/man/man1/ascii-chat.1.in"; echo $?)
  man5_just_edited=$(perl -e 'exit(time - (stat($ARGV[0]))[9] <= 2 ? 0 : 1)' "$REPO_ROOT/share/man/man5/ascii-chat.5.in"; echo $?)
  if [ "$man1_just_edited" -eq "0" ] || [ "$man5_just_edited" -eq "0" ]; then
    echo "TOUCHING options.c"
    touch "$REPO_ROOT/lib/options/options.c"
  fi

  BUILD_DIR="$REPO_ROOT/build_release"
  if [ ! -d "$BUILD_DIR" ]; then
    BUILD_DIR="$REPO_ROOT/build"
  fi

  cmake --build "$BUILD_DIR" --target man1 man5

  # Generate man(1) page from troff format and convert to HTML
  #$BUILD_DIR/bin/ascii-chat --man-page-create | mandoc -Thtml > public/ascii-chat-man1.html
  mandoc -Thtml "$BUILD_DIR/share/man/man1/ascii-chat.1" > public/ascii-chat-man1.html
  echo "📜 man(1) page updated"

  # Generate man(5) page from built manpage and convert to HTML
  mandoc -Thtml "$BUILD_DIR/share/man/man5/ascii-chat.5" > public/ascii-chat-man5.html
  echo "📜 man(5) page updated"

  # Generate man(3) pages from Doxygen (man pages only, no HTML)
  echo "📚 Generating man(3) pages from Doxygen..."
  # Force doxygen to run directly to pick up documentation changes in header files
  # (cmake build system skips if source code hasn't changed, even if comments did)
  if [ -f "$BUILD_DIR/Doxyfile.man3" ]; then
    doxygen "$BUILD_DIR/Doxyfile.man3" > /dev/null 2>&1
  else
    cmake --build "$BUILD_DIR" --target man3
  fi

  # Check if man3 directory exists and has files
  if [ -d "$BUILD_DIR/share/man/man3" ]; then
    # Convert man3 pages to HTML with mandoc
    mkdir -p public/man3
    for manfile in "$BUILD_DIR/share/man/man3"/*.3; do
      if [ -f "$manfile" ]; then
        basename=$(basename "$manfile" .3)
        mandoc -Thtml "$manfile" > "public/man3/${basename}.html"
      fi
    done

    # Generate pages.json index from converted HTML files
    python3 scripts/generate_man3.py
    if [ $? -ne 0 ]; then
      echo "Failed to generate pages.json"
      exit 1
    fi

    echo "📜 man(3) pages generated ($(ls public/man3/*.html 2>/dev/null | wc -l) pages)"
  else
    echo "⚠️  man(3) directory not found. Ensure Doxygen is installed."
  fi
fi

