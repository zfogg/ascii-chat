#!/usr/bin/env python3
"""Format tracked project files, or the files staged for a commit."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
EXTENSIONS = {'.md', '.mdx', '.toml', '.js', '.mjs', '.cjs', '.jsx', '.ts', '.mts', '.cts', '.tsx', '.json', '.jsonc', '.json5', '.css', '.scss', '.less', '.html', '.htm', '.yaml', '.yml', '.graphql', '.gql', '.vue', '.svelte', '.astro'}

def git(*args):
    return subprocess.check_output(['git', *args]).decode('utf-8').split('\0')[:-1]

def run(command):
    subprocess.run(command, check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('group', choices=['docs', 'config', 'web', 'other', 'all'])
    parser.add_argument('--staged', action='store_true')
    args = parser.parse_args()
    files = git('diff', '--cached', '--name-only', '--diff-filter=ACM', '-z') if args.staged else git('ls-files', '-z')
    def group(path):
        if path.startswith('docs/'):
            return 'docs'
        if path.endswith('.toml') or (path.endswith(('.md', '.mdx')) and not path.startswith('web/')):
            return 'config'
        if path.startswith('web/'):
            return 'web'
        return 'other'
    def selected(path):
        if args.group == 'all':
            return True
        if args.group == 'config':
            return path.endswith('.toml') or (path.endswith(('.md', '.mdx')) and not path.startswith('docs/'))
        return group(path) == args.group
    files = [p for p in files if Path(p).is_file() and (Path(p).suffix.lower() in EXTENSIONS or p.endswith('.dox')) and selected(p)]
    if args.staged:
        dirty = set(git('diff', '--name-only', '-z')) & set(files)
        if dirty:
            sys.exit('Cannot format partially staged files; stage or stash their unstaged changes first:\n' + '\n'.join(sorted(dirty)))
    vp = ['node', str(ROOT / 'node_modules' / 'vite-plus' / 'bin' / 'vp')]
    # Group web projects independently so all sites and shared packages run concurrently.
    batches = {}
    for path in files:
        if path.endswith('.dox'):
            continue
        parts = path.split('/')
        key = '/'.join(parts[:3] if len(parts) > 2 and parts[1] in ('packages', 'openai-sites', 'chatgpt-sites') else parts[:2]) if path.startswith('web/') else group(path)
        batches.setdefault(key, []).append(path)
    def format_batch(paths):
        # Bound command-line length on Windows.
        for start in range(0, len(paths), 30):
            run([*vp, 'fmt', '--threads=2', '--no-error-on-unmatched-pattern', '--config', str(ROOT / '.oxfmtrc.json'), '--ignore-path', str(ROOT / 'scripts' / 'format-empty-ignore'), *paths[start:start+30]])
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(4, max(1, len(batches)))) as pool:
        list(pool.map(format_batch, batches.values()))
    for path in files:
        if path.endswith('.dox'):
            # Doxygen examples can contain nested C comment delimiters, which
            # clang-format treats as real C and can corrupt. Preserve their text.
            doc = Path(path)
            content = doc.read_bytes()
            trimmed = content.rstrip(b'\r\n') + (b'\r\n' if b'\r\n' in content else b'\n')
            if trimmed != content:
                doc.write_bytes(trimmed)
    if args.staged and files:
        for start in range(0, len(files), 30):
            run(['git', 'add', '-u', '--', *files[start:start+30]])

if __name__ == '__main__':
    main()
