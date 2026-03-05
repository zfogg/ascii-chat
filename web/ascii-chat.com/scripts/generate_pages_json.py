#!/usr/bin/env python3
"""Generate pages.json index from Doxygen man3 HTML files."""

import json
import sys
from pathlib import Path
import re
import subprocess


def build_source_lookup(repo_root):
    """Build filename->path lookup from git-tracked source files."""
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
    return source_lookup


def extract_doxygen_name(man3_file):
    """Extract Doxygen name from .3 file's .TH line."""
    try:
        content = man3_file.read_text(encoding='utf-8', errors='ignore')
        # Extract from .TH line: .TH "name" 3 ...
        th_match = re.search(r'\.TH\s+"([^"]+)"', content)
        if th_match:
            return th_match.group(1).strip()
    except Exception:
        pass
    return None


def get_fallback_name(basename_no_ext):
    """Get fallback name from HTML filename."""
    name = basename_no_ext
    if name.startswith("ascii-chat-"):
        name = name[11:]  # Remove "ascii-chat-" prefix
    return name


def lookup_source_path(name, source_lookup, html_file):
    """Look up source path from filename or HTML definition."""
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
            def_match = re.search(
                r'Definition at line <b>\d+</b> of file\s*<b>([^<]+)</b>', html_content
            )
            if def_match:
                def_filename = def_match.group(1)
                if def_filename in source_lookup:
                    source_path = source_lookup[def_filename]
        except Exception:
            pass

    return source_path


def deduplicate_names(pages):
    """Handle duplicate page names by using HTML filename."""
    name_counts = {}
    for page in pages:
        name = page["name"]
        name_counts[name] = name_counts.get(name, 0) + 1

    # For duplicates, use HTML filename (with ascii-chat- prefix removed)
    for page in pages:
        name = page["name"]
        if name_counts[name] > 1:
            html_file = page["file"]
            if html_file.startswith("ascii-chat-") and html_file.endswith(".html"):
                # Remove "ascii-chat-" prefix and ".html" suffix
                unique_name = html_file[11:-5]  # Skip "ascii-chat-", remove ".html"
                page["name"] = unique_name


def check_duplicates(pages):
    """Check for remaining duplicates and error if any exist."""
    name_counts_final = {}
    for page in pages:
        name = page["name"]
        name_counts_final[name] = name_counts_final.get(name, 0) + 1

    duplicates = {k: v for k, v in name_counts_final.items() if v > 1}
    if duplicates:
        print(f"ERROR: Found {len(duplicates)} duplicate page names:", file=sys.stderr)
        for name in sorted(duplicates.keys()):
            matching = [p for p in pages if p["name"] == name]
            for p in matching:
                print(
                    f"  {name}: {p['file']} -> {p.get('sourcePath', 'N/A')}",
                    file=sys.stderr,
                )
        return False
    return True


def main():
    """Generate pages.json from Doxygen man3 HTML files."""
    man3_dir = Path("public/man3")
    build_man3_dir = Path("../../build/share/man/man3")
    repo_root = Path("../../").resolve()

    if not man3_dir.exists():
        print("ERROR: public/man3 directory not found", file=sys.stderr)
        return False

    source_lookup = build_source_lookup(repo_root)
    pages = []

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
        name = extract_doxygen_name(man3_file)

        # Fallback: use basename with ascii-chat- prefix removed
        if not name:
            name = get_fallback_name(basename_no_ext)

        # Look up source path
        source_path = lookup_source_path(name, source_lookup, html_file)

        pages.append(
            {
                "name": name,
                "title": name,
                "file": filename,
                "sourcePath": source_path,
            }
        )

    # Deduplicate names
    deduplicate_names(pages)

    # Check for remaining duplicates
    if not check_duplicates(pages):
        return False

    # Write pages.json
    with open("public/man3/pages.json", "w") as f:
        json.dump(pages, f, indent=2)

    print(f"Generated pages.json with {len(pages)} entries")
    return True


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
