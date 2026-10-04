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
