# ASCII-Chat Test Scripts

This directory contains unified test runner scripts that consolidate all the common test running patterns from the Makefile into reusable, standalone scripts.

## Scripts

### `run_tests.sh` - Main Test Runner

The comprehensive test runner that supports all test types, build configurations, and output formats.

**Usage:**
```bash
./tests/scripts/run_tests.sh [OPTIONS]
```

**Options:**
- `-t, --type TYPE` - Test type: `all`, `unit`, `integration`, `performance` (default: `all`)
- `-b, --build TYPE` - Build type: `debug`, `release`, `debug-coverage`, `release-coverage` (default: `debug`)
- `-j, --jobs N` - Number of parallel jobs (default: auto-detect CPU cores)
- `-J, --junit` - Generate JUnit XML output
- `-v, --verbose` - Verbose output
- `-c, --coverage` - Enable coverage (overrides build type)
- `-h, --help` - Show help message

**Examples:**
```bash
# Run all tests with debug build
./tests/scripts/run_tests.sh

# Run unit tests with release build
./tests/scripts/run_tests.sh -t unit -b release

# Run performance tests with JUnit output
./tests/scripts/run_tests.sh -t performance -J

# Run integration tests with coverage
./tests/scripts/run_tests.sh -c -t integration

# Run all tests with release+coverage+JUnit
./tests/scripts/run_tests.sh -b release-coverage -J
```

### `test.sh` - Simple Wrapper

A simple wrapper around `run_tests.sh` for common use cases. Defaults to running all tests with debug build.

**Usage:**
```bash
./tests/scripts/test.sh [OPTIONS]
```

All options are passed through to `run_tests.sh`.

## Build Types

- **`debug`** - Debug build with symbols, no optimization
- **`release`** - Release build with optimizations and LTO
- **`debug-coverage`** - Debug build with coverage instrumentation
- **`release-coverage`** - Release build with coverage instrumentation

## Test Types

- **`all`** - Run all test categories (unit, integration, performance)
- **`unit`** - Run only unit tests
- **`integration`** - Run only integration tests
- **`performance`** - Run only performance tests

## Features

### Automatic Test Building
If tests don't exist, the script will automatically build them using the appropriate Makefile target.

### Parallel Execution
Tests run in parallel using all available CPU cores by default. Use `-j N` to override.

### JUnit XML Output
Use `-J` or `--junit` to generate JUnit XML output compatible with CI systems like GitHub Actions.

### Comprehensive Logging
- Regular mode: Logs saved to `/tmp/test_logs.txt`
- JUnit mode: XML output saved to `junit.xml` in project root
- Verbose mode: Additional debug information with `-v`

### Error Handling
- Proper exit codes for CI integration
- Detailed error reporting
- Graceful handling of missing tests

## Integration with Makefile

These scripts replace the repetitive test running code in the Makefile. The Makefile targets now delegate to these scripts:

```makefile
# Instead of inline shell scripts, use:
test: $(TEST_EXECUTABLES)
	./tests/scripts/run_tests.sh -b debug

test-performance: $(filter $(BIN_DIR)/test_performance_%, $(TEST_EXECUTABLES))
	./tests/scripts/run_tests.sh -t performance -b debug
```

## CI Integration

The scripts are designed to work seamlessly with CI systems:

```yaml
# GitHub Actions example
- name: Run Tests
  run: ./tests/scripts/run_tests.sh -J -b release-coverage
  env:
    GENERATE_JUNIT: 1
```

## Benefits

1. **DRY Principle** - Eliminates code duplication across Makefile targets
2. **Consistency** - All tests run with the same logic and error handling
3. **Maintainability** - Single place to update test running behavior
4. **Flexibility** - Easy to add new options and features
5. **Reusability** - Can be used outside of Makefile context
6. **CI-Friendly** - Proper exit codes and JUnit XML output
