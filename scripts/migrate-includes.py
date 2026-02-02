#!/usr/bin/env python3
"""Migrate ascii-chat includes to <ascii-chat/...> style"""
import re
from pathlib import Path

LIB_MODULES = {
    'audio', 'common', 'crypto', 'debug', 'discovery', 'log',
    'media', 'network', 'options', 'platform', 'session',
    'tests', 'tooling', 'util', 'video', 'uthash'
}

TOP_LEVEL_HEADERS = {
    'common.h', 'asciichat_errno.h', 'buffer_pool.h', 'ringbuffer.h',
    'thread_pool.h', 'embedded_resources.h', 'version.h', 'paths.h'
}

def should_migrate(path: str, current_file: Path | None = None) -> bool:
    """Check if include should become <ascii-chat/...>"""
    # Already angle bracket or dependency
    if path.startswith('<') or 'ascii-chat-deps' in path:
        return False

    # Top-level lib headers (common.h, buffer_pool.h, etc.)
    if '/' not in path and path in TOP_LEVEL_HEADERS:
        return True

    # Module headers (network/network.h, crypto/crypto.h, etc.)
    if '/' in path:
        module = path.split('/')[0]
        return module in LIB_MODULES

    # Local headers in the same module (e.g., capture.h in lib/session/)
    # These should also be migrated to angle-bracket style
    if current_file and '/' not in path:
        try:
            rel_path = current_file.relative_to(Path.cwd().parent.parent / 'lib')
            module_dir = rel_path.parent
            if module_dir != Path('.'):
                # Check if this is a header file in the same module directory
                potential_header = Path.cwd().parent.parent / 'include' / 'ascii-chat' / module_dir / path
                if potential_header.exists():
                    return True
        except (ValueError, RuntimeError):
            pass

    return False

def migrate_file(filepath: Path) -> bool:
    """Migrate includes in a single file"""
    try:
        content = filepath.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        # Skip binary files
        return False

    lines = content.splitlines(keepends=True)

    include_re = re.compile(r'^(\s*#\s*include\s+)"([^"]+)"(.*)$')
    changed = False

    for i, line in enumerate(lines):
        if match := include_re.match(line):
            prefix, path, suffix = match.groups()
            # For local module headers, we need to include the module path
            if should_migrate(path, filepath):
                # If it's a local header (no slash), determine the module
                if '/' not in path:
                    try:
                        rel_path = filepath.relative_to(Path.cwd().parent.parent / 'lib')
                        module_dir = rel_path.parent
                        if module_dir != Path('.'):
                            # It's a module local header
                            new_include = f'{prefix}<ascii-chat/{module_dir}/{path}>{suffix}\n'
                        else:
                            # Top-level header
                            new_include = f'{prefix}<ascii-chat/{path}>{suffix}\n'
                    except (ValueError, RuntimeError):
                        new_include = f'{prefix}<ascii-chat/{path}>{suffix}\n'
                else:
                    new_include = f'{prefix}<ascii-chat/{path}>{suffix}\n'

                lines[i] = new_include
                changed = True

    if changed:
        filepath.write_text(''.join(lines), encoding='utf-8')
        print(f'✓ {filepath.relative_to(Path.cwd().parent.parent)}')

    return changed

def main():
    repo_root = Path(__file__).parent.parent
    total_files = 0

    print("Migrating includes in lib/...")
    for pattern in ['**/*.c', '**/*.h', '**/*.m', '**/*.cpp']:
        for f in (repo_root / 'lib').glob(pattern):
            if 'deps/' not in str(f) and 'ascii-chat-deps' not in str(f):
                if migrate_file(f):
                    total_files += 1

    print("\nMigrating includes in src/...")
    for pattern in ['**/*.c', '**/*.h', '**/*.m', '**/*.cpp']:
        for f in (repo_root / 'src').glob(pattern):
            if migrate_file(f):
                total_files += 1

    print(f"\n✓ Migrated {total_files} files")

if __name__ == '__main__':
    main()
