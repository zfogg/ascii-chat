# =============================================================================
# SIMD Auto-Detection Repository Rule for ascii-chat
# =============================================================================
# Detects CPU SIMD capabilities at Bazel analysis time and generates
# appropriate configuration constants.
#
# Usage in MODULE.bazel:
#   simd = use_extension("//bazel/rules:simd_detect.bzl", "simd_detect_ext")
#   use_repo(simd, "simd_config")
#
# Usage in BUILD files:
#   load("@simd_config//:defs.bzl", "SIMD_COPTS", "SIMD_DEFINES")
#
#   cc_library(
#       name = "my_simd_lib",
#       copts = SIMD_COPTS,
#       local_defines = SIMD_DEFINES,
#       ...
#   )
# =============================================================================

def _detect_x86_simd(repository_ctx):
    """Detect x86_64 SIMD capabilities."""
    os = repository_ctx.os.name.lower()

    if "linux" in os:
        # Linux: read /proc/cpuinfo
        result = repository_ctx.execute(["cat", "/proc/cpuinfo"])
        if result.return_code == 0:
            cpuinfo = result.stdout.lower()
            if "avx2" in cpuinfo:
                return "avx2"
            elif "ssse3" in cpuinfo:
                return "ssse3"
            elif "sse2" in cpuinfo:
                return "sse2"

    elif "mac" in os or "darwin" in os:
        # macOS: check sysctl for Apple Silicon first
        result = repository_ctx.execute(["sysctl", "-n", "sysctl.proc_translated"])
        is_rosetta = result.return_code == 0 and result.stdout.strip() == "1"

        result = repository_ctx.execute(["uname", "-m"])
        arch = result.stdout.strip().lower() if result.return_code == 0 else ""

        if arch == "arm64" and not is_rosetta:
            # Native Apple Silicon - no x86 SIMD
            return None
        elif is_rosetta:
            # Rosetta on Apple Silicon - SSSE3 is safe
            return "ssse3"
        else:
            # Intel Mac - check for AVX2
            result = repository_ctx.execute(["sysctl", "-n", "hw.optional.avx2_0"])
            if result.return_code == 0 and result.stdout.strip() == "1":
                return "avx2"
            else:
                return "ssse3"

    elif "win" in os:
        # Windows: assume SSE2 minimum for x86_64
        return "sse2"

    return None

def _detect_arm_simd(repository_ctx):
    """Detect ARM SIMD capabilities."""
    os = repository_ctx.os.name.lower()

    # ARM64 always has NEON
    result = repository_ctx.execute(["uname", "-m"])
    if result.return_code == 0:
        arch = result.stdout.strip().lower()
        if arch in ["aarch64", "arm64"]:
            # Check for SVE on Linux
            if "linux" in os:
                result = repository_ctx.execute(["cat", "/proc/cpuinfo"])
                if result.return_code == 0 and "sve" in result.stdout.lower():
                    return "sve"
            return "neon"
        elif "arm" in arch:
            # ARM32 - check for NEON
            if "linux" in os:
                result = repository_ctx.execute(["cat", "/proc/cpuinfo"])
                if result.return_code == 0 and "neon" in result.stdout.lower():
                    return "neon"

    return None

def _detect_sse42(repository_ctx):
    """Detect SSE4.2 for CRC32 hardware acceleration."""
    os = repository_ctx.os.name.lower()

    if "linux" in os:
        result = repository_ctx.execute(["cat", "/proc/cpuinfo"])
        if result.return_code == 0:
            if "sse4_2" in result.stdout.lower():
                return True

    elif "mac" in os or "darwin" in os:
        result = repository_ctx.execute(["sysctl", "-n", "hw.optional.sse4_2"])
        if result.return_code == 0 and result.stdout.strip() == "1":
            return True

    return False

