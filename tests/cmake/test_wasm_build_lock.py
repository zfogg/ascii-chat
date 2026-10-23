"""Exercise the parent WASM lock with concurrent native build processes."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import tempfile
import unittest


class WasmBuildLockTest(unittest.TestCase):
    def test_separate_parent_builds_do_not_overlap(self):
        wrapper = Path(__file__).resolve().parents[2] / "cmake/scripts/build_wasm.cmake"
        with tempfile.TemporaryDirectory(prefix="ascii-wasm-lock-") as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.25)\nproject(lock_probe NONE)\n'
                'set_property(GLOBAL PROPERTY JOB_POOLS probe=1)\n'
                'foreach(name mirror-web client-web face-follow-web)\n'
                '  add_custom_target(${name} COMMAND "${CMAKE_COMMAND}" '
                '-P "${CMAKE_SOURCE_DIR}/probe.cmake" JOB_POOL probe VERBATIM)\n'
                'endforeach()\n',
                encoding="utf-8",
            )
            (root / "probe.cmake").write_text(
                'if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/active")\n'
                '  message(FATAL_ERROR "Concurrent nested builds")\nendif()\n'
                'file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/active" "active")\n'
                'execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.3)\n'
                'file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/active")\n'
                'file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/completed" "done\\n")\n',
                encoding="utf-8",
            )
            build = root / "build"
            subprocess.run(
                ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja"],
                check=True, capture_output=True, text=True,
            )

            def run(targets):
                return subprocess.run(
                    ["cmake", f"-DWASM_BUILD_DIR={build}", f"-DWASM_TARGETS={targets}",
                     "-P", str(wrapper)],
                    capture_output=True, text=True, timeout=60,
                )

            with ThreadPoolExecutor(max_workers=3) as pool:
                results = list(pool.map(run, [
                    "mirror-web", "client-web",
                    "mirror-web;client-web;face-follow-web",
                ]))
            for result in results:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((build / "completed").read_text().count("done"), 5)


if __name__ == "__main__":
    unittest.main()
