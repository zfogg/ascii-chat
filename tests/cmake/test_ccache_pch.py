"""Verify native C/C++ objects and PCH survive a clean ccache rebuild."""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ("cmake", "ninja", "ccache", "clang", "clang++")


@unittest.skipUnless(all(shutil.which(tool) for tool in TOOLS), "CMake, Ninja, ccache and Clang are required")
class CCachePCHTest(unittest.TestCase):
    def test_clean_rebuild_hits_c_and_cxx_pch(self):
        with tempfile.TemporaryDirectory(prefix="ascii_chat_pch_") as temporary:
            source = pathlib.Path(temporary)
            build = source / "out"
            (source / "probe.h").write_text("#pragma once\nstruct CacheValue { int value; };\n")
            for suffix in ("c", "cpp"):
                (source / ("probe." + suffix)).write_text(
                    '#include "probe.h"\nint cached_value(void) { return sizeof(struct CacheValue); }\n')
            (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.30)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(cache_probe C CXX)
find_program(ASCIICHAT_CCACHE_EXECUTABLE ccache REQUIRED)
set(CMAKE_C_COMPILER_LAUNCHER "${{ASCIICHAT_CCACHE_EXECUTABLE}}" CACHE STRING "" FORCE)
include("{ROOT.as_posix()}/cmake/compiler/CCache.cmake")
add_library(ascii-chat-core OBJECT probe.c probe.cpp)
include("{ROOT.as_posix()}/cmake/compiler/PCH.cmake")
set_property(TARGET ascii-chat-core PROPERTY PRECOMPILE_HEADERS "${{CMAKE_CURRENT_SOURCE_DIR}}/probe.h")
add_library(cxx-probe OBJECT probe.cpp)
''')
            environment = os.environ.copy()
            environment.pop("CCACHE_DISABLE", None)
            environment.update(CCACHE_DIR=str(source / "cache"), CCACHE_STATSLOG=str(source / "stats.log"))

            def run(*args):
                result = subprocess.run(args, env=environment, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout

            run("cmake", "-G", "Ninja", "-S", str(source), "-B", str(build),
                "-DCMAKE_C_COMPILER=" + shutil.which("clang"),
                "-DCMAKE_CXX_COMPILER=" + shutil.which("clang++"))
            run("cmake", "--build", str(build), "--parallel", "2")
            run("cmake", "--build", str(build), "--target", "clean")
            run("cmake", "--build", str(build), "--parallel", "2")
            stats = dict(line.split() for line in run("ccache", "--print-log-stats").splitlines())
            self.assertEqual(int(stats["cache_miss"]), 5)
            self.assertEqual(int(stats["direct_cache_hit"]), 5)
            self.assertEqual(int(stats["could_not_use_precompiled_header"]), 0)

            # A real header edit must invalidate both PCH files and their users.
            (source / "probe.h").write_text("#pragma once\nstruct CacheValue { int value; int extra; };\n")
            run("cmake", "--build", str(build), "--parallel", "2")
            stats = dict(line.split() for line in run("ccache", "--print-log-stats").splitlines())
            self.assertEqual(int(stats["cache_miss"]), 10)
            self.assertEqual(int(stats["direct_cache_hit"]), 5)


if __name__ == "__main__":
    unittest.main()
