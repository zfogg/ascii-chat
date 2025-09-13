# ASCII-Chat Testing Notes

## Test Framework Status (September 2025)

### ✅ Exit Codes Working Correctly
The Makefile test targets (`make test`, `make test-unit`, `make test-integration`) now correctly return non-zero exit codes when tests fail. This ensures CI/CD pipelines and build scripts can properly detect test failures.

### ⚠️ macOS Criterion Framework Limitation

**Issue**: All Criterion-based tests fail on macOS with "Protocol error" and abort trap (exit code 134).

**Root Cause**: The Criterion test framework (v2.4.2) attempts to spawn child processes for test isolation, but macOS security features (System Integrity Protection, code signing requirements, etc.) prevent this from working correctly.

**Error Message**:
```
criterion: Could not spawn test instance: Protocol error
/bin/sh: line 1: [PID] Abort trap: 6
```

**Affected Systems**: macOS 13+ with enhanced security features enabled.

### Workarounds

1. **Run tests on Linux**: The tests work correctly on Linux systems or in Docker containers.

2. **Use the test wrapper script**: `./test_wrapper.sh` provides better visibility into which tests exist and would run if the framework worked.

3. **Manual testing**: The main binaries (`./bin/server` and `./bin/client`) can be tested manually with various options.

### Test Coverage

The project includes comprehensive test coverage for:
- **Unit Tests** (11 test suites, ~200+ individual tests):
  - Audio system and ringbuffers
  - Buffer pool management
  - Packet queues and networking
  - Hash tables
  - Cryptography
  - SIMD optimizations
  - Common utilities

- **Integration Tests** (3 test suites):
  - SIMD vs scalar performance comparison
  - Crypto + network integration
  - Architecture-specific optimizations

### Future Solutions

1. Consider migrating to a different test framework that works better with macOS security
2. Set up CI/CD to run tests in Linux containers
3. Implement custom test runner that doesn't require process spawning

## Running Tests

```bash
# Build and run all tests (will fail on macOS but exit codes work)
make test

# Run specific test categories
make test-unit
make test-integration
make test-performance

# Use wrapper script for better output on macOS
./test_wrapper.sh

# List tests in a specific binary (works on macOS)
./bin/test_unit_audio_test --list
```

## Exit Code Behavior

- Exit code 0: All tests passed
- Exit code 1: Test execution failed (caught by Makefile)
- Exit code 134: Abort trap on macOS (Criterion framework issue)
- Exit code 2: Makefile-level test failure (properly propagated)
