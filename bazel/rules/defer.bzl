# =============================================================================
# defer() Source Transformation Rule
# =============================================================================
# Custom Bazel rule for transforming sources that use defer() macro.
#
# The defer() macro provides RAII-style cleanup in C. The transformation tool
# (defer) rewrites sources to insert cleanup code at all exit points.
#
# Usage:
#   load("//bazel/rules:defer.bzl", "defer_transform")
#
#   defer_transform(
#       name = "config_defer",
#       srcs = ["config.c"],
#       deps = [":some_lib"],  # For include paths
#   )
#
#   cc_library(
#       name = "my_lib",
#       srcs = [":config_defer"],
#       ...
#   )
# =============================================================================

load("@simd_config//:defs.bzl", "SIMD_DEFINES")

def _get_include_paths(deps):
    """Extract include paths from dependencies."""
    paths = []
    for dep in deps:
        if CcInfo in dep:
            cc_info = dep[CcInfo]
            for inc in cc_info.compilation_context.includes.to_list():
                paths.append(inc)
            for inc in cc_info.compilation_context.system_includes.to_list():
                paths.append(inc)
            for inc in cc_info.compilation_context.quote_includes.to_list():
                paths.append(inc)
    return paths

def _generate_copy_headers_commands(hdrs):
    """Generate shell commands to copy headers to $BAZEL_GEN_DIR."""
    commands = []
    for hdr in hdrs:
        # Get the basename of the header file
        basename = hdr.path.split("/")[-1]
        # Copy from Bazel's path to $BAZEL_GEN_DIR
        commands.append('cp "{hdr_path}" "$BAZEL_GEN_DIR/{basename}"'.format(
            hdr_path = hdr.path,
            basename = basename,
        ))
        commands.append('echo "Copied {hdr_path} -> $BAZEL_GEN_DIR/{basename}" >&2'.format(
            hdr_path = hdr.path,
            basename = basename,
        ))
    return "\n".join(commands) if commands else "echo 'No generated headers to copy' >&2"

def _generate_compile_db_entries_template(srcs, compiler_args):
    """Generate compile_commands.json entries as a bash template.

    Uses $SOURCE_ROOT variable which is resolved at runtime to handle
    Bazel's symlink structure correctly.
    """
    entries = []
    args_str = '","'.join(compiler_args)

    for src in srcs:
        # Use shell variable $SOURCE_ROOT which is resolved at runtime
        entry = '''{{
  "directory": "$SOURCE_ROOT",
  "file": "$SOURCE_ROOT/{src_path}",
  "arguments": ["clang","{args}","-c","$SOURCE_ROOT/{src_path}"]
}}'''.format(src_path = src.path, args = args_str)
        entries.append(entry)

    return ",\n".join(entries)

