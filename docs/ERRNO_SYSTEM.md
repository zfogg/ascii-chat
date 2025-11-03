# ascii-chat Error Number System

## Overview

The ascii-chat Error Number System (`asciichat_errno`) is a comprehensive error handling system that provides:

- **Thread-local error context** with automatic file/line/function capture
- **Stack trace capture** in debug builds
- **System error integration** (errno support)
- **Automatic error logging** with context
- **Enhanced debugging** with rich error information
- **Error statistics and monitoring**
- **Thread-safe error propagation**

## Quick Start

### In Library Code (lib/)

```c
#include "errno.h"

// Basic error setting
asciichat_error_t my_function(void) {
    if (malloc(1024) == NULL) {
        SET_ERRNO(ERROR_MEMORY);
        return ERROR_MEMORY;
    }
    return ASCIICHAT_OK;
}

// Error with custom context
asciichat_error_t bind_port(int port) {
    if (bind(sockfd, ...) < 0) {
        SET_ERRNO(ERROR_NETWORK_BIND, "Cannot bind to port %d", port);
        return ERROR_NETWORK_BIND;
    }
    return ASCIICHAT_OK;
}

// Error with system error context
asciichat_error_t open_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        SET_ERRNO_SYS(ERROR_CONFIG, errno);
        return ERROR_CONFIG;
    }
    fclose(f);
    return ASCIICHAT_OK;
}

// Error with automatic logging
asciichat_error_t allocate_memory(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        SET_ERRNO_AND_LOG(ERROR_MEMORY, error, "Failed to allocate %zu bytes", size);
        return ERROR_MEMORY;
    }
    free(ptr);
    return ASCIICHAT_OK;
}
```

### In Application Code (src/)

```c
#include "errno.h"

// Check for library errors with full context
asciichat_error_t init_server(int port) {
    asciichat_error_t err = bind_port(port);
    if (err != ASCIICHAT_OK) {
        asciichat_error_context_t err_ctx;
        if (HAS_ERRNO(&err_ctx)) {
            FATAL(err, "Server initialization failed: %s", err_ctx.context_message);
        } else {
            FATAL_ERROR(err);
        }
    }
    return ASCIICHAT_OK;
}

// Automatic context detection
asciichat_error_t load_config(const char *filename) {
    asciichat_error_t err = open_file(filename);
    if (err != ASCIICHAT_OK) {
        FATAL_AUTO_CONTEXT(err, "Failed to load config: %s", filename);
    }
    return ASCIICHAT_OK;
}
```

## API Reference

### Library Error Setting Macros

#### `SET_ERRNO(code)`
Set error code with automatic file/line/function capture.

```c
SET_ERRNO(ERROR_MEMORY);
```

#### `SET_ERRNO(code, context_msg, ...)`
Set error code with custom context message.

```c
SET_ERRNO(ERROR_NETWORK_BIND, "Cannot bind to port %d", port);
```

#### `SET_ERRNO_SYS(code, sys_errno)`
Set error code with system error context.

```c
SET_ERRNO_SYS(ERROR_CONFIG, errno);
```

#### `SET_ERRNO_SYS_MSG(code, sys_errno, context_msg, ...)`
Set error code with both system error and custom context.

```c
SET_ERRNO_SYS_MSG(ERROR_CONFIG, errno, "Failed to open config: %s", path);
```

### Error Logging Integration Macros

#### `SET_ERRNO_AND_LOG(code, level, ...)`
Set error code and log it automatically.

```c
SET_ERRNO_AND_LOG(ERROR_MEMORY, error, "Failed to allocate %zu bytes", size);
```

#### `SET_ERRNO_SYS_AND_LOG(code, sys_errno, level, ...)`
Set error code with system error and log it automatically.

```c
SET_ERRNO_SYS_AND_LOG(ERROR_NETWORK_BIND, errno, error, "Cannot bind to port %d", port);
```

### Application Error Checking Macros

#### `HAS_ERRNO(var)`
Check if an error occurred and get full context.

```c
asciichat_error_context_t err_ctx;
if (HAS_ERRNO(&err_ctx)) {
    // Handle error with full context
}
```

#### `CLEAR_ERRNO()`
Clear the current error state.

```c
CLEAR_ERRNO();
```

#### `GET_ERRNO()`
Get current error code (0 if no error).

```c
asciichat_error_t err = GET_ERRNO();
```

### Debug Utilities

#### `PRINT_ERRNO_CONTEXT(context)`
Print full error context including stack trace.

```c
asciichat_error_context_t err_ctx;
if (HAS_ERRNO(&err_ctx)) {
    PRINT_ERRNO_CONTEXT(&err_ctx);
}
```

#### `ASSERT_NO_ERRNO()`
Assert that no error occurred.

```c
ASSERT_NO_ERRNO(); // Will abort if there's an error
```

#### `PRINT_ERRNO_IF_ERROR()`
Print current error context if any error exists.

```c
PRINT_ERRNO_IF_ERROR();
```

## Error Context Structure

```c
typedef struct {
    asciichat_error_t code;        // Error code
    const char *file;                     // File where error occurred
    int line;                             // Line number
    const char *function;                 // Function name
    char *context_message;                // Custom context message
    uint64_t timestamp;                   // Microseconds since epoch
    int system_errno;                     // System errno value
    void *stack_trace[32];                // Stack trace (debug builds)
    int stack_depth;                      // Number of frames captured
    bool has_system_error;                // Whether system_errno is valid
} asciichat_error_context_t;
```

