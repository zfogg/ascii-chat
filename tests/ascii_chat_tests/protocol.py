"""Helpers for exercising the native wire protocol."""

import re
import struct
from pathlib import Path


def version_packet(encryption=0, feature_flags=0):
    constants = (Path(__file__).resolve().parents[2] / "include/ascii-chat/common/protocol_constants.h").read_text()
    versions = []
    for part in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"^#define PROTOCOL_VERSION_{part} (\d+)$", constants, re.MULTILINE)
        if match is None:
            raise ValueError(f"Missing protocol version {part}")
        versions.append(int(match.group(1)))
    return struct.pack("!HHHBBBH5x", *versions, encryption, 0, 0, feature_flags)

def crc32c(data):
    crc = 0xffffffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    return crc ^ 0xffffffff
