#!/usr/bin/env python3
"""
Test script for ACDS (ascii-chat Discovery Service) protocol

This script tests the ACIP binary protocol by connecting to the ACDS server
and sending SESSION_CREATE, SESSION_LOOKUP, and SESSION_JOIN packets.
"""

import socket
import struct
import sys
import time
from dataclasses import dataclass

# ACIP Packet Types (from lib/network/packet.h)
PACKET_TYPE_ACIP_SESSION_CREATE = 0x20
PACKET_TYPE_ACIP_SESSION_CREATED = 0x21
PACKET_TYPE_ACIP_SESSION_LOOKUP = 0x22
PACKET_TYPE_ACIP_SESSION_INFO = 0x23
PACKET_TYPE_ACIP_SESSION_JOIN = 0x24
PACKET_TYPE_ACIP_SESSION_JOINED = 0x25

# Packet header constants
PACKET_MAGIC = 0xDEADBEEF

@dataclass
class PacketHeader:
    """ACIP packet header structure"""
    magic: int  # uint32_t
    type: int   # uint16_t
    length: int # uint32_t
    crc32: int  # uint32_t
    client_id: int # uint32_t

def calculate_crc32(data: bytes) -> int:
    """Calculate CRC32 checksum (simple implementation for testing)"""
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF

def send_packet(sock: socket.socket, packet_type: int, payload: bytes):
    """Send an ACIP packet with header"""
    crc = calculate_crc32(payload)

    # Pack header in NETWORK byte order (big-endian): magic(u32), type(u16), length(u32), crc32(u32), client_id(u32)
    # Note: C code uses endian_unpack_u32() to convert from network byte order
    header = struct.pack('>IHIII',
                        PACKET_MAGIC,
                        packet_type,
                        len(payload),
                        crc,
                        0)  # client_id = 0

    packet = header + payload
    print(f"→ Sending packet type 0x{packet_type:02X}, payload_len={len(payload)}, crc=0x{crc:08X}")
    sock.sendall(packet)

def receive_packet(sock: socket.socket) -> tuple[int, bytes]:
    """Receive an ACIP packet and return (type, payload)"""
    # Receive header (18 bytes: 4+2+4+4+4)
    header_data = sock.recv(18)
    if len(header_data) < 18:
        raise ConnectionError("Failed to receive complete header")

    # Unpack in NETWORK byte order (big-endian)
    magic, pkt_type, length, crc, client_id = struct.unpack('>IHIII', header_data)

    if magic != PACKET_MAGIC:
        raise ValueError(f"Invalid magic number: 0x{magic:08X}")

    # Receive payload
    payload = b''
    if length > 0:
        payload = sock.recv(length)
        if len(payload) < length:
            raise ConnectionError(f"Failed to receive complete payload ({len(payload)}/{length} bytes)")

    print(f"← Received packet type 0x{pkt_type:02X}, payload_len={len(payload)}, crc=0x{crc:08X}")
    return (pkt_type, payload)

def test_session_create(sock: socket.socket):
    """Test SESSION_CREATE packet"""
    print("\n=== Test 1: SESSION_CREATE ===")

    # Build acip_session_create_t payload (229 bytes fixed part)
    identity_pubkey = b'\x00' * 32  # Dummy Ed25519 public key
    signature = b'\x00' * 64        # Dummy signature
    timestamp = int(time.time() * 1000)  # Unix ms

    capabilities = 0x03  # video=1, audio=1
    max_participants = 4
    has_password = 0
    password_hash = b'\x00' * 128
    reserved_string_len = 0  # Auto-generate session string

    payload = struct.pack('<32s64sQBBB128sB',
                         identity_pubkey,
                         signature,
                         timestamp,
                         capabilities,
                         max_participants,
                         has_password,
                         password_hash,
                         reserved_string_len)

    send_packet(sock, PACKET_TYPE_ACIP_SESSION_CREATE, payload)

    # Receive SESSION_CREATED response
    resp_type, resp_payload = receive_packet(sock)

    if resp_type != PACKET_TYPE_ACIP_SESSION_CREATED:
        print(f"✗ Expected SESSION_CREATED (0x{PACKET_TYPE_ACIP_SESSION_CREATED:02X}), got 0x{resp_type:02X}")
        return None

    # Parse response (66 bytes fixed part)
    string_len, session_string, session_id, expires_at, stun_count, turn_count = struct.unpack(
        '<B48s16sQBB', resp_payload[:66])

    session_string = session_string[:string_len].decode('utf-8')
    session_id_hex = session_id.hex()

    print(f"✓ Session created successfully!")
    print(f"  Session string: {session_string}")
    print(f"  Session ID: {session_id_hex}")
    print(f"  Expires at: {expires_at}")
    print(f"  STUN servers: {stun_count}, TURN servers: {turn_count}")

    return session_string

