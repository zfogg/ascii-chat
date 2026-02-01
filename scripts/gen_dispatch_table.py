#!/usr/bin/env python3
"""
Generate O(1) hash table dispatch code for C packet handlers.

This script reads packet type definitions from a C header file and generates
a hash table with linear probing for O(1) packet type -> handler dispatch.

Usage:
    # Generate hash table from network/packet.h for server client dispatch
    ./scripts/gen_dispatch_table.py lib/network/packet.h \
        --types PACKET_TYPE_PROTOCOL_VERSION,PACKET_TYPE_IMAGE_FRAME,... \
        --handlers handle_protocol_version_packet,handle_image_frame_packet,...

    # Or use a config file
    ./scripts/gen_dispatch_table.py --config scripts/dispatch_tables/server_client.json

    # Just print enum values
    ./scripts/gen_dispatch_table.py lib/network/packet.h --list-types

Example output:
    static const dispatch_entry_t g_dispatch_hash[32] = {
        [0]  = {PACKET_TYPE_AUDIO_BATCH, 2},  // hash(4000)=0
        [1]  = {PACKET_TYPE_PROTOCOL_VERSION, 0},  // hash(1)=1
        ...
    };
"""

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Optional


def parse_enum_values(header_path: Path, enum_prefix: str = "PACKET_TYPE_") -> dict[str, int]:
    """Parse enum values from a C header file."""
    content = header_path.read_text()

    # Match enum definitions like: PACKET_TYPE_FOO = 123,
    # Also handles hex values and expressions
    pattern = rf"({enum_prefix}\w+)\s*=\s*([^,\n]+)"

    values = {}
    for match in re.finditer(pattern, content):
        name = match.group(1)
        value_str = match.group(2).strip()

        # Handle simple integer values
        try:
            if value_str.startswith("0x"):
                value = int(value_str, 16)
            else:
                value = int(value_str)
            values[name] = value
        except ValueError:
            # Skip complex expressions for now
            pass

    return values


def compute_hash_table(
    entries: list[tuple[str, int, int]],  # (name, packet_type, handler_idx)
    table_size: int = 32
) -> tuple[list[tuple[int, str, int, str]], int]:
    """
    Compute hash table with linear probing.

    Returns:
        - List of (slot, name, handler_idx, comment) tuples
        - Maximum probe count
    """
    table: list[Optional[tuple[str, int, int]]] = [None] * table_size
    results = []
    max_probes = 0

    for name, ptype, handler_idx in entries:
        h = ptype % table_size
        orig_h = h
        probes = 0

        while table[h] is not None:
            h = (h + 1) % table_size
            probes += 1
            if probes >= table_size:
                raise ValueError(f"Hash table overflow! Increase table_size (currently {table_size})")

        table[h] = (name, ptype, handler_idx)
        max_probes = max(max_probes, probes)

        comment = f"hash({ptype})={orig_h}"
        if probes > 0:
            comment += f", probed->{h}"

        results.append((h, name, handler_idx, comment))

    return sorted(results, key=lambda x: x[0]), max_probes