def _defer_transform_impl(ctx):
    """Transform source files that use defer() macro."""

    # Get the defer tool
    defer_tool = ctx.executable._defer_tool

    # Collect all source files
    all_srcs = ctx.files.srcs

    # Get include paths from dependencies
    include_paths = _get_include_paths(ctx.attr.deps)

    # Collect header files for inputs
    all_hdrs = []
    for dep in ctx.attr.deps:
        if CcInfo in dep:
            all_hdrs.extend(dep[CcInfo].compilation_context.headers.to_list())

    # Add any additional headers (e.g., generated headers like version.h)
    for hdr_target in ctx.attr.hdrs:
        all_hdrs.extend(hdr_target.files.to_list())

    # Build compiler arguments list - each as a separate array element for JSON
    compiler_args = [
        "-std=c23",
        "-D_GNU_SOURCE",
        "-DASCIICHAT_BUILD_WITH_DEFER",
        "-D__BAZEL_BUILD__",
        # Add Clang resource directory so LibTooling can find builtin headers
        # This is critical for Clang to find system headers correctly
        # We'll detect it at runtime in the script below
        "-resource-dir=__CLANG_RESOURCE_DIR__",
    ]

    # System include paths for Clang tooling
    # IMPORTANT: We use -I (not -isystem) for /usr/include to ensure glibc headers
    # are found correctly by LibTooling. -isystem can cause issues with system headers.
    # For Clang resource dirs, we continue to use -isystem since those are compiler-specific.
    system_includes_I = [
        # C library headers - use -I to avoid -isystem issues with glibc
        "/usr/include",
        # Architecture-specific C library headers (Ubuntu/Debian)
        "/usr/include/x86_64-linux-gnu",
        "/usr/include/aarch64-linux-gnu",
        "/usr/include/arm-linux-gnueabihf",
    ]

    # Clang resource directories - these can safely use -isystem
    system_includes_isystem = [
        # Direct /usr/lib/clang paths (standalone Clang)
    # System include paths for Clang tooling - use -isystem as prefix
    system_includes = [
        # Clang built-in headers (must come first for stddef.h, etc.)
        "/usr/lib/clang/14/include",
        "/usr/lib/clang/15/include",
        "/usr/lib/clang/16/include",
        "/usr/lib/clang/17/include",
        "/usr/lib/clang/18/include",
        "/usr/lib/clang/19/include",
        "/usr/lib/clang/20/include",
        "/usr/lib/clang/21/include",
        "/usr/lib/clang/22/include",
        # Ubuntu/Debian apt package paths (GitHub Actions uses these)
        "/usr/lib/llvm-14/lib/clang/14/include",
        "/usr/lib/llvm-14/lib/clang/14.0.0/include",
        "/usr/lib/llvm-15/lib/clang/15/include",
        "/usr/lib/llvm-15/lib/clang/15.0.0/include",
        "/usr/lib/llvm-16/lib/clang/16/include",
        "/usr/lib/llvm-16/lib/clang/16.0.0/include",
        "/usr/lib/llvm-17/lib/clang/17/include",
        "/usr/lib/llvm-17/lib/clang/17.0.0/include",
        "/usr/lib/llvm-18/lib/clang/18/include",
        "/usr/lib/llvm-18/lib/clang/18.0.0/include",
        "/usr/lib/llvm-19/lib/clang/19/include",
        "/usr/lib/llvm-19/lib/clang/19.0.0/include",
        "/usr/lib/llvm-20/lib/clang/20/include",
        "/usr/lib/llvm-20/lib/clang/20.0.0/include",
        "/usr/lib/llvm-21/lib/clang/21/include",
        "/usr/lib/llvm-21/lib/clang/21.0.0/include",
        "/usr/lib/llvm-22/lib/clang/22/include",
        "/usr/lib/llvm-22/lib/clang/22.0.0/include",
        # macOS paths (Xcode Command Line Tools)
        # Standard C library headers
        "/usr/include",
        # Architecture-specific system headers (Linux)
        "/usr/include/x86_64-linux-gnu",  # AMD64/x86_64
        "/usr/include/aarch64-linux-gnu",  # ARM64
        # macOS paths
        "/Library/Developer/CommandLineTools/usr/lib/clang/14.0.0/include",
        "/Library/Developer/CommandLineTools/usr/lib/clang/15.0.0/include",
        "/Library/Developer/CommandLineTools/usr/lib/clang/16/include",
        "/Library/Developer/CommandLineTools/usr/lib/clang/16.0.0/include",
        "/Library/Developer/CommandLineTools/usr/lib/clang/17/include",
        "/Library/Developer/CommandLineTools/usr/lib/clang/17.0.0/include",
    ]

    # Add C library paths with -I (not -isystem) to avoid glibc parsing issues
    for inc in system_includes_I:
        compiler_args.append("-I" + inc)

    # Add Clang resource paths with -isystem
    for inc in system_includes_isystem:
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include",
    ]

    for inc in system_includes:
        compiler_args.append("-isystem")
        compiler_args.append(inc)

    # Add SIMD defines
    for define in SIMD_DEFINES:
        compiler_args.append("-D" + define)

    # Add include paths from dependencies
    # Skip Bazel-specific paths (external/, bazel-out/) since we're using real source paths
    for inc in include_paths:
        if inc and not inc.startswith("external/") and not inc.startswith("bazel-out/"):
            compiler_args.append("-I$SOURCE_ROOT/" + inc)

    # Add workspace root includes
    # NOTE: These paths use $SOURCE_ROOT which gets expanded in the script
    # We use source tree paths (deps/) not Bazel external paths (external/+non_bcr_deps+)
    # because we're processing real source files, not Bazel-sandboxed files
    compiler_args.append("-I$SOURCE_ROOT")
    compiler_args.append("-I$SOURCE_ROOT/lib")
    compiler_args.append("-I$SOURCE_ROOT/deps/tomlc17/src")
    compiler_args.append("-I$SOURCE_ROOT/deps/uthash/src")
    compiler_args.append("-I$SOURCE_ROOT/deps/libsodium/src/libsodium/include")
    compiler_args.append("-I$SOURCE_ROOT/deps/bearssl/inc")

    # Add include path for generated headers (like version.h)
    # The version.h may be in Bazel's output or the CMake build directory
    # We add both to handle either case
    compiler_args.append("-I$SOURCE_ROOT/build/generated")
    compiler_args.append("-I$SOURCE_ROOT/build_release/generated")
    compiler_args.append("-I$SOURCE_ROOT/build_debug/generated")
    # Also add Bazel-specific generated header path (set by script)
    # Use placeholder that gets replaced at runtime with actual path
    compiler_args.append("-I__BAZEL_GEN_DIR__")

    # Declare output directory for compile_commands.json
    compile_db_dir = ctx.actions.declare_directory(ctx.label.name + "_compile_db")

    # Declare output directory for transformed files
    output_dir = ctx.actions.declare_directory(ctx.label.name + "_out")

    # Build list of expected output paths (tool preserves directory structure)
    # e.g., lib/config.c -> output_dir/lib/config.c
    expected_outputs = " ".join([src.path for src in all_srcs])

    # Create a script that writes compile_commands.json and runs the tool
    # The script will resolve symlinks at runtime to get real source paths
    script_content = """#!/bin/bash
set -e

# Disable sanitizers for the defer tool
export ASAN_OPTIONS="detect_leaks=0:halt_on_error=0"
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=0"

# Get the first source file and resolve its symlink to find the real source root
FIRST_SRC="{first_src}"
REAL_SRC_PATH=$(readlink -f "$FIRST_SRC" 2>/dev/null || realpath "$FIRST_SRC" 2>/dev/null || echo "")
if [[ -z "$REAL_SRC_PATH" ]]; then
    echo "Error: Could not resolve real path for $FIRST_SRC" >&2
    exit 1
fi

# Extract the source root by removing the relative path suffix
# e.g., /home/user/repo/lib/config.c with lib/config.c -> /home/user/repo
RELATIVE_PATH="{first_src}"
SOURCE_ROOT="${{REAL_SRC_PATH%/$RELATIVE_PATH}}"

echo "Resolved source root: $SOURCE_ROOT" >&2

# Create directory for Bazel-generated headers
# Use absolute path so Clang can find headers regardless of working directory
BAZEL_GEN_DIR="$(pwd)/{compile_db_dir}/gen"
mkdir -p "$BAZEL_GEN_DIR"
echo "Bazel generated header dir: $BAZEL_GEN_DIR" >&2

# Copy Bazel-generated headers to a location the defer tool can find
# These are passed in as bazel-relative paths and need to be copied
{copy_generated_headers}

# Create compile database directory
mkdir -p "{compile_db_dir}"

# Generate compile_commands.json with resolved absolute paths
# This is critical: the defer tool follows symlinks, so we must use the real paths
# Note: We use __BAZEL_GEN_DIR__ placeholder which gets replaced with actual path
cat > "{compile_db_dir}/compile_commands.json" << COMPILE_DB_EOF
[
{compile_db_entries}
]
COMPILE_DB_EOF

# Detect Clang resource directory (contains builtin headers)
# This is critical for LibTooling to find system headers correctly
CLANG_RESOURCE_DIR=$(clang -print-resource-dir 2>/dev/null || echo "")
if [[ -z "$CLANG_RESOURCE_DIR" ]]; then
    echo "Warning: Could not detect Clang resource directory" >&2
    CLANG_RESOURCE_DIR="/usr/lib/clang/19"  # Fallback
fi
echo "Clang resource directory: $CLANG_RESOURCE_DIR" >&2

# Replace placeholders in compile_commands.json
# - __BAZEL_GEN_DIR__: Bazel-generated header directory (only known at runtime)
# - __CLANG_RESOURCE_DIR__: Clang resource directory (detected above)
sed -i "s|__BAZEL_GEN_DIR__|$BAZEL_GEN_DIR|g" "{compile_db_dir}/compile_commands.json"
sed -i "s|__CLANG_RESOURCE_DIR__|$CLANG_RESOURCE_DIR|g" "{compile_db_dir}/compile_commands.json"

# Debug output
echo "compile_commands.json:" >&2
cat "{compile_db_dir}/compile_commands.json" >&2

# Create output directory
mkdir -p "{output_dir}"

# Build the list of real source file paths
REAL_SRC_FILES=""
for src in {src_files}; do
    REAL_SRC_FILES="$REAL_SRC_FILES $SOURCE_ROOT/$src"
done

echo "Running defer tool: {defer_tool}" >&2
echo "Real source files:$REAL_SRC_FILES" >&2
echo "Output dir: {output_dir}" >&2
echo "Input root: $SOURCE_ROOT" >&2

# Run the defer tool with resolved paths
# Temporarily disable errexit to capture exit code and provide helpful error message
set +e
"{defer_tool}" $REAL_SRC_FILES --output-dir="{output_dir}" --input-root="$SOURCE_ROOT" -p "{compile_db_dir}" 2>&1
TOOL_EXIT=$?
set -e
echo "Tool exit code: $TOOL_EXIT" >&2
if [ $TOOL_EXIT -ne 0 ]; then
    echo "Defer tool failed with exit code $TOOL_EXIT" >&2
    exit 1
fi

# Debug: show what was created
echo "Output directory contents:" >&2
find "{output_dir}" -type f >&2 || true

# Check that output files were created (tool preserves directory structure)
for src in {expected_outputs}; do
    if [ ! -f "{output_dir}/$src" ]; then
        echo "Error: Expected output file {output_dir}/$src not found" >&2
        echo "Contents of output directory:" >&2
        find "{output_dir}" -type f >&2
        exit 1
    fi
done
""".format(
        compile_db_dir = compile_db_dir.path,
        output_dir = output_dir.path,
        defer_tool = defer_tool.path,
        src_files = " ".join([src.path for src in all_srcs]),
        first_src = all_srcs[0].path,
        expected_outputs = expected_outputs,
        # compile_db_entries will be dynamically generated by the script
        compile_db_entries = _generate_compile_db_entries_template(all_srcs, compiler_args),
        # Generate shell commands to copy Bazel-generated headers
        copy_generated_headers = _generate_copy_headers_commands(all_hdrs),
    )

    # Create the script file
    script = ctx.actions.declare_file(ctx.label.name + "_transform.sh")
    ctx.actions.write(
        output = script,
        content = script_content,
        is_executable = True,
    )

    # Run the transformation
    ctx.actions.run(
        inputs = depset(all_srcs + all_hdrs + [defer_tool]),
        outputs = [compile_db_dir, output_dir],
        executable = script,
        mnemonic = "DeferTransform",
        progress_message = "Transforming defer() in %d files" % len(all_srcs),
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(files = depset([output_dir])),
    ]

