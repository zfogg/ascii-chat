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
    except Exception as e:
        print(f"Warning: Could not read compilation database: {e}", file=sys.stderr)
        return False

    # Try to find an entry for this source file
    # First, try exact match (shouldn't happen but worth checking)
    for entry in db:
        if entry.get('file', '').endswith(source_file):
            print(f"DEBUG: Found exact match for {source_file} in database", file=sys.stderr)
            return True  # Found exact match, no need to fix

    # Look for a transformed version of this file
    source_name = os.path.basename(source_file)
    source_dir = os.path.dirname(source_file)

    for entry in db:
        file_path = entry.get('file', '')
        # Check if this is a defer_transformed version of our target file
        if f'defer_transformed/{source_file}' in file_path:
            print(f"DEBUG: Found transformed version, fixing path for {source_file}", file=sys.stderr)
            # Update this entry to use the original file path
            # Keep everything else (command, directory, etc) the same
            original_abs_path = os.path.join(os.path.dirname(file_path).replace('defer_transformed/', ''), source_dir, source_name)
            entry['file'] = original_abs_path

            # Write the fixed database back
            try:
                with open(db_path, 'w') as f:
                    json.dump(db, f)
                print(f"DEBUG: Updated database with corrected path", file=sys.stderr)
                return True
            except Exception as e:
                print(f"Warning: Could not write compilation database: {e}", file=sys.stderr)
                return False

    print(f"DEBUG: No matching entry found for {source_file} (checked {len(db)} entries)", file=sys.stderr)
    return False

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <compile_db_dir> <defer_tool_path> <source_file> [defer_args...]")
        sys.exit(1)

    db_dir = sys.argv[1]
    defer_tool = sys.argv[2]
    source_file = sys.argv[3]
    defer_args = sys.argv[4:] if len(sys.argv) > 4 else []

    # Find compilation database
    # Prefer compile_commands_defer.json (generated without PCH) over compile_commands.json
    defer_db_path = os.path.join(db_dir, 'compile_commands_defer.json')
    standard_db_path = os.path.join(db_dir, 'compile_commands.json')

    if os.path.exists(defer_db_path):
        db_path = defer_db_path
    elif os.path.exists(standard_db_path):
        db_path = standard_db_path
    else:
        print(f"Error: No compilation database found in {db_dir}", file=sys.stderr)
        sys.exit(1)

    # LibTooling expects -p to point to a directory containing compile_commands.json
    # If we're using the defer-specific database, we need to point to a directory
    # where compile_commands.json exists with the correct content
    if db_path == defer_db_path:
        # Create a temp directory with a symlink to the defer database as compile_commands.json
        defer_db_link_dir = os.path.join(db_dir, 'defer_db_link')
        os.makedirs(defer_db_link_dir, exist_ok=True)
        link_path = os.path.join(defer_db_link_dir, 'compile_commands.json')

        # Use atomic rename to avoid race conditions when multiple processes update simultaneously
        try:
            # Create symlink in a temp file first
            temp_link = link_path + '.tmp'
            if os.path.exists(temp_link):
                os.unlink(temp_link)
            try:
                # Use relative symlink so it works if directory is moved
                os.symlink(os.path.relpath(defer_db_path, defer_db_link_dir), temp_link)
                # Atomically move temp to final location
                os.replace(temp_link, link_path)
            except OSError:
                # Symlink failed (e.g., Windows), use copy instead
                import shutil
                shutil.copy2(defer_db_path, temp_link)
                os.replace(temp_link, link_path)
        except Exception as e:
            # If symlink creation still fails, it's likely already created by another process
            # Just verify it exists
            if not os.path.exists(link_path):
                print(f"Warning: Could not create symlink at {link_path}: {e}", file=sys.stderr)

        db_dir_to_use = defer_db_link_dir
    else:
        db_dir_to_use = db_dir

    # Fix the database if it has defer-transformed file paths instead of original paths
    fix_compilation_database(db_path, source_file)

    # Build command: defer_tool -p <db_dir> [other_args] source_file
    # The defer tool will read include paths from the compilation database automatically
    cmd = [defer_tool, "-p", db_dir_to_use] + defer_args + [source_file]

    # Run the defer tool
    print(f"Running: {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd)
    sys.exit(result.returncode)

if __name__ == '__main__':
    main()
