# ASCII-Chat Testing Framework

This project uses [Criterion](https://github.com/Snaipe/Criterion) as its C testing framework. All tests are automatically built and can be run individually or as complete test suites.

## Quick Start

```bash
# Run all tests
make test

# Run only unit tests
make test-unit

# Run only integration tests
make test-integration

# Clean and rebuild tests
make clean && make test
```

## Test Categories

### Unit Tests (`tests/unit/`)
- **common_test.c**: Memory allocation, error codes, utility macros
- **crypto_test.c**: X25519 key exchange, XSalsa20+Poly1305 encryption/decryption
- **logging_test.c**: All logging functions, format strings, edge cases

### Integration Tests (`tests/integration/`)
- **crypto_network_test.c**: End-to-end encrypted client-server communication

## Writing New Tests

### Test File Structure
```c
#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "common.h"  // Use simple names - Makefile provides -I flags

TestSuite(your_module_name);

Test(your_module_name, test_function_name) {
    // Your test code here
    cr_assert(condition, "Failure message");
}
```

### Parameterized Test Requirements (CRITICAL)

**⚠️ WARNING**: Parameterized tests have special memory requirements!

When using Criterion's parameterized tests (`ParameterizedTest`), any **pointers** in your test parameter structures require special Criterion allocators (`cr_malloc`, `cr_calloc`, `cr_realloc`, `cr_free`). Using regular `malloc`/`free` or string literals is **undefined behavior** and will crash.

**Best Practice for This Project**: Use fixed-size char arrays instead of pointers to avoid needing allocation code:

```c
// ✅ RECOMMENDED - Use char arrays (no allocation needed)
typedef struct {
  char input[256];           // Fixed-size array
  char expected[256];        // Fixed-size array
  int expected_result;
  char description[64];      // Fixed-size array
} test_case_t;

static test_case_t test_cases[] = {
    {"input1", "expected1", 0, "test case 1"},
    {"input2", "expected2", 0, "test case 2"},
};

ParameterizedTestParameters(suite, test_name) {
  return cr_make_param_array(test_case_t, test_cases,
                             sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(test_case_t *tc, suite, test_name) {
  // Test code using tc->input, tc->expected, etc.
}
```

```c
// ⚠️ ALTERNATIVE - Pointers with cr_malloc (more code, rarely needed)
typedef struct {
  const char *input;
  const char *expected;
  int expected_result;
  const char *description;
} test_case_t;

ParameterizedTestParameters(suite, test_name) {
  static test_case_t *cases = NULL;
  if (!cases) {
    size_t count = 2;
    cases = cr_malloc(count * sizeof(test_case_t));
    cases[0] = (test_case_t){
      .input = cr_strdup("input1"),
      .expected = cr_strdup("expected1"),
      .expected_result = 0,
      .description = cr_strdup("test case 1")
    };
    // ... more cases
  }
  return cr_make_param_array(test_case_t, cases, 2);
}
```

```c
// ❌ WRONG - String literals with pointers will crash!
typedef struct {
  const char *input;         // Pointer to string literal - CRASHES!
  const char *expected;      // Pointer to string literal - CRASHES!
  int expected_result;
  const char *description;   // Pointer to string literal - CRASHES!
} test_case_t;

static test_case_t test_cases[] = {
    {"input1", "expected1", 0, "test"},  // String literals - UNDEFINED BEHAVIOR!
};
```

**Why**: Criterion copies parameter arrays and requires all dynamic memory to use its allocators. String literals and regular heap allocations violate this requirement.

**Reference**: [Criterion Parameterized Tests Docs](https://criterion.readthedocs.io/en/master/parameterized.html)

### Important Patterns

#### Memory Allocation
```c
// Always use SAFE_MALLOC with 3 parameters
void *ptr;
SAFE_MALLOC(ptr, 1024, void*);
cr_assert_not_null(ptr, "Allocation should succeed");

// Clean up
free(ptr);
```

#### Assertions
```c
// Basic assertions
cr_assert(condition, "Message");
cr_assert_not_null(ptr, "Pointer should be valid");
cr_assert_eq(actual, expected, "Values should match");

// Boolean conditions
cr_assert(result == true, "Should be true");
cr_assert(condition == false, "Should be false");
// Note: Avoid cr_assert(!condition) - causes preprocessor issues
```

#### Error Testing
```c
// Use correct error constants
cr_assert_eq(error_code, ASCIICHAT_ERR_NETWORK, "Should be network error");
cr_assert_neq(result, ASCIICHAT_ERR_MALLOC, "Should not be memory error");
```

### File Naming Convention
- Unit tests: `tests/unit/module_test.c`
- Integration tests: `tests/integration/feature_test.c`
- Test executables: `bin/test_unit_module_test`, `bin/test_integration_feature_test`

## Test Categories Explained

### Unit Tests
Test individual functions and modules in isolation:
- Memory management (SAFE_MALLOC, SAFE_REALLOC, SAFE_CALLOC)
- Cryptographic functions (key generation, encryption, decryption)
- Logging system (all log levels, format strings, edge cases)
- Utility functions and macros

### Integration Tests
Test interactions between multiple components:
- Client-server communication with encryption
- Network protocol with packet handling
- Multi-threaded scenarios
- End-to-end data flows

## Running Individual Tests

```bash
# Run specific test executable
./bin/test_unit_crypto_test
./bin/test_integration_crypto_network_test

# Run with verbose output
./bin/test_unit_crypto_test --verbose

# List available tests
./bin/test_unit_crypto_test --list
```

## Debugging Tests

### Common Issues

#### Compilation Errors
```bash
# Check include paths are correct
# Use "common.h" not "../../common.h"
# Makefile provides -I lib -I src flags
```

#### Memory Errors
```bash
# Build with comprehensive sanitizers
make clean && make debug && make test

# Or build without sanitizers for faster iteration
make clean && make dev && make test
```

#### Assertion Failures
```bash
# Use proper assertion syntax
cr_assert(result == expected, "Description");
cr_assert_eq(actual, expected, "Should match");
cr_assert_not_null(ptr, "Should be valid");
```

### Debug Output
```c
// Add debug output to tests
log_debug("Test checkpoint: value=%d", test_value);

// Or use Criterion's messaging
cr_log_info("Debug info: %s", debug_string);
```

## Test Coverage

Current test coverage includes:
- ✅ Memory allocation and management
- ✅ Cryptographic operations (X25519, XSalsa20, Poly1305)
- ✅ Error handling and error codes
- ✅ Logging system functionality
- ✅ Network protocol with encryption
- ✅ Client-server communication

## Best Practices

### Test Design
1. **One concept per test**: Each test should verify one specific behavior
2. **Clear test names**: `test_crypto_encrypt_decrypt_roundtrip` vs `test1`
3. **Descriptive assertions**: Include helpful failure messages
4. **Clean up resources**: Free allocated memory, close files/sockets

### Code Quality
1. **Follow project patterns**: Use SAFE_MALLOC, log_debug(), etc.
2. **Handle edge cases**: Test with NULL pointers, zero sizes, invalid data
3. **Test error conditions**: Verify functions fail appropriately
4. **Use realistic data**: Test with actual-sized buffers and real-world inputs

### Performance
1. **Keep tests fast**: Unit tests should run in milliseconds
2. **Minimize setup**: Only create what's needed for the test
3. **Use timeouts**: For integration tests that might hang

## Continuous Integration

The test framework is designed to integrate with CI systems:

```bash
# CI-friendly test execution
make clean && make test
echo "Exit code: $?"

# Generate test reports (if needed)
./bin/test_unit_crypto_test --tap > crypto_results.tap
```

## Adding New Test Suites

1. Create test file in appropriate directory:
   ```
   tests/unit/new_module_test.c
   tests/integration/new_feature_test.c
   ```

2. Follow the standard structure with TestSuite() declaration

3. Tests will automatically be discovered and built by the Makefile

4. Run `make test` to verify new tests execute properly

The testing framework provides colored output by default through Criterion, comprehensive error reporting, and integrates seamlessly with the project's build system and development workflow.