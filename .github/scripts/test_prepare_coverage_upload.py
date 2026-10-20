import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("upload", Path(__file__).with_name("prepare-coverage-upload.py"))
upload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upload)


class CompleteCoverageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def write_shard(self, name, first=1, second=0):
        report = self.root / name / "build/coverage.xml"
        report.parent.mkdir(parents=True)
        report.write_text(
            '<coverage lines-covered="1" lines-valid="2"><packages><package><classes>'
            '<class filename="lib/example.c"><lines>'
            f'<line number="1" hits="{first}" branch="true" condition-coverage="50% (1/2)"/>'
            f'<line number="2" hits="{second}"/>'
            '</lines></class></classes></package></packages></coverage>'
        )
        (report.parent / "ctest-results.xml").write_text('<testsuite><testcase name="example"/></testsuite>')
        return report

    def test_single_performance_report_cannot_publish_one_percent_snapshot(self):
        self.write_shard("performance-test-results-Linux")
        with self.assertRaisesRegex(ValueError, "refusing partial upload"):
            upload.prepare(self.root)

    def test_missing_one_shard_is_rejected(self):
        for name in upload.EXPECTED_ARTIFACTS[:-1]:
            self.write_shard(name)
        with self.assertRaisesRegex(ValueError, upload.EXPECTED_ARTIFACTS[-1]):
            upload.prepare(self.root)

    def test_all_shards_union_line_hits_without_rewriting_branch_reports(self):
        originals = {}
        for index, name in enumerate(upload.EXPECTED_ARTIFACTS):
            path = self.write_shard(name, first=int(index != 0), second=int(index == 0))
            originals[path] = path.read_bytes()
        reports, summary = upload.prepare(self.root)
        self.assertEqual(set(reports), set(originals))
        self.assertEqual(summary["production"], {"covered": 2, "valid": 2, "executed_line_percent": 100})
        for path, content in originals.items():
            self.assertEqual(path.read_bytes(), content)

    def test_duplicate_report_in_shard_is_rejected(self):
        for name in upload.EXPECTED_ARTIFACTS:
            self.write_shard(name)
        duplicate = self.root / upload.EXPECTED_ARTIFACTS[0] / "coverage.xml"
        duplicate.write_text("duplicate")
        with self.assertRaisesRegex(ValueError, "found 2"):
            upload.prepare(self.root)

    def test_counters_without_completed_test_run_are_rejected(self):
        for name in upload.EXPECTED_ARTIFACTS:
            self.write_shard(name)
        (self.root / upload.EXPECTED_ARTIFACTS[0] / "build/ctest-results.xml").unlink()
        with self.assertRaisesRegex(ValueError, "Missing completed CTest"):
            upload.prepare(self.root)


if __name__ == "__main__":
    unittest.main()