defer_transform = rule(
    implementation = _defer_transform_impl,
    attrs = {
        "srcs": attr.label_list(
            allow_files = [".c"],
            mandatory = True,
            doc = "Source files to transform",
        ),
        "deps": attr.label_list(
            providers = [CcInfo],
            doc = "Dependencies (for include paths)",
        ),
        "hdrs": attr.label_list(
            allow_files = [".h"],
            doc = "Additional headers needed (e.g., generated headers like version.h)",
        ),
        # The defer tool is built from source at //src/tooling/defer:defer
        "_defer_tool": attr.label(
            default = "//bazel/stubs:defer_tool",
            executable = True,
            cfg = "exec",
            doc = "Defer tool built from source",
        ),
    },
    doc = "Transform C sources that use defer() macro for RAII-style cleanup.",
)

# =============================================================================
# Helper macros
# =============================================================================

def defer_cc_library(name, srcs, defer_srcs = [], **kwargs):
    """
    A cc_library that automatically transforms defer() sources.

    Args:
        name: Target name
        srcs: Regular source files
        defer_srcs: Source files that use defer() and need transformation
        **kwargs: Additional arguments passed to cc_library
    """
    if defer_srcs:
        defer_transform(
            name = name + "_defer",
            srcs = defer_srcs,
            deps = kwargs.get("deps", []),
        )

        # Combine transformed and regular sources
        all_srcs = srcs + [":" + name + "_defer"]
    else:
        all_srcs = srcs

    native.cc_library(
        name = name,
        srcs = all_srcs,
        **kwargs
    )
