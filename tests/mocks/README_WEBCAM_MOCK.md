# Webcam Mock Usage Guide

## How the Mock Works

The webcam mock uses C preprocessor `#define` directives to override the real webcam functions with mock implementations. This happens at **compile time**, not runtime.

## Three Ways to Use the Mock

### 1. **Include-Time Mocking** (For Unit Tests)
Include the mock header before any webcam headers in your test file:

```c
// test_file.c
#define WEBCAM_MOCK_ENABLED 1
#include "tests/mocks/webcam_mock.h"  // MUST come first!
#include "os/webcam.h"                 // Now uses mocked functions
#include "../../src/client.c"          // Include source directly

// Now all webcam_* calls are mocked
```

### 2. **Compile-Time Mocking** (For Integration Tests)
Compile client.c with a flag to use mocks:

```c
// In client.c or a wrapper:
#ifdef USE_WEBCAM_MOCK
  #include "tests/mocks/webcam_mock.h"
#endif
#include "os/webcam.h"

// Rest of client.c code...
```

Build with: `gcc -DUSE_WEBCAM_MOCK ...`

### 3. **Link-Time Mocking** (For Full Integration)
Build a separate test binary that links mock instead of real webcam:

```cmake
add_executable(client-with-mock
    src/client.c
    tests/mocks/webcam_mock.c  # Link mock implementation
    # Exclude: lib/os/webcam.c   # Don't link real implementation
)
```

## How It Works with client.c

When you test `src/client.c` with the mock:

1. **The mock header defines macros** that replace function names:
   ```c
   #define webcam_init mock_webcam_init
   #define webcam_read mock_webcam_read
   ```

2. **When client.c is compiled**, every call to `webcam_init()` becomes `mock_webcam_init()`

3. **The mock implementation** provides these mock functions that return test data

## Example: Testing client.c Video Capture

```c
// test_client_video.c
#include <criterion/criterion.h>

// Enable mock BEFORE including anything else
#define WEBCAM_MOCK_ENABLED 1
#include "tests/mocks/webcam_mock.h"

// Now include client internals
#define TEST_CLIENT_INTERNALS
#include "../../src/client.c"

Test(client, capture_with_mock) {
    // Configure mock
    mock_webcam_set_test_pattern(true);
    mock_webcam_set_dimensions(640, 480);

    // This calls mock_webcam_init_context internally
    webcam_context_t *ctx = NULL;
    webcam_init_context(&ctx, 0);  // Actually calls mock!

    // Capture frame - uses mock
    image_t *frame = webcam_read_context(ctx);
    cr_assert_not_null(frame);

    webcam_cleanup_context(ctx);
}
```

## Building Tests

### Option 1: Direct Include (Simple)
```bash
gcc -o test_client \
    tests/integration/client_mock_test.c \
    tests/mocks/webcam_mock.c \
    -lcriterion
```

### Option 2: CMake Target (Recommended)
```cmake
add_executable(test_client_mock
    tests/integration/client_mock_test.c
    tests/mocks/webcam_mock.c
    ${LIB_SRCS_WITHOUT_WEBCAM}
)

target_compile_definitions(test_client_mock PRIVATE
    WEBCAM_MOCK_ENABLED=1
)
```

### Option 3: Conditional Compilation in client.c
Add to top of client.c:
```c
#ifdef USE_WEBCAM_MOCK
  #include "tests/mocks/webcam_mock.h"
#endif
#include "os/webcam.h"
```

Then build test version:
```bash
gcc -DUSE_WEBCAM_MOCK -o client_test src/client.c tests/mocks/webcam_mock.c ...
```

## Docker/CI Usage

In Docker or CI environments, set environment variable:
```bash
export WEBCAM_MOCK=1
export WEBCAM_MOCK_VIDEO=/path/to/test/video.mp4
```

Or compile with mock by default for test builds:
```dockerfile
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DUSE_WEBCAM_MOCK=ON \
    && cmake --build build
```

## Important Notes

1. **Order matters!** The mock header MUST be included before webcam.h
2. **It's compile-time**, not runtime - you need to rebuild to switch
3. **The mock is transparent** - client.c code doesn't know it's mocked
4. **Falls back gracefully** - if video file missing, uses test pattern

## Test Patterns Available

- **Test Pattern**: Moving gradients and shapes (default)
- **Video File**: Play any MP4/AVI file in loop
- **Custom Dimensions**: Set any resolution for testing

Configure before init:
```c
mock_webcam_set_test_pattern(true);
mock_webcam_set_dimensions(1920, 1080);
// OR
mock_webcam_set_video_file("tests/fixtures/bad_apple.mp4");
```