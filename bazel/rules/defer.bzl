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

    # Build compiler arguments
    # The tool uses Clang's tooling infrastructure, so we pass flags after --
    # We need to include system headers for the Clang AST parser to work
    compiler_args = [
        "-std=c23",
        "-D_GNU_SOURCE",
        "-DASCIICHAT_BUILD_WITH_DEFER",
        # System include paths for Clang tooling
        "-isystem", "/usr/include",
        "-isystem", "/usr/lib/clang/21/include",
    ]

    # Add SIMD defines
    for define in SIMD_DEFINES:
        compiler_args.append("-D" + define)

    # Add include paths
    for inc in include_paths:
        compiler_args.append("-I" + inc)

    # Add workspace root include
    compiler_args.append("-I.")
    compiler_args.append("-Ilib")

    # Add include path for generated headers (like version.h)
    # The genfiles directory contains generated headers
    compiler_args.append("-I" + ctx.genfiles_dir.path)
    compiler_args.append("-I" + ctx.genfiles_dir.path + "/lib")
    compiler_args.append("-I" + ctx.bin_dir.path)
    compiler_args.append("-I" + ctx.bin_dir.path + "/lib")

    # Declare output directory for transformed files
    output_dir = ctx.actions.declare_directory(ctx.label.name + "_out")

    # Build arguments
    args = ctx.actions.args()

    # Add source files
    for src in all_srcs:
        args.add(src.path)

    # Add output directory option
    args.add("--output-dir=" + output_dir.path)

    # Set input-root to flatten the output structure
    # If source is lib/config.c, we want output to be just config.c
    if all_srcs:
        first_src = all_srcs[0]
        src_dir = first_src.dirname
        if src_dir:
            args.add("--input-root=" + src_dir)

    # Add separator for compiler flags
    args.add("--")

    # Add compiler flags
    for arg in compiler_args:
        args.add(arg)

    ctx.actions.run(
        inputs = depset(all_srcs + all_hdrs),
        outputs = [output_dir],
        executable = defer_tool,
        arguments = [args],
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
            default = "//src/tooling/defer:ascii-instr-defer",
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
