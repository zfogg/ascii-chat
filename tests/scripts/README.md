# ascii-chat Test Scripts

This directory contains test runner scripts for ascii-chat.

## Scripts

### `run-docker-tests.ps1` - Docker-Based Test Runner

PowerShell script to run tests via Docker using ctest. This is the recommended way to run tests on Windows or when you need a consistent Linux test environment.

**Usage:**
```powershell
# Run all tests
./tests/scripts/run-docker-tests.ps1

# Run specific test category
./tests/scripts/run-docker-tests.ps1 unit           # All unit tests
./tests/scripts/run-docker-tests.ps1 integration    # All integration tests
./tests/scripts/run-docker-tests.ps1 performance    # All performance tests

# Run tests matching a pattern
./tests/scripts/run-docker-tests.ps1 -Filter "buffer"

# Run a specific test
./tests/scripts/run-docker-tests.ps1 test_unit_buffer_pool

# Run clang-tidy analysis
./tests/scripts/run-docker-tests.ps1 -ClangTidy
./tests/scripts/run-docker-tests.ps1 clang-tidy lib/common.c

# Interactive shell in container
./tests/scripts/run-docker-tests.ps1 -Interactive

# Clean rebuild
./tests/scripts/run-docker-tests.ps1 -Clean
```

## Running Tests Natively with ctest

For native builds (without Docker), use ctest directly:

```bash
# Build tests
cmake --build build --target tests

# Run all tests
ctest --test-dir build --output-on-failure --parallel 0

# Run specific category using labels
ctest --test-dir build --label-regex "^unit$" --output-on-failure
ctest --test-dir build --label-regex "^integration$" --output-on-failure
ctest --test-dir build --label-regex "^performance$" --output-on-failure

# Run specific test by name pattern
ctest --test-dir build -R "buffer_pool" --output-on-failure

# Verbose output
ctest --test-dir build --output-on-failure --verbose
```

## Test Types

- **`unit`** - Unit tests for individual components
- **`integration`** - Multi-component integration tests
- **`performance`** - Performance and stress tests

## Docker Testing

Docker provides a consistent Linux environment for running tests:

```bash
# Using docker-compose directly
docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests

# Interactive shell
docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests /bin/bash

# Rebuild image
docker-compose -f tests/docker-compose.yml build --no-cache
```

## CI Integration

Tests are run automatically via GitHub Actions using ctest with JUnit XML output from Criterion:

```yaml
- name: Run Tests
  run: |
    cmake --build build --target tests
    ctest --test-dir build --output-on-failure --parallel 0
```

## Features

- **Parallel Execution**: `--parallel 0` auto-detects CPU cores
- **Label Filtering**: Use `--label-regex` to filter by test category
- **Name Filtering**: Use `-R` to filter by test name pattern
- **XML Output**: Criterion generates XML in `build/Testing/criterion-xml/`
- **Docker Support**: Consistent test environment via Docker
