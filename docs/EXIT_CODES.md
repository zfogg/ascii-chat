# ascii-chat Exit Codes

## Overview

ascii-chat uses a comprehensive exit code system to communicate detailed error information to users and scripts. Exit codes follow Unix conventions where **0 indicates success** and non-zero values indicate various types of failures.

## Exit Code Philosophy

1. **Exit codes (0-255)**: Returned to the shell when the program exits
2. **Internal error codes (negative)**: Used for function return values within library code
3. **Conversion function**: `asciichat_error_to_exit()` converts internal codes to exit codes

### Design Principles

- **0**: Success (EXIT_OK)
- **1**: General/unspecified error
- **2**: Command line usage error (following BSD conventions)
- **3-127**: Application-specific errors (grouped by category)
- **128-255**: Reserved for signal-related exits (Unix convention: 128+signal_number)

---

## Standard Exit Codes (0-2)

### EXIT_OK = 0
**Success** - Operation completed successfully

```bash
$ ascii-chat client --help
# ... prints help ...
$ echo $?
0
```

### EXIT_GENERAL_ERROR = 1
**General error** - Unspecified error occurred

Used when no more specific exit code applies.

### EXIT_USAGE_ERROR = 2
**Invalid command line usage** - Invalid arguments or options

Examples:
- Invalid option specified
- Required argument missing
- Conflicting options provided
- Invalid value for an option

```bash
$ ascii-chat server --port invalid
Invalid port number: invalid
$ echo $?
2
```

---

## Initialization Failures (3-19)

### EXIT_MEMORY_ERROR = 3
**Memory allocation failed** - Out of memory (OOM)

The system ran out of available memory. This is typically unrecoverable.

```bash
$ ascii-chat server
ERROR: Memory allocation failed: 1048576 bytes
$ echo $?
3
```

### EXIT_CONFIG_ERROR = 4
**Configuration error** - Configuration file or settings error

Reserved for future use when configuration files are implemented.

### EXIT_CRYPTO_INIT_ERROR = 5
**Cryptographic initialization failed** - libsodium or key generation failed

Examples:
- libsodium initialization failed
- Random number generator initialization failed
- Key generation failed

### EXIT_LOGGING_INIT_ERROR = 6
**Logging initialization failed** - Cannot open log file or initialize logging system

```bash
$ ascii-chat server --log-file /root/server.log
ERROR: Cannot open log file: Permission denied
$ echo $?
6
```

### EXIT_PLATFORM_INIT_ERROR = 7
**Platform initialization failed** - Platform-specific subsystem initialization failed

Examples:
- Windows socket initialization (WSAStartup) failed
- Signal handler installation failed
- Thread subsystem initialization failed

---

## Hardware/Device Errors (20-39)

### EXIT_WEBCAM_ERROR = 20
**Webcam error** - Webcam initialization or capture failed

Generic webcam error when the device cannot be opened or accessed.

```bash
$ ascii-chat client
ERROR: Failed to open webcam
On Linux, make sure:
* Your user is in the 'video' group: sudo usermod -a -G video $USER
* The camera device exists: ls /dev/video*
* No other application is using the camera
$ echo $?
20
```

### EXIT_WEBCAM_IN_USE = 21
**Webcam in use** - Webcam is already in use by another application

This is especially common on Windows, which allows only one application to access the webcam at a time.

```bash
$ ascii-chat client
ERROR: Webcam is already in use by another application
Try using --test-pattern to use a generated pattern instead
$ echo $?
21
```

**Workaround**:
```bash
$ ascii-chat client --test-pattern
# Uses generated test pattern instead of webcam
```

### EXIT_WEBCAM_PERMISSION = 22
**Webcam permission denied** - Camera permission not granted

On macOS, this occurs when camera access is denied in System Preferences.

```bash
$ ascii-chat client
ERROR: Webcam permission denied
On macOS, grant camera access in:
System Preferences > Security & Privacy > Privacy > Camera
$ echo $?
22
```

### EXIT_AUDIO_ERROR = 23
**Audio device error** - Audio initialization or I/O failed

Examples:
- No audio device found
- Audio device initialization failed
- Audio stream cannot be opened

```bash
$ ascii-chat server --audio
ERROR: Failed to initialize audio device
$ echo $?
23
```

### EXIT_AUDIO_IN_USE = 24
**Audio device in use** - Audio device is in use by another application

