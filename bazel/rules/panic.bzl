# =============================================================================
# Panic Instrumentation Rule
# =============================================================================
# Custom Bazel rule for instrumenting sources with execution tracing.
#
# The panic tool adds instrumentation for logging function entry/exit and
# statement execution. It runs AFTER defer transformation.
#
# Usage:
#   load("//bazel/rules:panic.bzl", "panic_transform")
#
#   panic_transform(
#       name = "my_panic",
#       srcs = [":my_defer"],  # Output from defer_transform
#       deps = [":some_lib"],  # For include paths
#   )
#
#   cc_library(
#       name = "my_lib_panic",
#       srcs = [":my_panic"],
#       deps = ["//lib:panic"],  # Runtime instrumentation library
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
        basename = hdr.path.split("/")[-1]
        commands.append('cp "{hdr_path}" "$BAZEL_GEN_DIR/{basename}"'.format(
            hdr_path = hdr.path,
            basename = basename,
        ))
        commands.append('echo "Copied {hdr_path} -> $BAZEL_GEN_DIR/{basename}" >&2'.format(
            hdr_path = hdr.path,
            basename = basename,
        ))
    return "\n".join(commands) if commands else "echo 'No generated headers to copy' >&2"

def _generate_compile_db_entries_template(srcs, compiler_args, input_root_var):
    """Generate compile_commands.json entries as a bash template."""
    entries = []
    args_str = '","'.join(compiler_args)

    for src in srcs:
        # Use shell variable for the input root (resolved at runtime)
        entry = '''{{
  "directory": "${input_root}",
  "file": "${input_root}/{src_path}",
  "arguments": ["clang","{args}","-c","${input_root}/{src_path}"]
}}'''.format(src_path = src.path, args = args_str, input_root = input_root_var)
        entries.append(entry)

    return ",\n".join(entries)

