import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("coverage_report", Path(__file__).with_name("generate-coverage.py"))
coverage_report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage_report)


class CoverageReportTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.report = self.root / "coverage.xml"

    def tearDown(self):
        self.directory.cleanup()

    def write_report(self, covered="1", filename="lib/audio/recording.c"):
        self.report.write_text(
            f'<coverage lines-covered="{covered}" lines-valid="2"><packages><package><classes>'
            f'<class filename="{filename}"/></classes></package></packages></coverage>'
        )

    def test_relative_report_retains_counts(self):
        self.write_report()
        self.assertEqual(coverage_report.validate_report(self.report), (1, 2))

    def test_empty_coverage_is_rejected(self):
        self.write_report(covered="0")
        with self.assertRaisesRegex(ValueError, "no executed"):
            coverage_report.validate_report(self.report)

    def test_absolute_source_is_rejected(self):
        self.write_report(filename=self.root.as_posix() + "/source.c")
        with self.assertRaisesRegex(ValueError, "repository-relative"):
            coverage_report.validate_report(self.report)

    def test_parent_traversal_is_rejected(self):
        self.write_report(filename="../source.c")
        with self.assertRaisesRegex(ValueError, "repository-relative"):
            coverage_report.validate_report(self.report)

    def test_missing_counters_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "No matching"):
            coverage_report.generate(self.root, self.root, self.report)

    @unittest.skipUnless(shutil.which("clang") and shutil.which("cmake") and shutil.which("ninja"), "Requires CMake, Ninja and Clang")
    def test_fresh_configuration_retains_test_flags_when_cached_compiler_changes(self):
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(fixture C)\n'
            'option(BUILD_TESTS "Build tests" OFF)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/settings.txt" "${BUILD_TESTS};${CMAKE_BUILD_TYPE}")\n'
        )
        build = self.root / "build"
        clang = shutil.which("clang")
        cached_compiler = self.root / "cached-clang"
        cached_compiler.symlink_to(clang)
        subprocess.run(["cmake", "-S", str(self.root), "-B", str(build), "-G", "Ninja",
                        f"-DCMAKE_C_COMPILER={cached_compiler}", "-DCMAKE_BUILD_TYPE=Release",
                        "-DBUILD_TESTS=OFF"], check=True)
        preserved = build / "preserved-build-output"
        preserved.write_text("keep")
        subprocess.run(["cmake", "--fresh", "-S", str(self.root), "-B", str(build), "-G", "Ninja",
                        f"-DCMAKE_C_COMPILER={clang}", "-DCMAKE_BUILD_TYPE=Debug",
                        "-DBUILD_TESTS=ON"], check=True)
        self.assertEqual((build / "settings.txt").read_text(), "ON;Debug")
        self.assertEqual(preserved.read_text(), "keep")

    @unittest.skipUnless(shutil.which("clang") and shutil.which("cmake") and shutil.which("ninja") and shutil.which("pkg-config"), "Requires CMake, Ninja, Clang and OpenSSL development files")
    def test_missing_cached_openssl_paths_are_rediscovered_and_link(self):
        module = Path(__file__).resolve().parents[2] / "cmake/deps/OpenSSL.cmake"
        (self.root / "main.c").write_text('#include <openssl/crypto.h>\nint main(void) { return OpenSSL_version_num() == 0; }\n')
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(fixture C)\n'
            'set(ASCIICHAT_SHARED_DEPS ON)\n'
            f'include("{module.as_posix()}")\n'
            'add_executable(fixture main.c)\n'
            'target_link_libraries(fixture OpenSSL::SSL OpenSSL::Crypto)\n'
        )
        build = self.root / "build"
        (self.root / "missing/include").mkdir(parents=True)
        subprocess.run(["cmake", "-S", str(self.root), "-B", str(build), "-G", "Ninja",
                        f"-DCMAKE_C_COMPILER={shutil.which('clang')}",
                        f"-DOPENSSL_ROOT_DIR={self.root / 'missing'}",
                        f"-DOPENSSL_INCLUDE_DIR={self.root / 'missing/include'}",
                        f"-DOPENSSL_CRYPTO_LIBRARY={self.root / 'missing/libcrypto.so'}",
                        f"-DOPENSSL_SSL_LIBRARY={self.root / 'missing/libssl.so'}"], check=True)
        subprocess.run(["cmake", "--build", str(build)], check=True)
        subprocess.run([str(build / "fixture")], check=True)

    @unittest.skipUnless(shutil.which("clang") and shutil.which("cmake") and shutil.which("ninja") and importlib.util.find_spec("gcovr"), "Requires CMake, Ninja, Clang and gcovr")
    def test_release_path_mapping_preserves_coverage_source_lookup(self):
        module = Path(__file__).resolve().parents[2] / "cmake/compiler/SourcePaths.cmake"
        source = self.root / "lib/main.c"
        source.parent.mkdir()
        source.write_text('#include <stdio.h>\nint main(void) { puts(__FILE__); return 0; }\n')
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(fixture C)\n'
            f'include("{module.as_posix()}")\nconfigure_source_path_flags()\n'
            'add_executable(fixture lib/main.c)\n'
            'if(ASCIICHAT_ENABLE_COVERAGE)\n'
            'target_compile_options(fixture PRIVATE --coverage)\n'
            'target_link_options(fixture PRIVATE --coverage)\nendif()\n'
        )
        for enabled in ("OFF", "ON"):
            build = self.root / "build" / enabled
            subprocess.run(["cmake", "-S", str(self.root), "-B", str(build), "-G", "Ninja",
                            f"-DCMAKE_C_COMPILER={shutil.which('clang')}", "-DCMAKE_BUILD_TYPE=Release",
                            f"-DASCIICHAT_ENABLE_COVERAGE={enabled}"], check=True)
            subprocess.run(["cmake", "--build", str(build)], check=True)
            output = subprocess.check_output([str(build / "fixture")], text=True).strip()
            if enabled == "OFF":
                self.assertEqual(output, "lib/main.c")
            else:
                self.assertEqual(output, str(source))
                coverage_report.generate(self.root, build, self.report)
                entry = ET.parse(self.report).find('.//class[@filename="lib/main.c"]')
                self.assertIsNotNone(entry)
                self.assertGreater(int(entry.find("lines/line").get("hits")), 0)

    @unittest.skipUnless(shutil.which("clang") and importlib.util.find_spec("gcovr"), "Requires Clang and gcovr")
    def test_real_counters_merge_without_losing_same_basename_sources(self):
        build = self.root / "build"
        build.mkdir()
        clang = shutil.which("clang")
        (build / "CMakeCache.txt").write_text(f"CMAKE_C_COMPILER:FILEPATH={clang}\n")
        objects = []
        for name in ("one", "two"):
            source = self.root / "lib" / name / "same.c"
            source.parent.mkdir(parents=True)
            source.write_text(f"int {name}(void) {{ return 1; }}\n")
            obj = build / f"{name}.o"
            subprocess.run([clang, "--coverage", "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        main = self.root / "lib" / "main.c"
        main.write_text("int one(void); int two(void); int main(void) { return one() + two() == 2 ? 0 : 1; }\n")
        executable = build / "coverage-fixture"
        subprocess.run([clang, "--coverage", str(main), *objects, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        coverage_report.generate(self.root, build, self.report)
        entries = {item.get("filename"): item for item in ET.parse(self.report).findall(".//class")}
        for name in ("one", "two"):
            entry = entries[f"lib/{name}/same.c"]
            self.assertGreater(int(entry.find("lines/line").get("hits")), 0)


if __name__ == "__main__":
    unittest.main()