def _detect_arm_crc32(repository_ctx):
    """Detect ARM CRC32 hardware support."""
    os = repository_ctx.os.name.lower()

    result = repository_ctx.execute(["uname", "-m"])
    if result.return_code != 0:
        return False

    arch = result.stdout.strip().lower()
    if arch not in ["aarch64", "arm64"]:
        return False

    if "mac" in os or "darwin" in os:
        # Apple Silicon always has CRC32
        return True

    if "linux" in os:
        result = repository_ctx.execute(["cat", "/proc/cpuinfo"])
        if result.return_code == 0 and "crc32" in result.stdout.lower():
            return True

    return False

def _get_simd_copts(simd_level, is_windows):
    """Get compiler options for the detected SIMD level."""
    if simd_level == "avx2":
        if is_windows:
            return ["-mavx2", "-mno-mmx", "-mprefer-vector-width=256"]
        return ["-mavx2", "-mprefer-vector-width=256"]
    elif simd_level == "ssse3":
        if is_windows:
            return ["-mssse3", "-mno-mmx"]
        return ["-mssse3"]
    elif simd_level == "sse2":
        if is_windows:
            return ["-msse2", "-mno-mmx"]
        return ["-msse2"]
    elif simd_level == "neon":
        return []  # NEON is baseline for ARM64
    elif simd_level == "sve":
        return ["-march=armv8-a+sve"]
    return []

def _get_simd_defines(simd_level):
    """Get preprocessor defines for the detected SIMD level."""
    if simd_level == "avx2":
        return [
            "SIMD_SUPPORT",
            "SIMD_SUPPORT_AVX2=1",
            "SIMD_SUPPORT_SSSE3=0",
            "SIMD_SUPPORT_SSE2=0",
            "SIMD_SUPPORT_NEON=0",
            "SIMD_SUPPORT_SVE=0",
        ]
    elif simd_level == "ssse3":
        return [
            "SIMD_SUPPORT",
            "SIMD_SUPPORT_AVX2=0",
            "SIMD_SUPPORT_SSSE3=1",
            "SIMD_SUPPORT_SSE2=0",
            "SIMD_SUPPORT_NEON=0",
            "SIMD_SUPPORT_SVE=0",
        ]
    elif simd_level == "sse2":
        return [
            "SIMD_SUPPORT",
            "SIMD_SUPPORT_AVX2=0",
            "SIMD_SUPPORT_SSSE3=0",
            "SIMD_SUPPORT_SSE2=1",
            "SIMD_SUPPORT_NEON=0",
            "SIMD_SUPPORT_SVE=0",
        ]
    elif simd_level == "neon":
        return [
            "SIMD_SUPPORT",
            "SIMD_SUPPORT_AVX2=0",
            "SIMD_SUPPORT_SSSE3=0",
            "SIMD_SUPPORT_SSE2=0",
            "SIMD_SUPPORT_NEON=1",
            "SIMD_SUPPORT_SVE=0",
        ]
    elif simd_level == "sve":
        return [
            "SIMD_SUPPORT",
            "SIMD_SUPPORT_AVX2=0",
            "SIMD_SUPPORT_SSSE3=0",
            "SIMD_SUPPORT_SSE2=0",
            "SIMD_SUPPORT_NEON=1",
            "SIMD_SUPPORT_SVE=1",
        ]
    else:
        # No SIMD
        return [
            "SIMD_SUPPORT_AVX2=0",
            "SIMD_SUPPORT_SSSE3=0",
            "SIMD_SUPPORT_SSE2=0",
            "SIMD_SUPPORT_NEON=0",
            "SIMD_SUPPORT_SVE=0",
        ]

