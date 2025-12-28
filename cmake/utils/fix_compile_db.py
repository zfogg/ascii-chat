#!/usr/bin/env python3
"""
Fix the "directory" field in compile_commands.json to use source directory
instead of the temp build directory. This allows LibTooling-based tools
like the defer tool to resolve includes correctly.
"""

import json
import sys

def fix_compilation_db(db_file, source_dir):
    """Fix directory field in compilation database."""
    try:
        with open(db_file, 'r') as f:
            db = json.load(f)

        # Replace all "directory" fields with the source directory
        for entry in db:
            entry['directory'] = source_dir

        with open(db_file, 'w') as f:
            json.dump(db, f, indent=2)

        print(f"Fixed compilation database: {db_file}")
        return True
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return False

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: fix_compile_db.py <input_file> <source_dir>", file=sys.stderr)
        sys.exit(1)

    db_file = sys.argv[1]
    source_dir = sys.argv[2]

    if not fix_compilation_db(db_file, source_dir):
        sys.exit(1)