### EXIT_TERMINAL_ERROR = 25
**Terminal error** - Terminal initialization or capability detection failed

Examples:
- Cannot detect terminal dimensions
- Terminal doesn't support required features
- Cannot set terminal to raw mode

```bash
$ ascii-chat client < /dev/null
ERROR: Not running in a terminal
$ echo $?
25
```

---

## Network Errors (40-59)

### EXIT_NETWORK_ERROR = 40
**Network error** - General network error

Generic network error when no more specific code applies.

### EXIT_NETWORK_BIND_ERROR = 41
**Cannot bind to port** - Server cannot bind to the specified port

Most commonly occurs when:
- Port is already in use by another process
- Port number requires elevated privileges (< 1024 on Unix)
- Address is already in use

```bash
$ ascii-chat server --port 80
ERROR: Cannot bind to port 80: Permission denied
Try a port number above 1024, or run with sudo
$ echo $?
41
```

### EXIT_NETWORK_CONNECT_ERROR = 42
**Cannot connect to server** - Client cannot connect to the server

Examples:
- Server is not running
- Server address is incorrect
- Network is unreachable
- Firewall blocking connection

```bash
$ ascii-chat client --address 192.168.1.100 --port 27224
ERROR: Cannot connect to server: Connection refused
$ echo $?
42
```

### EXIT_NETWORK_TIMEOUT = 43
**Network timeout** - Network operation timed out

Examples:
- Connection attempt timed out
- Server did not respond within expected time
- Packet receive timeout

### EXIT_NETWORK_PROTOCOL_ERROR = 44
**Protocol error** - Protocol violation or incompatible version

Examples:
- Client and server versions incompatible
- Malformed packet received
- Unexpected packet type
- Protocol state machine violation

---

## Security/Crypto Errors (60-79)

### EXIT_CRYPTO_ERROR = 60
**Cryptographic error** - Cryptographic operation failed

Generic crypto error when no more specific code applies.

Examples:
- Encryption/decryption failed
- Random number generation failed
- Hash computation failed

### EXIT_CRYPTO_KEY_ERROR = 61
**Key error** - Key loading, parsing, or generation failed

Examples:
- Cannot read SSH key file
- Key file is corrupted or invalid format
- Key file is encrypted but no password provided
- Key generation failed

```bash
$ ascii-chat server --key /path/to/invalid_key
ERROR: Failed to parse SSH key file: invalid format
$ echo $?
61
```

### EXIT_CRYPTO_AUTH_FAILED = 62
**Authentication failed** - Authentication or authorization failed

Examples:
- Password incorrect
- Client key not in authorized_keys
- Signature verification failed
- Authentication challenge failed

```bash
$ ascii-chat client
ERROR: Authentication failed: incorrect password
$ echo $?
62
```

### EXIT_CRYPTO_HANDSHAKE_FAILED = 63
**Handshake failed** - Cryptographic handshake failed

Examples:
- Key exchange failed
- Diffie-Hellman failed
- Handshake timeout
- Protocol version mismatch

### EXIT_CRYPTO_VERIFICATION_FAILED = 64
**Verification failed** - Signature or key verification failed

Examples:
- Server public key doesn't match expected
- Ed25519 signature verification failed
- Certificate validation failed
- Man-in-the-middle attack detected

```bash
$ ascii-chat client --server-key expected_key.pub
ERROR: Server key verification failed
WARNING: Possible man-in-the-middle attack!
$ echo $?
64
```

---

## Runtime Errors (80-99)

### EXIT_THREAD_ERROR = 80
**Thread error** - Thread creation or management failed

Examples:
- pthread_create() failed
- Thread resource limit reached
- Cannot create thread pool
- Mutex initialization failed

### EXIT_BUFFER_ERROR = 81
**Buffer error** - Buffer allocation or overflow

Examples:
- Ringbuffer allocation failed
- Packet buffer too small
- Buffer overflow detected
- Buffer pool exhausted

### EXIT_DISPLAY_ERROR = 82
**Display error** - Display rendering or output error

Examples:
- Cannot write to stdout/stderr
- Terminal output failed
- ASCII rendering error
- Frame buffer allocation failed

### EXIT_INVALID_STATE = 83
**Invalid program state** - Program reached an invalid state

This indicates a logic error or programming bug. Should not occur in production.