def _simd_config_impl(repository_ctx):
    """Repository rule implementation for SIMD detection."""

    os_name = repository_ctx.os.name.lower()
    is_windows = "win" in os_name

    # Detect architecture
    result = repository_ctx.execute(["uname", "-m"])
    arch = result.stdout.strip().lower() if result.return_code == 0 else "unknown"

    # Determine if x86_64 or ARM
    is_x86_64 = arch in ["x86_64", "amd64"]
    is_arm64 = arch in ["aarch64", "arm64"]
    is_arm32 = "arm" in arch and not is_arm64

    # Detect SIMD level
    simd_level = None
    if is_x86_64:
        simd_level = _detect_x86_simd(repository_ctx)
    elif is_arm64 or is_arm32:
        simd_level = _detect_arm_simd(repository_ctx)

    # Detect CRC32 hardware support
    has_crc32_hw = False
    if is_x86_64:
        has_crc32_hw = _detect_sse42(repository_ctx)
    elif is_arm64:
        has_crc32_hw = _detect_arm_crc32(repository_ctx)

    # Get SIMD copts and defines
    simd_copts = _get_simd_copts(simd_level, is_windows)
    simd_defines = _get_simd_defines(simd_level)

    # CRC32 copts and defines
    crc32_copts = ["-msse4.2"] if has_crc32_hw and is_x86_64 else []
    crc32_defines = ["ASCIICHAT_CRC32_HW=1"] if has_crc32_hw else ["ASCIICHAT_CRC32_HW=0"]

    # Generate BUILD.bazel with visibility
    build_content = """\
# =============================================================================
# Auto-generated SIMD configuration
# =============================================================================
# Generated by //bazel/rules:simd_detect.bzl
#
# Detected architecture: {arch}
# Detected SIMD level: {simd}
# Has CRC32 HW: {crc32}
# =============================================================================

package(default_visibility = ["//visibility:public"])

exports_files(["defs.bzl"])
""".format(
        arch = arch,
        simd = simd_level or "none",
        crc32 = "true" if has_crc32_hw else "false",
    )

    # Generate defs.bzl with constants
    bzl_content = """\
# =============================================================================
# Auto-generated SIMD constants
# =============================================================================
# Generated by //bazel/rules:simd_detect.bzl at Bazel analysis time
#
# Usage in BUILD files:
#   load("@simd_config//:defs.bzl", "SIMD_COPTS", "SIMD_DEFINES")
# =============================================================================

# Detected values
DETECTED_ARCH = "{arch}"
DETECTED_SIMD = "{simd}"
DETECTED_CRC32_HW = {crc32}

# Architecture flags
IS_X86_64 = {is_x86_64}
IS_ARM64 = {is_arm64}
IS_ARM32 = {is_arm32}

# SIMD compiler flags - add these to copts
SIMD_COPTS = {simd_copts}

# SIMD preprocessor defines - add these to local_defines
SIMD_DEFINES = {simd_defines}

# CRC32 hardware acceleration - add to copts for crc32_hw.c
CRC32_HW_COPTS = {crc32_copts}

# CRC32 hardware defines - add to local_defines
CRC32_HW_DEFINES = {crc32_defines}

# Combined SIMD + CRC32 for convenience
ALL_SIMD_COPTS = SIMD_COPTS + CRC32_HW_COPTS
ALL_SIMD_DEFINES = SIMD_DEFINES + CRC32_HW_DEFINES
""".format(
        arch = arch,
        simd = simd_level or "none",
        crc32 = "True" if has_crc32_hw else "False",
        is_x86_64 = "True" if is_x86_64 else "False",
        is_arm64 = "True" if is_arm64 else "False",
        is_arm32 = "True" if is_arm32 else "False",
        simd_copts = repr(simd_copts),
        simd_defines = repr(simd_defines),
        crc32_copts = repr(crc32_copts),
        crc32_defines = repr(crc32_defines),
    )

    # Write the files
    repository_ctx.file("BUILD.bazel", build_content)
    repository_ctx.file("defs.bzl", bzl_content)

simd_config = repository_rule(
    implementation = _simd_config_impl,
    local = True,
    doc = "Detects SIMD capabilities and generates configuration constants",
)

def _simd_detect_ext_impl(module_ctx):
    """Module extension for SIMD detection."""
    simd_config(name = "simd_config")
    return module_ctx.extension_metadata(
        reproducible = False,  # Detection is host-specific
        root_module_direct_deps = ["simd_config"],
        root_module_direct_dev_deps = [],
    )

simd_detect_ext = module_extension(
    implementation = _simd_detect_ext_impl,
)