def generate_c_code(
    entries: list[tuple[int, str, int, str]],
    table_size: int,
    handler_count: int,
    handlers: list[str],
    table_name: str = "g_dispatch_hash",
    handler_array_name: str = "g_dispatch_handlers",
    handler_typedef: str = "packet_handler_t",
    entry_typedef: str = "dispatch_entry_t",
    prefix: str = "",
) -> str:
    """Generate C code for the hash table."""

    lines = []

    # Constants
    lines.append(f"#define {prefix}DISPATCH_HASH_SIZE {table_size}")
    lines.append(f"#define {prefix}DISPATCH_HANDLER_COUNT {handler_count}")
    lines.append("")

    # Entry type
    lines.append(f"typedef struct {{")
    lines.append(f"  packet_type_t key;")
    lines.append(f"  uint8_t handler_idx;")
    lines.append(f"}} {entry_typedef};")
    lines.append("")

    # Hash function
    lines.append(f"#define {prefix}DISPATCH_HASH(type) ((type) % {prefix}DISPATCH_HASH_SIZE)")
    lines.append("")

    # Lookup function
    lines.append(f"static inline int {prefix.lower()}dispatch_hash_lookup(const {entry_typedef} *table, packet_type_t type) {{")
    lines.append(f"  uint32_t h = {prefix}DISPATCH_HASH(type);")
    lines.append(f"  for (int i = 0; i < {prefix}DISPATCH_HASH_SIZE; i++) {{")
    lines.append(f"    uint32_t slot = (h + i) % {prefix}DISPATCH_HASH_SIZE;")
    lines.append(f"    if (table[slot].key == 0) return -1;")
    lines.append(f"    if (table[slot].key == type) return table[slot].handler_idx;")
    lines.append(f"  }}")
    lines.append(f"  return -1;")
    lines.append(f"}}")
    lines.append("")

    # Handler array
    lines.append(f"// Handler array (indexed by hash lookup result)")
    lines.append(f"static const {handler_typedef} {handler_array_name}[{prefix}DISPATCH_HANDLER_COUNT] = {{")
    for i, handler in enumerate(handlers):
        lines.append(f"    ({handler_typedef}){handler},  // {i}")
    lines.append(f"}};")
    lines.append("")

    # Hash table
    lines.append(f"// Hash table mapping packet type -> handler index")
    lines.append(f"// clang-format off")
    lines.append(f"static const {entry_typedef} {table_name}[{prefix}DISPATCH_HASH_SIZE] = {{")

    for slot, name, handler_idx, comment in entries:
        lines.append(f"    [{slot:2}] = {{{name + ',':<40} {handler_idx:2}}},  // {comment}")

    lines.append(f"}};")
    lines.append(f"// clang-format on")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Generate O(1) hash table dispatch code for C packet handlers",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    parser.add_argument("header", nargs="?", type=Path,
                        help="Path to C header file with enum definitions")
    parser.add_argument("--config", type=Path,
                        help="JSON config file with types and handlers")
    parser.add_argument("--types", type=str,
                        help="Comma-separated list of packet type names")
    parser.add_argument("--handlers", type=str,
                        help="Comma-separated list of handler function names (same order as types)")
    parser.add_argument("--list-types", action="store_true",
                        help="List all packet types found in header")
    parser.add_argument("--table-size", type=int, default=32,
                        help="Hash table size (default: 32)")
    parser.add_argument("--prefix", type=str, default="",
                        help="Prefix for generated constants (e.g., 'CLIENT_')")
    parser.add_argument("--table-name", type=str, default="g_dispatch_hash",
                        help="Name for hash table variable")
    parser.add_argument("--handler-array", type=str, default="g_dispatch_handlers",
                        help="Name for handler array variable")
    parser.add_argument("--handler-typedef", type=str, default="packet_handler_t",
                        help="Typedef for handler function pointer")
    parser.add_argument("--entry-typedef", type=str, default="dispatch_entry_t",
                        help="Typedef for hash table entry")

    args = parser.parse_args()

    # Load config if provided
    if args.config:
        config = json.loads(args.config.read_text())
        args.header = Path(config.get("header", args.header))
        args.types = config.get("types", args.types)
        args.handlers = config.get("handlers", args.handlers)
        args.table_size = config.get("table_size", args.table_size)
        args.prefix = config.get("prefix", args.prefix)
        args.table_name = config.get("table_name", args.table_name)
        args.handler_array = config.get("handler_array", args.handler_array)
        args.handler_typedef = config.get("handler_typedef", args.handler_typedef)
        args.entry_typedef = config.get("entry_typedef", args.entry_typedef)

    if not args.header:
        parser.error("Header file required (positional argument or in config)")

    if not args.header.exists():
        print(f"Error: Header file not found: {args.header}", file=sys.stderr)
        sys.exit(1)

    # Parse enum values
    enum_values = parse_enum_values(args.header)

    if args.list_types:
        print(f"Packet types found in {args.header}:")
        for name, value in sorted(enum_values.items(), key=lambda x: x[1]):
            print(f"  {name} = {value}")
        sys.exit(0)

    if not args.types or not args.handlers:
        parser.error("--types and --handlers required (or use --config)")

    # Parse type and handler lists
    type_names = [t.strip() for t in args.types.split(",")]
    handlers = [h.strip() for h in args.handlers.split(",")]

    if len(type_names) != len(handlers):
        print(f"Error: Number of types ({len(type_names)}) must match handlers ({len(handlers)})",
              file=sys.stderr)
        sys.exit(1)

    # Build entries list
    entries = []
    for i, (type_name, handler) in enumerate(zip(type_names, handlers)):
        if type_name not in enum_values:
            print(f"Error: Unknown packet type: {type_name}", file=sys.stderr)
            print(f"Available types: {', '.join(sorted(enum_values.keys()))}", file=sys.stderr)
            sys.exit(1)
        entries.append((type_name, enum_values[type_name], i))

    # Compute hash table
    try:
        hash_entries, max_probes = compute_hash_table(entries, args.table_size)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    # Generate C code
    code = generate_c_code(
        hash_entries,
        args.table_size,
        len(handlers),
        handlers,
        table_name=args.table_name,
        handler_array_name=args.handler_array,
        handler_typedef=args.handler_typedef,
        entry_typedef=args.entry_typedef,
        prefix=args.prefix,
    )

    print(code)

    # Print stats to stderr
    load_factor = len(entries) / args.table_size * 100
    print(f"\n// Stats: {len(entries)} entries, table size {args.table_size}, "
          f"load factor {load_factor:.1f}%, max probes {max_probes}", file=sys.stderr)


if __name__ == "__main__":
    main()