### EXIT_RESOURCE_EXHAUSTED = 84
**System resources exhausted** - System resources exhausted

Examples:
- File descriptor limit reached
- Process limit reached
- Socket limit reached
- Maximum clients reached

```bash
$ ascii-chat server
ERROR: Maximum client limit reached (10/10)
$ echo $?
84
```

---

## Signal/Crash Handlers (100-127)

### EXIT_SIGNAL_INTERRUPT = 100
**Interrupted by signal** - Program interrupted by SIGINT or SIGTERM

Occurs when user presses Ctrl-C or sends SIGTERM.

```bash
$ ascii-chat server
^C
Server shutting down...
$ echo $?
100
```

### EXIT_SIGNAL_CRASH = 101
**Fatal signal** - Program terminated by fatal signal (SIGSEGV, SIGABRT, etc.)

Indicates a crash or unhandled exception. This is a bug.

### EXIT_ASSERTION_FAILED = 102
**Assertion failed** - Assertion or invariant violation

Indicates a programming error where an expected condition was not met.

---

## Reserved Exit Codes (128-255)

Exit codes 128-255 are reserved for signal-related exits following Unix conventions:
- **128 + signal_number** indicates termination by signal

Examples:
- **130** (128 + 2): Terminated by SIGINT (Ctrl-C)
- **137** (128 + 9): Terminated by SIGKILL
- **143** (128 + 15): Terminated by SIGTERM

**ascii-chat does not use these codes directly.** They are set automatically by the shell.

---

## Usage in Scripts

### Bash Example

```bash
#!/bin/bash

# Start server and check exit code
ascii-chat server &
SERVER_PID=$!

# Wait for server to initialize
sleep 1

# Start client
ascii-chat client
CLIENT_EXIT=$?

# Check client exit code
case $CLIENT_EXIT in
  0)
    echo "Client exited successfully"
    ;;
  2)
    echo "Invalid command line options"
    exit 1
    ;;
  20)
    echo "Webcam error - trying test pattern mode"
    ascii-chat client --test-pattern
    ;;
  42)
    echo "Cannot connect to server"
    exit 1
    ;;
  62)
    echo "Authentication failed - check password"
    exit 1
    ;;
  *)
    echo "Unexpected error: $CLIENT_EXIT"
    echo "Error: $(ascii-chat client --help 2>&1 | grep EXIT_CODE_$CLIENT_EXIT)"
    exit 1
    ;;
esac

# Cleanup
kill $SERVER_PID
```

### Python Example

```python
import subprocess
import sys

# Exit code constants
EXIT_OK = 0
EXIT_WEBCAM_ERROR = 20
EXIT_WEBCAM_IN_USE = 21
EXIT_NETWORK_CONNECT_ERROR = 42

try:
    result = subprocess.run(
        ["ascii-chat client"],
        capture_output=True,
        timeout=30
    )

    if result.returncode == EXIT_OK:
        print("Client completed successfully")
    elif result.returncode == EXIT_WEBCAM_IN_USE:
        print("Webcam in use, retrying with test pattern...")
        result = subprocess.run(["ascii-chat client", "--test-pattern"])
    elif result.returncode == EXIT_NETWORK_CONNECT_ERROR:
        print("Cannot connect to server")
        sys.exit(1)
    else:
        print(f"Error: exit code {result.returncode}")
        print(result.stderr.decode())
        sys.exit(1)

except subprocess.TimeoutExpired:
    print("Client timed out")
    sys.exit(1)
```

---

## Debugging Exit Codes

### View Exit Code After Running

```bash
$ ascii-chat client --some-option
# ... program runs ...
$ echo $?
2
```

### Get Human-Readable Error String

Future versions will include a utility to decode exit codes:

```bash
$ ascii-chat-explain 62
EXIT_CRYPTO_AUTH_FAILED (62): Authentication failed
```

### Check Exit Code in C Code

```c
#include "common.h"

int main(int argc, char **argv) {
    // ... initialization ...

    asciichat_error_t err = webcam_init(0, false);
    if (err != ASCIICHAT_OK) {
        fprintf(stderr, "Webcam error: %s\n", asciichat_error_string(err));
        asciichat_exit_code_t exit_code = asciichat_error_to_exit(err);
        return exit_code;
    }

    return EXIT_OK;
}
```

---

## Exit Code Summary Table