def _panic_transform_impl(ctx):
    """Transform source files with panic instrumentation."""

    # Get the panic tool
    panic_tool = ctx.executable._panic_tool

    # Collect all source files
    all_srcs = ctx.files.srcs

    # Get include paths from dependencies
    include_paths = _get_include_paths(ctx.attr.deps)

    # Collect header files for inputs
    all_hdrs = []
    for dep in ctx.attr.deps:
        if CcInfo in dep:
            all_hdrs.extend(dep[CcInfo].compilation_context.headers.to_list())

    # Add any additional headers
    for hdr_target in ctx.attr.hdrs:
        all_hdrs.extend(hdr_target.files.to_list())

    # Build compiler arguments list
    compiler_args = [
        "-std=c23",
        "-D_GNU_SOURCE",
        "-DASCIICHAT_BUILD_WITH_DEFER",
        "-DASCIICHAT_BUILD_WITH_PANIC",
        "-D__BAZEL_BUILD__",
    ]

    # System include paths for Clang tooling
    system_includes = [
        "/usr/include",
        "/usr/lib/clang/14/include",
        "/usr/lib/clang/15/include",
        "/usr/lib/clang/16/include",
        "/usr/lib/clang/17/include",
        "/usr/lib/clang/18/include",
        "/usr/lib/clang/19/include",
        "/usr/lib/clang/20/include",
        "/usr/lib/clang/21/include",
        "/usr/lib/clang/22/include",
        # macOS paths
        "/Library/Developer/CommandLineTools/usr/lib/clang/14.0.0/include",
        "/Library/Developer/CommandLineTools/usr/lib/clang/15.0.0/include",
        "/Library/Developer/CommandLineTools/usr/lib/clang/16/include",
    ]

    for inc in system_includes:
        compiler_args.append("-isystem")
        compiler_args.append(inc)

    # Add SIMD defines
    for define in SIMD_DEFINES:
        compiler_args.append("-D" + define)

    # Add include paths from dependencies
    for inc in include_paths:
        if inc and not inc.startswith("external/") and not inc.startswith("bazel-out/"):
            compiler_args.append("-I$INPUT_ROOT/" + inc)

    # Add workspace root includes
    compiler_args.append("-I$INPUT_ROOT")
    compiler_args.append("-I$INPUT_ROOT/lib")
    compiler_args.append("-I$INPUT_ROOT/deps/tomlc17/src")
    compiler_args.append("-I$INPUT_ROOT/deps/uthash/src")
    compiler_args.append("-I$INPUT_ROOT/deps/libsodium/src/libsodium/include")
    compiler_args.append("-I$INPUT_ROOT/deps/bearssl/inc")

    # Include paths for generated headers
    compiler_args.append("-I$INPUT_ROOT/build/generated")
    compiler_args.append("-I$INPUT_ROOT/build_release/generated")
    compiler_args.append("-I$INPUT_ROOT/build_debug/generated")
    compiler_args.append("-I$BAZEL_GEN_DIR")

    # Declare output directories
    compile_db_dir = ctx.actions.declare_directory(ctx.label.name + "_compile_db")
    output_dir = ctx.actions.declare_directory(ctx.label.name + "_out")

    # Build file filters if specified
    file_filters = ""
    for f in ctx.attr.filter_files:
        file_filters += ' --filter-file="{}"'.format(f)

    # Build function filters if specified
    func_filters = ""
    for f in ctx.attr.filter_functions:
        func_filters += ' --filter-function="{}"'.format(f)

    # Create the transformation script
    script_content = """#!/bin/bash
set -e

# Disable sanitizers for the panic tool
export ASAN_OPTIONS="detect_leaks=0:halt_on_error=0"
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=0"

# Get the first source path (could be a file or directory from defer_transform)
FIRST_SRC="{first_src}"

echo "First source path: $FIRST_SRC" >&2

# Check if input is a directory (from defer_transform output)
if [[ -d "$FIRST_SRC" ]]; then
    echo "Input is a directory (from defer_transform)" >&2
    # Use the path Bazel provides directly - it's a symlink to the actual location
    # Don't resolve symlinks as that breaks in sandboxed execution
    DEFER_OUTPUT_DIR="$FIRST_SRC"

    # Find .c files in the directory (use the Bazel-provided path, not resolved path)
    echo "Looking for .c files in: $DEFER_OUTPUT_DIR" >&2
    ls -la "$DEFER_OUTPUT_DIR" >&2 || true
    find "$DEFER_OUTPUT_DIR" -type d >&2 || true

    REAL_SRCS=$(find "$DEFER_OUTPUT_DIR" -name "*.c" -type f 2>/dev/null | tr '\\n' ' ')
    if [[ -z "$REAL_SRCS" ]]; then
        echo "Error: No .c files found in $DEFER_OUTPUT_DIR" >&2
        echo "Directory contents:" >&2
        find "$DEFER_OUTPUT_DIR" -type f >&2 || true
        exit 1
    fi
    echo "Found source files in defer output: $REAL_SRCS" >&2

    # Set INPUT_ROOT to the absolute path of the defer output directory
    # This is needed for compile_commands.json to match the absolute paths
    INPUT_ROOT=$(cd "$DEFER_OUTPUT_DIR" && pwd)
    echo "INPUT_ROOT (absolute): $INPUT_ROOT" >&2

    # Convert REAL_SRCS to absolute paths
    ABS_SRCS=""
    for src in $REAL_SRCS; do
        ABS_SRC=$(cd "$(dirname "$src")" && pwd)/$(basename "$src")
        ABS_SRCS="$ABS_SRCS $ABS_SRC"
    done
    REAL_SRCS="$ABS_SRCS"
    echo "Absolute source files: $REAL_SRCS" >&2
else
    # For panic transform, input may be from defer transform (Bazel output)
    # or original source files. Handle both cases.
    if [[ "$FIRST_SRC" == bazel-out/* ]]; then
        # Input is from Bazel (defer transform output)
        # Resolve the symlink to get the actual path
        REAL_SRC_PATH=$(readlink -f "$FIRST_SRC" 2>/dev/null || realpath "$FIRST_SRC" 2>/dev/null || echo "$PWD/$FIRST_SRC")
        INPUT_ROOT=$(dirname $(dirname $(dirname "$REAL_SRC_PATH")))
    else
        # Input is original source file
        REAL_SRC_PATH=$(readlink -f "$FIRST_SRC" 2>/dev/null || realpath "$FIRST_SRC" 2>/dev/null || echo "")
        RELATIVE_PATH="{first_src}"
        INPUT_ROOT="${{REAL_SRC_PATH%/$RELATIVE_PATH}}"
    fi

    # Collect real source file paths
    REAL_SRCS=""
    for src in {src_paths}; do
        if [[ -d "$src" ]]; then
            # Directory - find .c files inside
            FOUND_FILES=$(find "$src" -name "*.c" -type f | tr '\\n' ' ')
            REAL_SRCS="$REAL_SRCS $FOUND_FILES"
        elif [[ "$src" == bazel-out/* ]]; then
            REAL_PATH=$(readlink -f "$src" 2>/dev/null || realpath "$src" 2>/dev/null || echo "$PWD/$src")
            REAL_SRCS="$REAL_SRCS $REAL_PATH"
        else
            REAL_PATH=$(readlink -f "$src" 2>/dev/null || realpath "$src" 2>/dev/null || echo "$INPUT_ROOT/$src")
            REAL_SRCS="$REAL_SRCS $REAL_PATH"
        fi
    done
fi

echo "Input root: $INPUT_ROOT" >&2

# Create directory for generated headers
BAZEL_GEN_DIR="{compile_db_dir}/gen"
mkdir -p "$BAZEL_GEN_DIR"
echo "Bazel generated header dir: $BAZEL_GEN_DIR" >&2

# Copy generated headers
{copy_headers_commands}

# Determine source root for include paths
# If input is from defer transform, source root is the workspace, not the defer output
WORKSPACE_ROOT=$(cd "$PWD" && pwd)
if [[ "$WORKSPACE_ROOT" == *"execroot/_main"* ]]; then
    # Running in Bazel execroot - find the workspace
    WORKSPACE_ROOT=$(echo "$WORKSPACE_ROOT" | sed 's|/execroot/_main.*|/execroot/_main|')
fi
echo "Workspace root for includes: $WORKSPACE_ROOT" >&2

# Generate compile_commands.json dynamically based on discovered files
COMPILE_DB="{compile_db_dir}/compile_commands.json"
echo "[" > "$COMPILE_DB"
FIRST=1
for src_file in $REAL_SRCS; do
    if [[ -f "$src_file" ]]; then
        if [[ $FIRST -eq 0 ]]; then
            echo "," >> "$COMPILE_DB"
        fi
        FIRST=0
        # Use workspace root for include paths (headers are there), INPUT_ROOT for directory
        cat >> "$COMPILE_DB" << EOF
{{
  "directory": "$INPUT_ROOT",
  "file": "$src_file",
  "arguments": ["clang","-std=c23","-D_GNU_SOURCE","-DASCIICHAT_BUILD_WITH_DEFER","-DASCIICHAT_BUILD_WITH_PANIC","-D__BAZEL_BUILD__","-I$WORKSPACE_ROOT","-I$WORKSPACE_ROOT/lib","-I$WORKSPACE_ROOT/deps/tomlc17/src","-I$WORKSPACE_ROOT/deps/uthash/src","-I$WORKSPACE_ROOT/deps/libsodium/src/libsodium/include","-I$WORKSPACE_ROOT/deps/bearssl/inc","-I$BAZEL_GEN_DIR","-c","$src_file"]
}}
EOF
    fi
done
echo "]" >> "$COMPILE_DB"

echo "compile_commands.json:" >&2
cat "$COMPILE_DB" >&2

echo "Running panic tool: {panic_tool}" >&2
echo "Real source files: $REAL_SRCS" >&2
echo "Output dir: {output_dir}" >&2
echo "Input root: $INPUT_ROOT" >&2

# Run the panic tool
"{panic_tool}" \\
    --output-dir="{output_dir}" \\
    --input-root="$INPUT_ROOT" \\
    -p "{compile_db_dir}" \\
    {file_filters} \\
    {func_filters} \\
    $REAL_SRCS

TOOL_EXIT=$?
echo "Tool exit code: $TOOL_EXIT" >&2

if [ $TOOL_EXIT -ne 0 ]; then
    echo "Panic tool failed with exit code $TOOL_EXIT" >&2
    exit $TOOL_EXIT
fi

# List output files
echo "Output directory contents:" >&2
find "{output_dir}" -name "*.c" 2>/dev/null | head -20 >&2

exit 0
""".format(
        first_src = all_srcs[0].path if all_srcs else "",
        compile_db_dir = compile_db_dir.path,
        output_dir = output_dir.path,
        panic_tool = panic_tool.path,
        src_paths = " ".join([src.path for src in all_srcs]),
        copy_headers_commands = _generate_copy_headers_commands(all_hdrs),
        compile_db_entries = _generate_compile_db_entries_template(all_srcs, compiler_args, "INPUT_ROOT"),
        file_filters = file_filters,
        func_filters = func_filters,
    )

    # Write the script
    script = ctx.actions.declare_file(ctx.label.name + "_panic.sh")
    ctx.actions.write(
        output = script,
        content = script_content,
        is_executable = True,
    )

    # Run the transformation
    # Note: We use local execution (no sandbox) because TreeArtifact inputs
    # (directories from defer_transform) don't properly populate in sandboxes
    # when they contain nested subdirectories.
    ctx.actions.run(
        inputs = all_srcs + all_hdrs + [panic_tool],
        outputs = [compile_db_dir, output_dir],
        executable = script,
        tools = [panic_tool],
        mnemonic = "PanicTransform",
        progress_message = "Panic-instrumenting %{label}",
        use_default_shell_env = True,
        execution_requirements = {"no-sandbox": "1"},
    )

    return [
        DefaultInfo(files = depset([output_dir])),
    ]

