# ASCII-Chat TODO Summary

This document provides a comprehensive overview of all TODOs in the ascii-chat codebase, organized by category and status.

## Completed TODOs (January 2026)

### 1. ACDS Server Graceful Shutdown ✅
- **File**: `src/acds/server.c:184`
- **Status**: COMPLETED
- **Description**: Implemented graceful shutdown that waits for all client handler threads to exit before destroying the worker thread pool.
- **Implementation**: Added 10-second wait loop checking `tcp_server_get_client_count()` with 100ms polling intervals.
- **Benefits**: Ensures clean server shutdown without orphaned threads or resource leaks.

### 2. Windows Audio Thread Priority ✅
- **File**: `lib/audio/audio.c:1407`
- **Status**: COMPLETED
- **Description**: Implemented Windows-specific thread priority setting for audio threads using `SetThreadPriority()` with `THREAD_PRIORITY_TIME_CRITICAL`.
- **Implementation**: Added Windows-specific `#ifdef _WIN32` block using native Windows thread API.
- **Benefits**: Audio processing now has consistent real-time priority across all platforms (Linux, macOS, Windows).

### 3. Configurable ACDS Server Address ✅
- **File**: `src/client/crypto.c:347`
- **Status**: COMPLETED
- **Description**: Made ACDS server address and port configurable via `--acds-server` and `--acds-port` command-line options.
- **Implementation**: Updated code to use `GET_OPTION(acds_server)` and `GET_OPTION(acds_port)` instead of hardcoded defaults.
- **Benefits**: Clients can now connect to ACDS servers on non-standard addresses/ports for testing and deployment flexibility.

### 4. Socket Timeouts for ACIP Client ✅
- **File**: `lib/network/acip/client.c:61`
- **Status**: COMPLETED
- **Description**: Implemented cross-platform socket timeouts using `SO_RCVTIMEO` and `SO_SNDTIMEO`.
- **Implementation**:
  - Windows: Uses `DWORD` timeout in milliseconds
  - POSIX: Uses `struct timeval` for seconds and microseconds conversion
- **Benefits**: Network operations now respect configured timeout values, preventing indefinite hangs.

---

## Future Enhancement TODOs

### Network & Protocol

#### ACDS Crypto Handshake
- **File**: `src/acds/server.c:608`
- **Priority**: Medium
- **Description**: Implement cryptographic handshake for ACDS server connections
- **Rationale**: Currently ACDS protocol supports plain TCP without encryption; future versions should add end-to-end encryption
- **Effort**: Significant - requires designing secure key exchange protocol
- **Blocking**: Not critical for current ACDS functionality (session discovery service)

#### mDNS Advertisement
- **File**: `lib/network/mdns/mdns.c:86, 100`
- **Priority**: Medium
- **Description**: Implement actual mDNS advertisement/unadvertisement functionality
- **Current Status**: Stub implementations only (TODO comments)
- **Rationale**: Enable local network service discovery without ACDS
- **Dependencies**: Requires mdns library integration

### Security & Cryptography

#### Password Hashing with Argon2id
- **File**: `src/server/main.c:1045`
- **Priority**: High
- **Description**: Hash passwords with Argon2id instead of storing plaintext
- **Current Status**: Passwords stored directly in session config
- **Rationale**: Improved security for password-protected sessions
- **Implementation Notes**:
  - Use libsodium's `crypto_pwhash_argon2id13()` function
  - Store hash in session database instead of plaintext
  - Update authentication verification to use `crypto_pwhash_str_verify()`

#### GPG Key Parsing
- **Files**:
  - `lib/crypto/gpg/gpg_keys.c:89` - Binary GPG key parsing
  - `lib/crypto/gpg/gpg_keys.c:144` - GPG fingerprint extraction
  - `lib/crypto/gpg/gpg_keys.c:157` - GPG key ID extraction
  - `lib/crypto/gpg/gpg_keys.c:314` - GPG key comment extraction
- **Priority**: Medium
- **Description**: Implement proper binary GPG key parsing instead of relying on gpg command-line tool
- **Rationale**: Reduce dependency on system GPG installation for key operations
- **Complexity**: Requires understanding GPG packet format

#### GPG Key Validation
- **File**: `lib/crypto/keys_validation.c:194, 345`
- **Priority**: Low
- **Description**: Add more comprehensive GPG key validation and pattern detection
- **Current Status**: Basic validation implemented; room for improvement
- **Examples**: Additional fingerprint formats, key expiry edge cases

### Video & SIMD Optimization

#### Fix SIMD Algorithm Edge Cases
- **File**: `tests/integration/ascii_simd_integration_test.c:847`
- **Priority**: Medium
- **Description**: Fix SIMD algorithm to match scalar output for edge case image dimensions
- **Current Status**: SIMD produces different output for certain image sizes
- **Testing**: Edge case test exists but SIMD implementation needs fixing

#### Clear SIMD Caches
- **File**: `tests/integration/ascii_simd_integration_test.c:1887`
- **Priority**: Low
- **Description**: Implement `clear_all_simd_caches()` function for test isolation
- **Rationale**: Ensure test independence by clearing CPU caches between runs

#### Fix AVX2 Color Implementation
- **File**: `lib/video/simd/ascii_simd_color.c:380`
- **Priority**: Medium
- **Description**: Fix AVX2 ANSI color implementation - currently has vertical stripe artifacts and dimness
- **Current Status**: Disabled in favor of scalar implementation
- **Rationale**: SIMD color rendering performance optimization for high-color terminals