## Error Statistics and Monitoring

### Initialize Statistics
```c
asciichat_error_stats_init();
```

### Record Error
```c
asciichat_error_stats_record(ERROR_MEMORY);
```

### Print Statistics
```c
asciichat_error_stats_print();
```

### Get Statistics
```c
asciichat_error_stats_t stats = asciichat_error_stats_get();
printf("Total errors: %llu\n", (unsigned long long)stats.total_errors);
```

## Thread-Safe Error Propagation

### Set Thread Error
```c
asciichat_set_thread_error(thread_id, ERROR_THREAD);
```

### Get Thread Error
```c
asciichat_error_t err = asciichat_get_thread_error(thread_id);
```

### Clear Thread Error
```c
asciichat_clear_thread_error(thread_id);
```

## Error Context Printing

### Print to stderr
```c
asciichat_print_error_context(&err_ctx);
```

### Print to specific file
```c
FILE *log_file = fopen("error.log", "w");
asciichat_print_error_context_to_file(log_file, &err_ctx);
fclose(log_file);
```

## Example Output

### Basic Error Output
```
FATAL ERROR: Cannot bind to port 8080: Address already in use
Exit code: 41 (Cannot bind to network port)
Location: src/server/main.c:456 in main()

Library Error Context:
  Error occurred in: lib/network.c:123 in network_bind()
  Context: Cannot bind to port 8080
  System error: Address already in use (98)
  Timestamp: 2024-01-15 14:30:25.123456

Stack trace from library error:
  [0] 0x00007f1234567890 in network_bind (lib/network.c:123)
  [1] 0x00007f1234567891 in server_init (src/server/main.c:456)
  [2] 0x00007f1234567892 in main (src/server/main.c:789)
```

### Error Statistics Output
```
=== ascii-chat Error Statistics ===
Total errors: 15
Last error: 2024-01-15 14:30:25 (code 41)

Error breakdown:
  41 (Cannot bind to network port): 5
  60 (Cryptographic operation failed): 3
  80 (Thread error): 2
  81 (Buffer error): 5
```

## Best Practices

### In Library Code
1. **Always use SET_ERRNO macros** when errors occur
2. **Provide meaningful context** with SET_ERRNO
3. **Capture system errors** with SET_ERRNO_SYS
4. **Use logging integration** for important errors
5. **Return appropriate error codes**

### In Application Code
1. **Check for library errors** with HAS_ERRNO
2. **Use enhanced FATAL macros** for better error reporting
3. **Handle errors gracefully** when possible
4. **Use FATAL_AUTO_CONTEXT** for automatic context detection

### Debug Development
1. **Use PRINT_ERRNO_CONTEXT** for debugging
2. **Use ASSERT_NO_ERRNO** for invariants
3. **Enable error statistics** for monitoring
4. **Use thread-safe error handling** in multi-threaded code

## Migration Guide

### From Current Error Handling
```c
// Old way
if (malloc(size) == NULL) {
    log_error("Memory allocation failed");
    return ERROR_MEMORY;
}

// New way
if (malloc(size) == NULL) {
    SET_ERRNO_AND_LOG(ERROR_MEMORY, error, "Memory allocation failed");
    return ERROR_MEMORY;
}
```

### From FATAL Macros
```c
// Old way
asciichat_error_t err = network_bind(sockfd, port);
if (err != ASCIICHAT_OK) {
    FATAL_ERROR(err);
}

// New way
asciichat_error_t err = network_bind(sockfd, port);
if (err != ASCIICHAT_OK) {
    FATAL_AUTO_CONTEXT(err, "Failed to bind to port %d", port);
}
```

## Performance Considerations

- **Thread-local storage** is used for error context (minimal overhead)
- **Stack trace capture** only occurs in debug builds
- **Error statistics** are lightweight and thread-safe
- **Memory allocation** for context messages is minimal
- **System calls** are only made when necessary

## Thread Safety

- **Thread-local storage** ensures no conflicts between threads
- **Error statistics** are thread-safe
- **Thread-specific error handling** is available
- **No global state** that could cause race conditions

## Debug vs Release Builds

### Debug Builds
- Full stack trace capture
- Enhanced error context printing
- Debug utilities available
- Detailed error information

### Release Builds
- No stack trace capture
- Minimal error context
- Optimized performance
- Clean error messages

## Integration with Existing Code

The `asciichat_errno` system is designed to work alongside your existing error handling:

- **Backward compatible** with current error codes
- **Enhances existing FATAL macros** with library context
- **Works with existing logging system**
- **No breaking changes** to current API

## Troubleshooting

### Common Issues

1. **Include the header**: Make sure to `#include "errno.h"`
2. **Initialize logging**: Call `log_init()` before using logging macros
3. **Check thread safety**: Use thread-specific functions in multi-threaded code
4. **Memory management**: Context messages are automatically freed

### Debug Tips

1. **Use PRINT_ERRNO_CONTEXT** to see full error information
2. **Enable error statistics** to monitor error patterns
3. **Check HAS_ERRNO** before using error context
4. **Use ASSERT_NO_ERRNO** to catch unexpected errors

## Future Enhancements

- **Error recovery suggestions** based on error type
- **Error correlation** across multiple threads
- **Performance metrics** for error handling
- **Integration with external monitoring** systems
