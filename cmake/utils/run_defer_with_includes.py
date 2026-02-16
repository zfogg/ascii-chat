#!/usr/bin/env python3
"""
Wrapper script to run the defer tool with the compilation database.

The defer tool (LibTooling-based) reads include paths from the compilation database
automatically via the -p parameter. This script just ensures proper invocation.
"""

import sys
import subprocess
import os

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <compile_db_dir> <defer_tool_path> <source_file> [defer_args...]")
        sys.exit(1)

    db_dir = sys.argv[1]
    defer_tool = sys.argv[2]
    source_file = sys.argv[3]
    defer_args = sys.argv[4:] if len(sys.argv) > 4 else []

    # Find compilation database (defer tool uses compile_commands_defer.json)
    db_path = os.path.join(db_dir, 'compile_commands_defer.json')
    if not os.path.exists(db_path):
        print(f"Error: compile_commands_defer.json not found in {db_dir}", file=sys.stderr)
        sys.exit(1)

    # Build command: defer_tool -p <db_dir> [other_args] source_file
    # The defer tool will read include paths from the compilation database automatically
    cmd = [defer_tool, "-p", db_dir] + defer_args + [source_file]

    # Run the defer tool
    print(f"Running: {' '.join(cmd)}", file=sys.stderr)
    result = subprocess.run(cmd)
    sys.exit(result.returncode)

if __name__ == '__main__':
    main()
