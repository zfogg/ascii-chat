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

  # Generate man(3) pages from Doxygen (man pages only, no HTML)
  echo "📚 Generating man(3) pages from Doxygen..."
  cmake --build "$REPO_ROOT/build_release" --target man3 2>&1 | grep -E "(Doxygen|manpage|error)" || true

  # Check if man3 directory exists and has files
  if [ -d "$REPO_ROOT/build_release/share/man/man3" ]; then
    # Convert all man(3) pages to HTML and create index
    mkdir -p public/man3

    # Convert each .3 file to HTML
    for manfile in "$REPO_ROOT/build_release/share/man/man3"/*.3; do
      if [ -f "$manfile" ]; then
        basename=$(basename "$manfile" .3)
        mandoc -Thtml "$manfile" > "public/man3/${basename}.html"
      fi
    done

    # Create man3 index JSON for search and listing
    node -e "
      const fs = require('fs');
      const path = require('path');
      const mandir = '$REPO_ROOT/build_release/share/man/man3';

      const pages = [];
      const files = fs.readdirSync(mandir).filter(f => f.endsWith('.3'));

      for (const file of files) {
        const filepath = path.join(mandir, file);
        const content = fs.readFileSync(filepath, 'utf-8');
        const name = file.replace('.3', '');

        // Extract NAME section (first line with brief description)
        const nameMatch = content.match(/^\.SH NAME\n(.+?)(?:\n\.SH|\$)/s);
        let title = name;
        if (nameMatch) {
          title = nameMatch[1].trim().split('\n')[0];
        }

        pages.push({
          name: name,
          title: title,
          file: name + '.html'
        });
      }

      pages.sort((a, b) => a.name.localeCompare(b.name));
      fs.writeFileSync('public/man3/index.json', JSON.stringify(pages, null, 2));
    "

    echo "📜 man(3) pages generated ($(ls public/man3/*.html 2>/dev/null | wc -l) pages)"
  else
    echo "⚠️  man(3) directory not found. Ensure Doxygen is installed."
  fi
fi

