"""Exercise Windows export generation with real COFF objects and long inputs."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("clang") and shutil.which("llvm-nm") and shutil.which("cmake"),
                     "Clang, llvm-nm and CMake are required")
class WindowsExportsTest(unittest.TestCase):
    def test_response_file_exports_and_errors(self):
        with tempfile.TemporaryDirectory(prefix="ascii_chat_exports_") as temporary:
            directory = pathlib.Path(temporary)
            objects = []
            for subdir, name, code in [
                ("plain", "alpha", "int fixture_alpha(void) { return 1; }\n"
                 "__declspec(selectany) int fixture_shared = 1;\n"
                 "int __sanitizer_fixture = 0;\n"),
                ("with spaces", "beta", "int fixture_beta(void) { return 2; }\n"
                 "__declspec(selectany) int fixture_shared = 1;\n"
                 "int fixture_value = 3;\n"),
                ("deps", "vendor", "int fixture_vendor(void) { return 4; }\n"),
            ]:
                source = directory / subdir / (name + ".c")
                source.parent.mkdir()
                source.write_text(code)
                obj = source.with_suffix(".obj")
                subprocess.run(["clang", "--target=x86_64-pc-windows-msvc", "-c", str(source),
                                "-o", str(obj)], check=True, capture_output=True)
                objects.append(obj)

            manifest = directory / "objects.txt"
            # Exceed cmd.exe and CreateProcess command-line limits. Include a PCH
            # entry as CMake's TARGET_OBJECTS does for PCH-enabled modules.
            manifest.write_text("\n".join(map(str, objects * 250 + [directory / "cmake_pch.h.pch"])))
            self.assertGreater(manifest.stat().st_size, 32767)
            output = directory / "exports.def"
            command = ["cmake", "-DNM_TOOL=" + shutil.which("llvm-nm"),
                       "-DOBJECT_LIST_FILE=" + str(manifest), "-DOUTPUT_FILE=" + str(output),
                       "-DLIBRARY_NAME=fixture", "-P", str(ROOT / "cmake/tools/GenerateDefFile.cmake")]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_text().splitlines(), ["LIBRARY fixture", "EXPORTS",
                             "    fixture_alpha", "    fixture_beta", "    fixture_shared", "    fixture_value"])

            expected = output.read_bytes()
            manifest.write_text(str(directory / "missing.obj"))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Symbol extraction failed", result.stderr)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
