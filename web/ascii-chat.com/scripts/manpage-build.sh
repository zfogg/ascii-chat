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
import subprocess

man3_dir = Path("public/man3")
build_man3_dir = Path("../../build/share/man/man3")
repo_root = Path("../../").resolve()
pages = []

# Build filename->path lookup from git-tracked source files
source_lookup = {}
try:
    result = subprocess.run(
        ['git', 'ls-files'],
        capture_output=True, text=True, cwd=str(repo_root), timeout=10
    )
    for filepath in result.stdout.strip().splitlines():
        fname = Path(filepath).name
        if fname not in source_lookup:
            source_lookup[fname] = filepath
        # If duplicate, prefer src/ or lib/ paths
        elif filepath.startswith(('src/', 'lib/', 'include/')):
            source_lookup[fname] = filepath
except Exception:
    pass

# Get all HTML files except pages.json
for html_file in sorted(man3_dir.glob("*.html")):
    filename = html_file.name
    basename_no_ext = filename[:-5]  # Remove .html

    # Skip Doxygen internal documentation pages (file docs, source listings, etc)
    if re.search(r'_8[a-z](_source)?$', basename_no_ext):
        continue

    # Corresponding .3 file has same basename but with .3 extension
    man3_file = build_man3_dir / f"{basename_no_ext}.3"

    # Skip HTML files that don't have corresponding .3 man files
    # (ensures pages.json only includes pages from current Doxygen output)
    if not man3_file.exists():
        continue

    # Extract Doxygen name from .3 file
    name = None
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

    # Look up source path from filename
    source_path = None

    # If name already ends with .c, .h, or .cpp, use it directly
    if name.endswith(('.c', '.h', '.cpp')):
        if name in source_lookup:
            source_path = source_lookup[name]
    else:
        # Try to match the name.c or name.h
        for ext in ['.c', '.h', '.cpp']:
            candidate = name + ext
            if candidate in source_lookup:
                source_path = source_lookup[candidate]
                break

    # If not found by direct name, try to look up by Doxygen name pattern
    if not source_path and name.endswith('_8c'):
        base_name = name[:-3] + '.c'
        if base_name in source_lookup:
            source_path = source_lookup[base_name]

    # If still not found, try extracting filename from HTML's "Definition at line of file" section
    # This handles classes/structs defined in other files
    if not source_path:
        try:
            html_content = html_file.read_text(encoding='utf-8', errors='ignore')
            # Look for: Definition at line <b>XX</b> of file <b>filename</b>
            def_match = re.search(r'Definition at line <b>\d+</b> of file\s*<b>([^<]+)</b>', html_content)
            if def_match:
                def_filename = def_match.group(1)
                if def_filename in source_lookup:
                    source_path = source_lookup[def_filename]
        except Exception:
            pass

    pages.append({
        "name": name,
        "title": name,
        "file": filename,
        "sourcePath": source_path
    })

# Handle duplicate page names by using HTML filename to create unique names
name_counts = {}
for page in pages:
    name = page["name"]
    name_counts[name] = name_counts.get(name, 0) + 1

# For duplicates, use HTML filename (with ascii-chat- prefix removed)
for i, page in enumerate(pages):
    name = page["name"]
    if name_counts[name] > 1:
        # Extract HTML filename without prefix: "ascii-chat-debug_atomic.c.html" -> "debug_atomic.c"
        html_file = page["file"]
        if html_file.startswith("ascii-chat-") and html_file.endswith(".html"):
            # Remove "ascii-chat-" prefix and ".html" suffix
            unique_name = html_file[11:-5]  # Skip "ascii-chat-", remove ".html"
            page["name"] = unique_name

# Check for remaining duplicates (error if any exist)
name_counts_final = {}
for page in pages:
    name = page["name"]
    name_counts_final[name] = name_counts_final.get(name, 0) + 1

duplicates = {k: v for k, v in name_counts_final.items() if v > 1}
if duplicates:
    print(f"ERROR: Found {len(duplicates)} duplicate page names:")
    for name, count in sorted(duplicates.items()):
        matching = [p for p in pages if p["name"] == name]
        for p in matching:
            print(f"  {name}: {p['file']} -> {p.get('sourcePath', 'N/A')}")
    exit(1)

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

