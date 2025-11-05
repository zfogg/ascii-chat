#!/usr/bin/env python3
"""
Analyze header file dependencies and generate a Graphviz DOT file.
Correctly handles relative includes like ../common.h and ../../common.h

By default, shows TRANSITIVE dependencies (if A includes B and B includes C, show A→C).
Use --direct to show only direct includes.
"""

import os
import re
import subprocess
import argparse
from pathlib import Path
from collections import defaultdict

def find_headers(directories=["lib", "src"], include_source=True):
    """
    Find all .h files (and optionally .c files) in specified directories

    Args:
        directories: List of directories to search
        include_source: If True, also include .c files to see full dependency picture
    """
    files = []
    for directory in directories:
        if Path(directory).exists():
            files.extend(Path(directory).rglob("*.h"))
            if include_source:
                files.extend(Path(directory).rglob("*.c"))
                files.extend(Path(directory).rglob("*.m"))  # Objective-C for macOS
    return files

def extract_includes(header_path):
    """Extract all #include "..." statements from a header"""
    includes = []
    try:
        with open(header_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                # Match #include "..."
                match = re.match(r'^\s*#\s*include\s*"([^"]+)"', line)
                if match:
                    includes.append(match.group(1))
    except Exception as e:
        print(f"Warning: Could not read {header_path}: {e}")
    return includes

def resolve_include(header_path, include_str, base_dirs=["lib", "src"]):
    """
    Resolve an include string to an actual header file path.

    Includes are resolved in this order:
    1. Relative to header's directory: "keys/types.h" from crypto/ → crypto/keys/types.h
    2. Relative to lib/ directory: "common.h" → lib/common.h (due to -Ilib/ flag)
    3. Relative to src/server/ or src/client/: "client.h" → src/server/client.h (due to -Isrc/server/ flag)
    4. Relative to src/ directory: "client/display.h" → src/client/display.h (due to -Isrc/ flag)
    """
    # Skip external dependencies
    if include_str.startswith('deps/') or include_str.startswith('/'):
        return None

    # Strategy 1: Try relative to the header's directory first (handles subdirectory includes)
    header_dir = Path(header_path).parent
    resolved = (header_dir / include_str).resolve()
    if resolved.exists() and resolved.is_file():
        # Make sure it's within one of our base directories
        for base_dir in base_dirs:
            try:
                resolved.relative_to(Path(base_dir).resolve())
                return resolved
            except ValueError:
                continue

    # Strategy 2: Try relative to each base directory (lib/, src/)
    for base_dir in base_dirs:
        base_path = Path(base_dir)
        if not base_path.exists():
            continue
        resolved = base_path / include_str
        if resolved.exists() and resolved.is_file():
            return resolved

    # Strategy 3: For src/ headers, also try src/server/ and src/client/ subdirectories
    # (compiler uses -Isrc/server/ and -Isrc/client/)
    if 'src' in base_dirs:
        for subdir in ['server', 'client']:
            resolved = Path('src') / subdir / include_str
            if resolved.exists() and resolved.is_file():
                return resolved

    return None

def normalize_path(path, base_dirs=["lib", "src"]):
    """Convert lib/foo/bar.h or src/client/main.h to foo_bar or src_client_main"""
    # Resolve path to handle absolute vs relative
    path = Path(path).resolve()

    # Try to make relative to each base directory
    for base_dir in base_dirs:
        try:
            base_path = Path(base_dir).resolve()
            rel_path = path.relative_to(base_path)
            # Prepend directory name to avoid collisions (lib_common vs src_common)
            return f"{base_dir}_{str(rel_path.with_suffix('')).replace('/', '_').replace(chr(92), '_')}"
        except ValueError:
            continue

    # Fallback: use full path
    return str(path.with_suffix('')).replace('/', '_').replace('\\', '_')

def generate_dot_file(output_file="header_deps.dot", base_dirs=["lib", "src"], use_transitive=True):
    """
    Generate Graphviz DOT file from header dependencies

    Args:
        output_file: Path to output DOT file
        base_dirs: List of base directories to search
        use_transitive: If True, show transitive dependencies; if False, only direct
    """

    headers = find_headers(base_dirs)
    print(f"Found {len(headers)} headers in {', '.join(base_dirs)}/")

    # Build dependency graph
    dependencies = defaultdict(set)
    all_nodes = set()

    for header in headers:
        from_node = normalize_path(header, base_dirs)
        all_nodes.add(from_node)

        includes = extract_includes(header)
        for inc in includes:
            resolved = resolve_include(header, inc, base_dirs)
            if resolved:
                to_node = normalize_path(resolved, base_dirs)
                all_nodes.add(to_node)
                dependencies[from_node].add(to_node)

    print(f"Found {len(dependencies)} headers with direct dependencies")
    total_edges = sum(len(deps) for deps in dependencies.values())
    print(f"Total direct dependency edges: {total_edges}")

    # Compute transitive dependencies if requested
    if use_transitive:
        print("Computing transitive closure...")
        transitive_deps = compute_transitive_closure(dependencies)
        total_transitive_edges = sum(len(deps) for deps in transitive_deps.values())
        print(f"Total transitive dependency edges: {total_transitive_edges}")
        deps_to_write = transitive_deps
        graph_label = "ascii-chat Header Dependencies (TRANSITIVE)"
    else:
        deps_to_write = dependencies
        graph_label = "ascii-chat Header Dependencies (DIRECT)"

    # Categorize nodes by module
    def categorize_node(node):
        # Handle both lib_ and src_ prefixes
        if node.startswith('src_'):
            if node.startswith('src_server'):
                return 'src_server'
            elif node.startswith('src_client'):
                return 'src_client'
            return 'src_other'
        elif node.startswith('lib_crypto'):
            return 'crypto'
        elif node.startswith('lib_platform'):
            return 'platform'
        elif node.startswith('lib_util'):
            return 'util'
        elif node.startswith('lib_image2ascii') or node.startswith('lib_os_'):
            return 'simd'
        elif node.startswith('lib_network') or node.startswith('lib_packet') or \
             node.startswith('lib_buffer_pool') or node.startswith('lib_compression') or \
             node.startswith('lib_crc32'):
            return 'network'
        elif node.startswith('lib_audio') or node.startswith('lib_video') or \
             node.startswith('lib_mixer') or node.startswith('lib_ringbuffer'):
            return 'media'
        elif node == 'lib_common' or node.startswith('lib_logging') or node.startswith('lib_options') or \
             node.startswith('lib_config') or node.startswith('lib_palette') or \
             node.startswith('lib_asciichat') or node.startswith('lib_lock_debug') or \
             node.startswith('lib_version') or node.startswith('lib_tests'):
            return 'core'
        return 'other'

    categorized = defaultdict(list)
    for node in sorted(all_nodes):
        category = categorize_node(node)
        categorized[category].append(node)

    # Write DOT file
    with open(output_file, 'w') as f:
        f.write('digraph header_deps {\n')
        f.write('  overlap=false;\n')
        f.write(f'  graph [fontname="Arial", label="{graph_label}", labelloc=t, fontsize=20];\n')
        f.write('  node [shape=box, style="rounded,filled", fontname="Arial", fontsize=14];\n')
        f.write('  edge [fontname="Arial", fontsize=10, color="#666666"];\n')
        f.write('\n')
        f.write('  // Default color\n')
        f.write('  node [fillcolor=lightblue];\n')
        f.write('\n')

        # Write categorized nodes with colors
        colors = {
            'crypto': ('lightcoral', 'Crypto module'),
            'platform': ('lightgreen', 'Platform abstraction'),
            'util': ('lightyellow', 'Utilities'),
            'simd': ('lightcyan', 'SIMD/Image processing'),
            'network': ('plum', 'Network'),
            'media': ('wheat', 'Media'),
            'core': ('lightgray', 'Core/Common'),
            'src_server': ('lightblue', 'Server application'),
            'src_client': ('lightpink', 'Client application'),
            'src_other': ('lavender', 'Other src'),
        }

        for category, (color, label) in colors.items():
            if category in categorized:
                f.write(f'  // {label} ({color})\n')
                f.write(f'  node [fillcolor={color}] {{\n')
                for node in categorized[category]:
                    f.write(f'    "{node}";\n')
                f.write('  }\n\n')

        # Write dependencies
        f.write('  // Dependencies\n\n')
        for from_node in sorted(deps_to_write.keys()):
            for to_node in sorted(deps_to_write[from_node]):
                f.write(f'  "{from_node}" -> "{to_node}";\n')

        f.write('}\n')

    print(f"Generated {output_file}")
    return dependencies, all_nodes  # Always return direct dependencies for stats

def compute_transitive_closure(dependencies):
    """
    Compute transitive closure of dependencies.

    If A → B and B → C, then A transitively includes C.
    Returns dict mapping each node to set of ALL headers it includes (direct + transitive)
    """
    transitive = defaultdict(set)

    # Initialize with direct dependencies
    for node, deps in dependencies.items():
        transitive[node] = set(deps)

    # Floyd-Warshall algorithm for transitive closure
    all_nodes = set(dependencies.keys()) | set(dep for deps in dependencies.values() for dep in deps)

    changed = True
    iterations = 0
    max_iterations = 100  # Safety limit

    while changed and iterations < max_iterations:
        changed = False
        iterations += 1

        for node in all_nodes:
            old_size = len(transitive[node])

            # Add transitive dependencies
            for intermediate in list(transitive[node]):
                if intermediate in transitive:
                    transitive[node] |= transitive[intermediate]

            if len(transitive[node]) > old_size:
                changed = True

    return transitive

def generate_stats(dependencies):
    """Generate and print dependency statistics"""

    # Compute transitive dependencies
    print("\n=== Computing transitive dependencies ===")
    transitive = compute_transitive_closure(dependencies)

    # Count direct incoming dependencies (fanin)
    direct_fanin = defaultdict(int)
    for from_node, to_nodes in dependencies.items():
        for to_node in to_nodes:
            direct_fanin[to_node] += 1

    # Count transitive incoming dependencies
    transitive_fanin = defaultdict(int)
    for from_node, to_nodes in transitive.items():
        for to_node in to_nodes:
            transitive_fanin[to_node] += 1

    # Count outgoing dependencies (fanout)
    direct_fanout = {node: len(deps) for node, deps in dependencies.items()}
    transitive_fanout = {node: len(deps) for node, deps in transitive.items()}

    print("\n=== Most Included Headers - DIRECT (Top 15) ===")
    for node, count in sorted(direct_fanin.items(), key=lambda x: -x[1])[:15]:
        print(f"{count:6d}  {node}")

    print("\n=== Most Included Headers - TRANSITIVE (Top 15) ===")
    for node, count in sorted(transitive_fanin.items(), key=lambda x: -x[1])[:15]:
        trans_count = transitive_fanin[node]
        direct_count = direct_fanin.get(node, 0)
        print(f"{trans_count:6d}  {node}  (direct: {direct_count})")

    print("\n=== Headers with Most Dependencies - DIRECT (Top 15) ===")
    for node, count in sorted(direct_fanout.items(), key=lambda x: -x[1])[:15]:
        print(f"{count:6d}  {node}")

    print("\n=== Headers with Most Dependencies - TRANSITIVE (Top 15) ===")
    for node, count in sorted(transitive_fanout.items(), key=lambda x: -x[1])[:15]:
        trans_count = transitive_fanout.get(node, 0)
        direct_count = direct_fanout.get(node, 0)
        print(f"{trans_count:6d}  {node}  (direct: {direct_count})")

def render_graph(dot_file="header_deps.dot", layout='fdp'):
    """Render DOT file to SVG and PNG using graphviz with specified layout"""
    try:
        # PNG
        subprocess.run(['dot', f'-K{layout}', '-Tpng', dot_file, '-o', 'header_deps.png'], check=True)
        print(f"Generated header_deps.png (using {layout} layout)")

        # SVG
        subprocess.run(['dot', f'-K{layout}', '-Tsvg', dot_file, '-o', 'header_deps.svg'], check=True)
        print(f"Generated header_deps.svg (using {layout} layout)")

        return True
    except FileNotFoundError:
        print("\nInstall graphviz to render the graph:")
        print("  Windows (scoop): scoop install graphviz")
        print("  macOS:           brew install graphviz")
        print("  Linux:           apt install graphviz")
        print(f"\nThen run: dot -K{layout} -Tsvg {dot_file} -o header_deps.svg")
        return False
    except subprocess.CalledProcessError as e:
        print(f"Error rendering graph: {e}")
        return False

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Analyze header dependencies and generate dependency graph',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
The graph always shows DIRECT dependencies by default (for readability).
Statistics show both direct and transitive dependency counts.

Examples:
  # Generate direct dependency graph with default fdp layout
  python3 scripts/analyze_header_deps.py

  # Use compact neato layout
  python3 scripts/analyze_header_deps.py --layout neato

  # Use spread-out sfdp layout
  python3 scripts/analyze_header_deps.py --layout sfdp

  # Show transitive graph (warning: very dense, hard to read)
  python3 scripts/analyze_header_deps.py --transitive-graph

  # Custom output file
  python3 scripts/analyze_header_deps.py -o my_deps.dot
        """
    )
    parser.add_argument(
        '--transitive-graph',
        action='store_true',
        help='Show transitive dependencies in graph (creates very dense graph with 1000+ edges)'
    )
    parser.add_argument(
        '-o', '--output',
        default='header_deps.dot',
        help='Output DOT file path (default: header_deps.dot)'
    )
    parser.add_argument(
        '--layout',
        choices=['neato', 'fdp', 'sfdp'],
        default='fdp',
        help='Graph layout algorithm (default: fdp) - neato=compact, fdp=balanced, sfdp=spread out'
    )
    args = parser.parse_args()

    use_transitive = args.transitive_graph  # Only show transitive in graph if explicitly requested

    print("Analyzing header dependencies in lib/ and src/...")
    if use_transitive:
        print("Graph mode: TRANSITIVE (warning: creates very dense graph)")
    else:
        print("Graph mode: DIRECT (showing only direct #include statements)")
    print("Stats mode: Computing both direct and transitive statistics")
    print(f"Layout: {args.layout}")

    dependencies, all_nodes = generate_dot_file(
        output_file=args.output,
        use_transitive=use_transitive
    )
    generate_stats(dependencies)
    render_graph(args.output, layout=args.layout)
