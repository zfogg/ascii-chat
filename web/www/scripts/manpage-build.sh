#!/bin/bash


set -euo pipefail

cd  "$(dirname "$0")/.."

# Build ascii-chat and generate manpages
# Now we're in web/www so root is ../../
REPO_ROOT="$(cd ../.. && pwd)"
node_command=node
if command -v node.exe >/dev/null 2>&1; then node_command=node.exe; fi
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

  # Use the repository Oxfmt configuration for generated HTML.
  (
    cd "$REPO_ROOT"
    "$node_command" node_modules/vite-plus/bin/vp fmt \
      --config .oxfmtrc.json --ignore-path scripts/format-empty-ignore \
      web/www/public/ascii-chat-man1.html web/www/public/ascii-chat-man5.html
  )

  # Generate man(3) pages from Doxygen (man pages only, no HTML)
  echo "📚 Generating man(3) pages from Doxygen..."
  rm -f "$BUILD_DIR/.man3.done"
  cmake --build "$BUILD_DIR" --target man3

  # Check if man3 directory exists and has files
  if [ -d "$BUILD_DIR/share/man/man3" ]; then
    # Generate a complete replacement before changing the published pages.
    mkdir -p public/man3
    staging_dir=$(mktemp -d "$REPO_ROOT/web/www/public/.man3-stage.XXXXXXXX")
    trap 'rm -rf "$staging_dir"' EXIT
    for manfile in "$BUILD_DIR/share/man/man3"/*.3; do
      if [ -f "$manfile" ]; then
        basename=$(basename "$manfile" .3)
        htmlfile="$staging_dir/${basename}.html"
        mandoc -Thtml "$manfile" > "$htmlfile"
        perl -pi -e 's/<\/([a-zA-Z][a-zA-Z0-9]*)\s*\n\s*>/<\/\1>/g' "$htmlfile"
        perl -pi -e 's/[ \t]+$//' "$htmlfile"
      fi
    done

    # Format the staged man(3) pages before publishing them.
    (
      cd "$REPO_ROOT"
      "$node_command" node_modules/vite-plus/bin/vp fmt \
        --config .oxfmtrc.json --ignore-path scripts/format-empty-ignore \
        "$staging_dir"
    )

    BUILD_DIR="$BUILD_DIR" MAN3_HTML_DIR="$staging_dir" uv run --project ../../../tests/ascii_chat_tests --frozen python scripts/generate_man3.py

    # Replace only generated pages after conversion and indexing succeed.
    for old_page in public/man3/ascii-chat-*.html; do
      [ ! -f "$old_page" ] || rm -f -- "$old_page"
    done
    cp "$staging_dir"/*.html public/man3/
    cp "$staging_dir/pages.json" public/man3/pages.json
    echo "📜 man(3) pages generated ($(find "$staging_dir" -name '*.html' | wc -l) pages)"

  else
    echo "⚠️  man(3) directory not found. Ensure Doxygen is installed."
  fi
fi