panic_transform = rule(
    implementation = _panic_transform_impl,
    attrs = {
        "srcs": attr.label_list(
            allow_files = [".c"],
            mandatory = True,
            doc = "Source files to instrument (can be from defer_transform or original)",
        ),
        "deps": attr.label_list(
            providers = [CcInfo],
            doc = "Dependencies (for include paths)",
        ),
        "hdrs": attr.label_list(
            allow_files = [".h"],
            doc = "Additional headers needed (e.g., generated headers like version.h)",
        ),
        "filter_files": attr.string_list(
            default = [],
            doc = "Only instrument files whose path contains one of these substrings",
        ),
        "filter_functions": attr.string_list(
            default = [],
            doc = "Only instrument functions whose name matches one of these substrings",
        ),
        # The panic tool is built from source at //src/tooling/panic:panic
        "_panic_tool": attr.label(
            default = "//bazel/stubs:panic_tool",
            executable = True,
            cfg = "exec",
            doc = "Panic tool built from source",
        ),
    },
    doc = "Instrument C sources with execution tracing for debugging.",
)

# =============================================================================
# Helper Macros for Combined Transformations
# =============================================================================
# Note: For combined defer + panic transformation, load both rules in your
# BUILD.bazel file and chain them manually:
#
#   load("//bazel/rules:defer.bzl", "defer_transform")
#   load("//bazel/rules:panic.bzl", "panic_transform")
#
#   defer_transform(
#       name = "my_defer",
#       srcs = ["my_file.c"],
#       deps = [...],
#   )
#
#   panic_transform(
#       name = "my_panic",
#       srcs = [":my_defer"],  # Output from defer_transform
#       deps = [...],
#   )
#
#   cc_library(
#       name = "my_lib_instrumented",
#       srcs = [":my_panic"],
#       deps = ["//lib:panic"],  # Runtime instrumentation library
#       ...
#   )
