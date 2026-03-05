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
  # Use build directory for man page generation (build_release uses musl and may not be available)
  BUILD_DIR="$REPO_ROOT/build"
  if [ ! -d "$BUILD_DIR" ]; then
    BUILD_DIR="$REPO_ROOT/build_release"
  fi

  cmake --build "$BUILD_DIR" --target man1 man5 || true

  # Generate man(1) page from troff format and convert to HTML
  #$BUILD_DIR/bin/ascii-chat --man-page-create | mandoc -Thtml > public/ascii-chat-man1.html
  mandoc -Thtml "$BUILD_DIR/share/man/man1/ascii-chat.1" > public/ascii-chat-man1.html || true
  echo "📜 man(1) page updated"

  # Generate man(5) page from built manpage and convert to HTML
  mandoc -Thtml "$BUILD_DIR/share/man/man5/ascii-chat.5" > public/ascii-chat-man5.html || true
  echo "📜 man(5) page updated"

  # Generate man(3) pages from Doxygen (man pages only, no HTML)
  echo "📚 Generating man(3) pages from Doxygen..."
  cmake --build "$BUILD_DIR" --target man3 2>&1 | grep -E "(Doxygen|manpage|error)" || true

  echo "📚 Generating man(3) pages from Doxygen..."
  cmake --build "$BUILD_DIR" --target man3 2>&1 | grep -E "(Doxygen|manpage|error)" || true

  # Check if man3 directory exists and has files
  if [ -d "$BUILD_DIR/share/man/man3" ]; then
    # Convert all man(3) pages to HTML and create index
    mkdir -p public/man3

    # Convert each .3 file to HTML
    for manfile in "$BUILD_DIR/share/man/man3"/*.3; do
      if [ -f "$manfile" ]; then
        basename=$(basename "$manfile" .3)
        mandoc -Thtml "$manfile" > "public/man3/${basename}.html"
      fi
    done

    # Generate pages.json index from converted HTML files
    python3 << 'PYSCRIPT'
import json
from pathlib import Path
import re

man3_dir = Path("public/man3")
build_man3_dir = Path("../../build/share/man/man3")
pages = []

# Get all HTML files except pages.json
for html_file in sorted(man3_dir.glob("*.html")):
    filename = html_file.name
    basename_no_ext = filename[:-5]  # Remove .html

    # Corresponding .3 file has same basename but with .3 extension
    man3_file = build_man3_dir / f"{basename_no_ext}.3"

    # Extract Doxygen name from .3 file
    name = None
    if man3_file.exists():
        try:
            content = man3_file.read_text(encoding='utf-8', errors='ignore')
            # Extract from .TH line: .TH "name" 3 ...
            th_match = re.search(r'\.TH\s+"([^"]+)"', content)
            if th_match:
                name = th_match.group(1).strip()
        except Exception:
            pass

    # Fallback: use basename with ascii-chat- prefix removed
    if not name:
        name = basename_no_ext
        if name.startswith("ascii-chat-"):
            name = name[11:]  # Remove "ascii-chat-" prefix

    pages.append({
        "name": name,
        "title": name,
        "file": filename
    })

# Write pages.json
with open("public/man3/pages.json", "w") as f:
    json.dump(pages, f, indent=2)

print(f"Generated pages.json with {len(pages)} entries")
PYSCRIPT

    echo "📜 man(3) pages generated ($(ls public/man3/*.html 2>/dev/null | wc -l) pages)"
  else
    echo "⚠️  man(3) directory not found. Ensure Doxygen is installed."
  fi
fi

