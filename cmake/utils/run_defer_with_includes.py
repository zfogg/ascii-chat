#!/usr/bin/env python3
"""
Wrapper script to run the defer tool with include paths extracted from the compilation database.

The defer tool (LibTooling-based) sometimes has issues resolving include paths from the
compilation database. This script extracts the include paths and passes them directly.
"""

import json
import sys
import subprocess
import os
import re
from pathlib import Path

def extract_include_paths(db_path, source_file):
    """Extract -I flags from compilation database for a source file."""

    try:
        with open(db_path, 'r') as f:
            db = json.load(f)
    except Exception as e:
        print(f"Error reading compilation database: {e}", file=sys.stderr)
        return []

    # Try to find matching entry for the source file
    # Handle both absolute and relative paths
    source_abs = os.path.abspath(source_file)
    source_rel = os.path.relpath(source_abs)

    for entry in db:
        entry_file = entry.get('file', '')

        # Match either absolute path or relative path
        if entry_file == source_abs or entry_file == source_rel:
            # Extract include paths from arguments if available
            if 'arguments' in entry:
                return [arg for arg in entry['arguments'] if arg.startswith('-I')]
            # Fallback to parsing command string
            elif 'command' in entry:
                return re.findall(r'-I[^ ]+', entry['command'])

    return []

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <compile_db_dir> <defer_tool_path> <source_file> [defer_args...]")
        sys.exit(1)

    db_dir = sys.argv[1]
    defer_tool = sys.argv[2]
    source_file = sys.argv[3]
    defer_args = sys.argv[4:] if len(sys.argv) > 4 else []

    # Find compilation database
    db_path = os.path.join(db_dir, 'compile_commands.json')
    if not os.path.exists(db_path):
        print(f"Error: compile_commands.json not found in {db_dir}", file=sys.stderr)
        sys.exit(1)

    # Extract include paths
    include_paths = extract_include_paths(db_path, source_file)

    if not include_paths:
        print(f"Warning: No include paths found for {source_file}", file=sys.stderr)
    else:
        print(f"Found {len(include_paths)} include paths for {source_file}", file=sys.stderr)

    # Build command: defer_tool [args] -I...paths... source_file
    # Insert include paths before the source file
    cmd = [defer_tool] + defer_args

    # Add include paths before the source file in args
    # Look for the source file position and insert before it
    if source_file in cmd:
        idx = cmd.index(source_file)
        for inc_path in include_paths:
            cmd.insert(idx, inc_path)
            idx += 1
    else:
        # Add at end before source file is appended
        for inc_path in include_paths:
            cmd.append(inc_path)

    # Run the defer tool
    print(f"Running: {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd)
    sys.exit(result.returncode)

if __name__ == '__main__':
    main()