| Code | Name | Category | Description |
|------|------|----------|-------------|
| 0 | EXIT_OK | Success | Operation completed successfully |
| 1 | EXIT_GENERAL_ERROR | Standard | Unspecified error |
| 2 | EXIT_USAGE_ERROR | Standard | Invalid command line usage |
| 3 | EXIT_MEMORY_ERROR | Init | Memory allocation failed (OOM) |
| 4 | EXIT_CONFIG_ERROR | Init | Configuration error |
| 5 | EXIT_CRYPTO_INIT_ERROR | Init | Crypto initialization failed |
| 6 | EXIT_LOGGING_INIT_ERROR | Init | Logging initialization failed |
| 7 | EXIT_PLATFORM_INIT_ERROR | Init | Platform initialization failed |
| 20 | EXIT_WEBCAM_ERROR | Device | Webcam error |
| 21 | EXIT_WEBCAM_IN_USE | Device | Webcam in use by another app |
| 22 | EXIT_WEBCAM_PERMISSION | Device | Webcam permission denied |
| 23 | EXIT_AUDIO_ERROR | Device | Audio device error |
| 24 | EXIT_AUDIO_IN_USE | Device | Audio device in use |
| 25 | EXIT_TERMINAL_ERROR | Device | Terminal error |
| 40 | EXIT_NETWORK_ERROR | Network | General network error |
| 41 | EXIT_NETWORK_BIND_ERROR | Network | Cannot bind to port |
| 42 | EXIT_NETWORK_CONNECT_ERROR | Network | Cannot connect to server |
| 43 | EXIT_NETWORK_TIMEOUT | Network | Network timeout |
| 44 | EXIT_NETWORK_PROTOCOL_ERROR | Network | Protocol error |
| 60 | EXIT_CRYPTO_ERROR | Security | Cryptographic error |
| 61 | EXIT_CRYPTO_KEY_ERROR | Security | Key error |
| 62 | EXIT_CRYPTO_AUTH_FAILED | Security | Authentication failed |
| 63 | EXIT_CRYPTO_HANDSHAKE_FAILED | Security | Handshake failed |
| 64 | EXIT_CRYPTO_VERIFICATION_FAILED | Security | Verification failed |
| 80 | EXIT_THREAD_ERROR | Runtime | Thread error |
| 81 | EXIT_BUFFER_ERROR | Runtime | Buffer error |
| 82 | EXIT_DISPLAY_ERROR | Runtime | Display error |
| 83 | EXIT_INVALID_STATE | Runtime | Invalid program state |
| 84 | EXIT_RESOURCE_EXHAUSTED | Runtime | Resources exhausted |
| 100 | EXIT_SIGNAL_INTERRUPT | Signal | Interrupted by signal |
| 101 | EXIT_SIGNAL_CRASH | Signal | Fatal signal (crash) |
| 102 | EXIT_ASSERTION_FAILED | Signal | Assertion failed |

---

## Using FATAL Macros in Code

### Overview

ascii-chat provides convenient macros for fatal error handling that automatically:
1. Print a human-readable error message to stderr
2. Include the exit code and location (file:line:function)
3. Print a stack trace (debug builds only)
4. Exit with the appropriate exit code

### Available Macros

#### FATAL_ERROR(error)
Exit with an internal error code (converts to exit code automatically).

```c
#include "common.h"

int main(void) {
    asciichat_error_t err = webcam_init(0, false);
    if (err != ASCIICHAT_OK) {
        FATAL_ERROR(err);  // Converts ASCIICHAT_ERR_WEBCAM to EXIT_WEBCAM_ERROR (20)
    }
    return EXIT_OK;
}
```

**Debug output:**
```
FATAL ERROR: Webcam error
Exit code: 20 (Webcam error)
Location: src/client/main.c:123 in main()

Stack trace:
  [0] 0x00007f1234567890 in main
  [1] 0x00007f1234567891 in __libc_start_main
  [2] 0x00007f1234567892 in _start
```

**Release output:**
```
FATAL ERROR: Webcam error
Exit code: 20 (Webcam error)
Location: src/client/main.c:123 in main()
```

#### FATAL_EXIT(code)
Exit with an explicit exit code.

```c
#include "common.h"

int main(void) {
    if (bind(sockfd, ...) < 0) {
        FATAL_EXIT(EXIT_NETWORK_BIND_ERROR);
    }
    return EXIT_OK;
}
```

