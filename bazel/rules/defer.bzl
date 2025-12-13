# =============================================================================
# defer() Source Transformation Rule
# =============================================================================
# Custom Bazel rule for transforming sources that use defer() macro.
#
# The defer() macro provides RAII-style cleanup in C. The transformation tool
# (ascii-instr-defer) rewrites sources to insert cleanup code at all exit points.
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

    # Build compiler arguments list
    compiler_args = [
        "-std=c23",
        "-D_GNU_SOURCE",
        "-DASCIICHAT_BUILD_WITH_DEFER",
        "-D__BAZEL_BUILD__",
        # System include paths for Clang tooling
        "-isystem/usr/include",
        "-isystem/usr/lib/clang/19/include",
        "-isystem/usr/lib/clang/20/include",
        "-isystem/usr/lib/clang/21/include",
        "-isystem/usr/lib/clang/22/include",
    ]

    # Add SIMD defines
    for define in SIMD_DEFINES:
        compiler_args.append("-D" + define)

    # Add include paths from dependencies
    for inc in include_paths:
        if inc:
            compiler_args.append("-I" + inc)

    # Add workspace root includes
    compiler_args.append("-I.")
    compiler_args.append("-Ilib")
    compiler_args.append("-Iexternal/+non_bcr_deps+tomlc17/src")
    compiler_args.append("-Iexternal/+non_bcr_deps+uthash/src")
    compiler_args.append("-Iexternal/+non_bcr_deps+libsodium/src/libsodium/include")
    compiler_args.append("-Iexternal/+non_bcr_deps+bearssl/inc")

    # Add include path for generated headers (like version.h)
    compiler_args.append("-I" + ctx.genfiles_dir.path)
    compiler_args.append("-I" + ctx.genfiles_dir.path + "/lib")
    compiler_args.append("-I" + ctx.bin_dir.path)
    compiler_args.append("-I" + ctx.bin_dir.path + "/lib")

    # Declare output directory for compile_commands.json
    compile_db_dir = ctx.actions.declare_directory(ctx.label.name + "_compile_db")

    # Declare output directory for transformed files
    output_dir = ctx.actions.declare_directory(ctx.label.name + "_out")

    # Generate compile_commands.json content
    # The tool requires absolute paths in the compilation database
    compile_commands = []
    for src in all_srcs:
        entry = {
            "directory": "/proc/self/cwd",
            "file": src.path,
            "arguments": ["clang"] + compiler_args + ["-c", src.path],
        }
        compile_commands.append(entry)

    compile_db_content = json.encode(compile_commands)

    # Create a script that writes compile_commands.json and runs the tool
    script_content = """#!/bin/bash
set -e

# Disable sanitizers for the defer tool
export ASAN_OPTIONS="detect_leaks=0:halt_on_error=0"
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=0"

# Create compile database directory
mkdir -p "{compile_db_dir}"

# Write compile_commands.json
cat > "{compile_db_dir}/compile_commands.json" << 'COMPILE_DB_EOF'
{compile_db_content}
COMPILE_DB_EOF

# Create output directory
mkdir -p "{output_dir}"

# Run the defer tool
"{defer_tool}" {src_files} --output-dir="{output_dir}" -p "{compile_db_dir}"

# Check that output files were created
for src in {src_basenames}; do
    if [ ! -f "{output_dir}/$src" ]; then
        echo "Error: Expected output file {output_dir}/$src not found" >&2
        exit 1
    fi
done
""".format(
        compile_db_dir = compile_db_dir.path,
        compile_db_content = compile_db_content,
        output_dir = output_dir.path,
        defer_tool = defer_tool.path,
        src_files = " ".join([src.path for src in all_srcs]),
        src_basenames = " ".join([src.basename for src in all_srcs]),
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
        "_defer_tool": attr.label(
            default = "//src/tooling:ascii-instr-defer",
            executable = True,
            cfg = "exec",
            doc = "The defer transformation tool",
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