def test_session_lookup(sock: socket.socket, session_string: str):
    """Test SESSION_LOOKUP packet"""
    print(f"\n=== Test 2: SESSION_LOOKUP ('{session_string}') ===")

    # Build acip_session_lookup_t payload
    string_bytes = session_string.encode('utf-8')
    string_len = len(string_bytes)

    payload = struct.pack(f'<B48s', string_len, string_bytes.ljust(48, b'\x00'))

    send_packet(sock, PACKET_TYPE_ACIP_SESSION_LOOKUP, payload)

    # Receive SESSION_INFO response
    resp_type, resp_payload = receive_packet(sock)

    if resp_type != PACKET_TYPE_ACIP_SESSION_INFO:
        print(f"✗ Expected SESSION_INFO (0x{PACKET_TYPE_ACIP_SESSION_INFO:02X}), got 0x{resp_type:02X}")
        return False

    # Parse response
    found, session_id, host_pubkey, capabilities, max_p, curr_p, has_pw, created_at, expires_at = struct.unpack(
        '<B16s32sBBBBQQ', resp_payload[:93])

    if found:
        print(f"✓ Session found!")
        print(f"  Session ID: {session_id.hex()}")
        print(f"  Capabilities: 0x{capabilities:02X}")
        print(f"  Participants: {curr_p}/{max_p}")
        print(f"  Password protected: {'yes' if has_pw else 'no'}")
        return True
    else:
        print(f"✗ Session not found")
        return False

def test_session_join(sock: socket.socket, session_string: str):
    """Test SESSION_JOIN packet"""
    print(f"\n=== Test 3: SESSION_JOIN ('{session_string}') ===")

    # Build acip_session_join_t payload (241 bytes)
    string_bytes = session_string.encode('utf-8')
    string_len = len(string_bytes)

    identity_pubkey = b'\x11' * 32  # Different identity
    signature = b'\x00' * 64
    timestamp = int(time.time() * 1000)
    has_password = 0
    password = b'\x00' * 128

    payload = struct.pack('<B48s32s64sQB128s',
                         string_len,
                         string_bytes.ljust(48, b'\x00'),
                         identity_pubkey,
                         signature,
                         timestamp,
                         has_password,
                         password)

    send_packet(sock, PACKET_TYPE_ACIP_SESSION_JOIN, payload)

    # Receive SESSION_JOINED response
    resp_type, resp_payload = receive_packet(sock)

    if resp_type != PACKET_TYPE_ACIP_SESSION_JOINED:
        print(f"✗ Expected SESSION_JOINED (0x{PACKET_TYPE_ACIP_SESSION_JOINED:02X}), got 0x{resp_type:02X}")
        return False

    # Parse response
    success, error_code, error_message, participant_id, session_id = struct.unpack(
        '<BB128s16s16s', resp_payload[:162])

    if success:
        print(f"✓ Joined session successfully!")
        print(f"  Participant ID: {participant_id.hex()}")
        print(f"  Session ID: {session_id.hex()}")
        return True
    else:
        error_msg = error_message.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"✗ Join failed: {error_msg} (code={error_code})")
        return False

def main():
    """Run all ACDS protocol tests"""
    host = '127.0.0.1'
    port = 27225

    print(f"Connecting to ACDS server at {host}:{port}...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
        print("✓ Connected\n")

        # Test 1: Create session
        session_string = test_session_create(sock)
        if not session_string:
            print("\n✗ SESSION_CREATE test failed")
            return 1

        time.sleep(0.1)

        # Test 2: Lookup session
        if not test_session_lookup(sock, session_string):
            print("\n✗ SESSION_LOOKUP test failed")
            return 1

        time.sleep(0.1)

        # Test 3: Join session
        if not test_session_join(sock, session_string):
            print("\n✗ SESSION_JOIN test failed")
            return 1

        print("\n" + "="*50)
        print("✓ All tests passed!")
        print("="*50)

        sock.close()
        return 0

    except Exception as e:
        print(f"\n✗ Test failed with exception: {e}")
        import traceback
        traceback.print_exc()
        return 1

if __name__ == '__main__':
    sys.exit(main())
