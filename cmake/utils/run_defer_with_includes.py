#!/usr/bin/env python3
"""
Wrapper script to run the defer tool with the compilation database.

The defer tool (LibTooling-based) reads include paths from the compilation database
automatically via the -p parameter. This script just ensures proper invocation.
"""

import sys
import subprocess
import os

def fix_compilation_database(db_path, source_file):
    """
    Fix compilation database entries that reference defer-transformed files.
    When the temp build runs, it also runs defer transformation, so the database
    has entries for defer_transformed/file.c instead of the original file.c.
    We need to find an entry and adjust it to use the original source file path.
    """
    import json
    try:
        with open(db_path, 'r') as f:
            db = json.load(f)
    except Exception:
        return False

    # Try to find an entry for this source file
    # First, try exact match (shouldn't happen but worth checking)
    for entry in db:
        if entry.get('file', '').endswith(source_file):
            return True  # Found exact match, no need to fix

    # Look for a transformed version of this file
    source_name = os.path.basename(source_file)
    source_dir = os.path.dirname(source_file)

    for entry in db:
        file_path = entry.get('file', '')
        # Check if this is a defer_transformed version of our target file
        if f'defer_transformed/{source_file}' in file_path:
            # Update this entry to use the original file path
            # Keep everything else (command, directory, etc) the same
            original_abs_path = os.path.join(os.path.dirname(file_path).replace('defer_transformed/', ''), source_dir, source_name)
            entry['file'] = original_abs_path

            # Write the fixed database back
            try:
                with open(db_path, 'w') as f:
                    json.dump(db, f)
                return True
            except Exception:
                return False

    return False

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <compile_db_dir> <defer_tool_path> <source_file> [defer_args...]")
        sys.exit(1)

    db_dir = sys.argv[1]
    defer_tool = sys.argv[2]
    source_file = sys.argv[3]
    defer_args = sys.argv[4:] if len(sys.argv) > 4 else []

    # Find compilation database (standard LibTooling expects compile_commands.json)
    # The compilation database generation utility copies the defer-specific database back
    # to the temp directory as compile_commands.json
    db_path = os.path.join(db_dir, 'compile_commands.json')
    if not os.path.exists(db_path):
        print(f"Error: compile_commands.json not found in {db_dir}", file=sys.stderr)
        sys.exit(1)

    # Fix the database if it has defer-transformed file paths instead of original paths
    fix_compilation_database(db_path, source_file)

    # Build command: defer_tool -p <db_dir> [other_args] source_file
    # The defer tool will read include paths from the compilation database automatically
    cmd = [defer_tool, "-p", db_dir] + defer_args + [source_file]

    # Run the defer tool
    print(f"Running: {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd)
    sys.exit(result.returncode)

if __name__ == '__main__':
    main()
