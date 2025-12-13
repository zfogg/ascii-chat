# =============================================================================
# ascii-chat Test Macros
# =============================================================================
# Provides the _ascii_chat_test macro for creating Criterion-based test targets.
# This mirrors CMake's test creation logic with proper naming conventions.
# =============================================================================

load("@rules_cc//cc:defs.bzl", "cc_test")

# =============================================================================
# Common Test Configuration
# =============================================================================

# Platform-specific defines (must match lib/BUILD.bazel and src/BUILD.bazel)
PLATFORM_DEFINES = select({
    "//bazel/toolchains:linux": ["_GNU_SOURCE"],
    "//bazel/toolchains:macos": ["_DARWIN_C_SOURCE"],
    "//conditions:default": [],
})

# Build mode defines for tests
BUILD_MODE_DEFINES = select({
    "//bazel/toolchains:debug": ["DEBUG_LOCKS", "DEBUG_MEMORY"],
    "//bazel/toolchains:release": ["NDEBUG"],
    "//conditions:default": ["NDEBUG"],
})

COMMON_TEST_COPTS = [
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-std=c23",
    # Disable sanitizers for tests - third-party libraries (BearSSL, tomlc17)
    # trigger UBSan/ASan errors that are not actionable
    "-fno-sanitize=all",
]

COMMON_TEST_DEFINES = PLATFORM_DEFINES + BUILD_MODE_DEFINES + [
    "CRITERION_TEST=1",
    "__BAZEL_BUILD__",
    "TESTING=1",
]

# All library deps needed by tests (order matters for linking)
COMMON_TEST_DEPS = [
    "//lib:test-helpers",
    "//lib:core",
    "//lib:util",
    "//lib:platform",
    "//lib:data-structures",
    "//lib:network",
    "//lib:crypto",
    "//lib:audio",
    "//lib:video",
    "//lib:simd",
]

# Criterion library (system-installed)
# Note: On Linux/macOS, install with package manager (apt install libcriterion-dev, brew install criterion)
CRITERION_LINKOPTS = select({
    "@platforms//os:linux": ["-lcriterion", "-lffi"],
    "@platforms//os:macos": [
        "-L/opt/homebrew/opt/criterion/lib",
        "-L/usr/local/opt/criterion/lib",
        "-L/opt/homebrew/lib",
        "-L/usr/local/lib",
        "-lcriterion",
    ],
    "//conditions:default": [],
})

# =============================================================================
# Test Macro
# =============================================================================

# Sanitizer runtime suppression for third-party libraries
# These libraries have known UBSan/ASan issues that are not actionable:
# - BearSSL: unsigned negation in pemdec.c
# - tomlc17: memory leaks in pool allocator
# - libsodium: various benign issues
SANITIZER_SUPPRESSIONS = {
    # Suppress memory leak detection entirely - third-party libs leak
    "LSAN_OPTIONS": "detect_leaks=0",
    # Suppress ASan leak detection
    "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=0",
    # Continue on UBSan errors (third-party libs have UB that's harmless)
    "UBSAN_OPTIONS": "halt_on_error=0:print_stacktrace=0:suppressions=",
}

def ascii_chat_test(
        name,
        src,
        size = "small",
        tags = [],
        extra_deps = [],
        extra_copts = [],
        extra_defines = [],
        extra_env = {},
        **kwargs):
    """Creates a Criterion-based test target.

    This macro mirrors CMake's test creation logic with proper naming conventions
    and standard configuration for the ascii-chat test suite.

    Args:
        name: Test target name (e.g., "buffer_pool_test")
        src: Source file path relative to tests/ (e.g., "unit/buffer_pool_test.c")
        size: Bazel test size (small/medium/large/enormous)
              - small: 60s timeout (default for unit tests)
              - medium: 300s timeout (for integration tests)
              - large: 900s timeout (for complex integration tests)
        tags: Additional test tags (e.g., ["unit"], ["integration", "crypto"])
        extra_deps: Additional dependencies beyond COMMON_TEST_DEPS
        extra_copts: Additional compiler options
        extra_defines: Additional preprocessor defines
        extra_env: Additional environment variables
        **kwargs: Additional arguments passed to cc_test
    """
    # Add "local" tag to all Criterion tests - Criterion uses boxfort for subprocess
    # isolation which doesn't work in Bazel's sandboxed environment. The "local" tag
    # tells Bazel to run these tests without sandboxing.
    all_tags = ["local"] + tags

    # Merge sanitizer suppressions with any extra env vars
    test_env = {}
    test_env.update(SANITIZER_SUPPRESSIONS)
    test_env.update(extra_env)

    cc_test(
        name = name,
        srcs = [src],
        copts = COMMON_TEST_COPTS + extra_copts,
        local_defines = COMMON_TEST_DEFINES + extra_defines,
        linkopts = CRITERION_LINKOPTS,
        deps = COMMON_TEST_DEPS + extra_deps,
        size = size,
        tags = all_tags,
        env = test_env,
        **kwargs
    )