#### Update SIMD Benchmark
- **File**: `lib/video/simd/ascii_simd.c:435`
- **Priority**: Low
- **Description**: Update benchmark to use custom palette testing
- **Rationale**: Ensure benchmarks cover all rendering modes

#### NEON Vectorized ANSI Sequence Generation
- **File**: `lib/video/simd/neon.c:238`
- **Priority**: Low
- **Description**: Implement true NEON vectorized ANSI escape sequence generation using TBL + compaction
- **Rationale**: ARM NEON optimization for ASCII art rendering on ARM devices
- **Note**: This is an advanced optimization with specialized ARM instructions

### Client Features (Multi-Party WebRTC)

#### Multi-Party Session Management
- **Files**:
  - `src/client/main.c:186` - Multi-party session handling
  - `src/client/main.c:193` - Hash table for participant tracking
- **Priority**: High
- **Description**: Implement proper multi-party session management for WebRTC
- **Status**: Skeleton code exists; full implementation needed
- **Scope**: Manage multiple participants, track participant IDs, handle joins/leaves

#### STUN/TURN Server Configuration
- **Files**:
  - `src/client/main.c:757` - Get STUN/TURN from ACDS
  - `src/client/main.c:768` - Configure TURN servers
- **Priority**: Medium
- **Description**: Integrate STUN/TURN server configuration from ACDS responses
- **Rationale**: NAT traversal for WebRTC connections in restrictive networks
- **Note**: Requires WebRTC library integration

#### Event Loop & DataChannel Handling
- **Files**:
  - `src/client/main.c:843` - Implement event loop
  - `src/client/main.c:855` - DataChannel operation
- **Priority**: High
- **Description**: Implement proper event loop handling for WebRTC DataChannel events
- **Status**: WebRTC integration incomplete
- **Blocking**: Prevents multi-party WebRTC functionality

#### Participant List from ACDS
- **File**: `src/client/main.c:807`
- **Priority**: Medium
- **Description**: Fetch current participant list from ACDS
- **Rationale**: Display active participants before joining session

#### Client Identity Provision
- **File**: `src/client/main.c:730`
- **Priority**: Medium
- **Description**: Provide Ed25519 identity for signature when required by ACDS policies
- **Rationale**: Support ACDS identity verification requirements
- **Note**: Related to `require_server_identity` option

### Platform & Audio

#### Windows Thread Priority Implementation
- **Status**: ✅ COMPLETED in this session
- See "Completed TODOs" section above

### Protocol & Transport

#### Transport Abstraction for ACDS
- **Files**:
  - `lib/network/acip/acds_handlers.c:72` - Transport abstraction for responses
  - `lib/network/acip/handlers.c:122, 482` - Transport for out-of-band messages
- **Priority**: Low
- **Description**: Implement transport abstraction for handling ACDS responses and out-of-band messages
- **Rationale**: Decouple protocol handling from transport layer (TCP/WebRTC)
- **Note**: Future-proofing for protocol extensions

#### Audio Processing Per-Channel
- **File**: `lib/network/acip/handlers.c:193`
- **Priority**: Medium
- **Description**: Implement per-channel audio processing or validate sample rate/channels
- **Rationale**: Proper audio handling for multi-channel scenarios
- **Note**: May require channel format negotiation

---

## Non-Actionable TODOs

### Documentation & Comments

#### FIXME Items in Tools
- **File**: `scripts/run-clang-tidy.py:10, 339`
- **Status**: Code works as-is; integration improvements suggested
- **Note**: Related to clang-tidy integration improvements

#### XXXX Patterns (Template Strings)
- Multiple files use `/tmp/xxx_XXXXXX` patterns for `mktemp()` compatibility
- These are intentional (POSIX standard temporary file creation)
- Not actual TODOs

#### Doxygen TODO List
- **File**: `docs/Doxyfile.in:96`
- **Status**: Intentional Doxygen configuration (generates TODO list in documentation)
- **Note**: Not a development TODO

#### README TODO Section
- **File**: `README.md:938`
- **Status**: Placeholder section for future roadmap items
- **Note**: Not specific development TODOs

#### Audio System Note
- **File**: `README.md:28`
- **Status**: Note that audio system needs work; not a specific actionable TODO
- **Note**: Known limitation

---

## Implementation Priority Guide

### Critical (Should Implement First)
1. ✅ Socket timeouts for ACIP client
2. ✅ ACDS server graceful shutdown
3. ✅ Windows audio thread priority
4. ✅ Configurable ACDS server address
5. Password hashing with Argon2id (security)

### Important (Medium Priority)
6. Multi-party WebRTC session management
7. Event loop and DataChannel handling
8. mDNS advertisement implementation
9. STUN/TURN server configuration
10. Fix SIMD algorithm edge cases

### Nice-to-Have (Low Priority)
11. GPG key binary parsing
12. GPG fingerprint extraction
13. NEON vectorized ANSI generation
14. SIMD cache clearing
15. Benchmark updates

### Blocked/Deferred
- ACDS crypto handshake (not critical for current functionality)
- Full WebRTC integration (large scope, requires additional libraries)
- Advanced SIMD color optimizations (temporarily disabled due to artifacts)

---

## Statistics

- **Total TODOs Found**: 47
- **Completed in This Session**: 4
- **Remaining TODOs**: 43
  - Actionable Enhancement TODOs: ~25
  - Non-actionable/Documentation: ~18

---

## Related Documentation

- [Cryptography Implementation](docs/crypto.md)
- [Network Protocol Specification](docs/network.md)
- [ACDS Architecture](lib/network/acip/client.h)
- [Platform Abstraction Layer](lib/platform/README.md)