**Output:**
```
FATAL ERROR: Cannot bind to network port
Exit code: 41
Location: src/server/main.c:456 in main()

Stack trace:
  [0] 0x00007f1234567890 in main
  [1] 0x00007f1234567891 in __libc_start_main
```

#### FATAL(code, format, ...)
Exit with an exit code and custom printf-style message.

```c
#include "common.h"

int main(void) {
    int port = 27224;
    if (bind(sockfd, ...) < 0) {
        FATAL(EXIT_NETWORK_BIND_ERROR, "Cannot bind to port %d: %s", port, strerror(errno));
    }
    return EXIT_OK;
}
```

**Output:**
```
FATAL ERROR: Cannot bind to port 27224: Address already in use
Exit code: 41 (Cannot bind to network port)
Location: src/server/main.c:456 in main()

Stack trace:
  [0] 0x00007f1234567890 in main
  [1] 0x00007f1234567891 in __libc_start_main
```

### Macro Comparison

| Macro | Takes | Converts? | Custom Message? | Use Case |
|-------|-------|-----------|-----------------|----------|
| `FATAL_ERROR(err)` | `asciichat_error_t` | Yes | No | When you have an internal error code from lib/ |
| `FATAL_EXIT(code)` | `asciichat_exit_code_t` | No | No | When you know the exact exit code |
| `FATAL(code, ...)` | `asciichat_exit_code_t` + format | No | Yes | When you need a custom error message |

### Best Practices

1. **Use FATAL_ERROR for library errors**: When library functions return `asciichat_error_t`, use `FATAL_ERROR()` to automatically convert to the appropriate exit code.

2. **Use FATAL for user-facing errors**: When you need to explain what went wrong (e.g., "Cannot bind to port 8080"), use `FATAL()` with a custom message.

3. **Use FATAL_EXIT for simple cases**: When you just need to exit with a specific code and the standard message is sufficient.

4. **Always use in src/ code only**: Never use these macros in lib/ code - library code should return error codes instead.

### Stack Trace Behavior

- **Debug builds** (`-DDEBUG` or no `-DNDEBUG`): Full stack trace printed
- **Release builds** (`-DNDEBUG`): No stack trace (cleaner output for users)

### Complete Example

```c
// src/server/main.c
#include "common.h"
#include "options.h"
#include "network.h"

int main(int argc, char **argv) {
    // Initialize logging
    log_init("server.log", LOG_INFO);

    // Parse options - if this returns an error, it already exited
    options_init(argc, argv, false);

    // Initialize crypto
    asciichat_error_t crypto_err = crypto_init();
    if (crypto_err != ASCIICHAT_OK) {
        FATAL_ERROR(crypto_err);  // Exits with EXIT_CRYPTO_INIT_ERROR (5)
    }

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        FATAL(EXIT_NETWORK_ERROR, "Failed to create socket: %s", strerror(errno));
    }

    // Bind to port
    if (bind(sockfd, ...) < 0) {
        if (errno == EADDRINUSE) {
            FATAL(EXIT_NETWORK_BIND_ERROR,
                  "Port %d is already in use. Try a different port or stop the other process.",
                  opt_port);
        } else {
            FATAL(EXIT_NETWORK_BIND_ERROR, "Cannot bind to port %d: %s", opt_port, strerror(errno));
        }
    }

    // ... rest of server code ...

    return EXIT_OK;
}
```

### Checking Exit Codes in Shell Scripts

```bash
#!/bin/bash

ascii-chat server --port 8080
EXIT_CODE=$?

case $EXIT_CODE in
    0)
        echo "Server exited successfully"
        ;;
    5)
        echo "Crypto initialization failed"
        exit 1
        ;;
    41)
        echo "Cannot bind to port - already in use?"
        echo "Try: ascii-chat server --port 8081"
        exit 1
        ;;
    *)
        echo "Server failed with exit code: $EXIT_CODE"
        exit 1
        ;;
esac
```

---

## See Also

- **lib/common.h**: Exit code definitions, conversion functions, and FATAL macros
- **CLAUDE.md**: Development guide
- **README.md**: User guide

## Future Improvements

1. **Exit code utility**: Command-line tool to decode exit codes
2. **Structured logging**: Include exit codes in structured log output
3. **Telemetry**: Track exit code distribution for debugging
4. **Man page**: Include this information in `man ascii-chat`
