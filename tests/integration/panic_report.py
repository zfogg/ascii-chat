"""Check panic-report CLI parsing, filtering and latest-record ownership."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401
from ascii_chat_tests.cli import clean_environment
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


def record(tid=2, seq=1, file="src/client/main.c", macro=0, snippet="return value;", **fields):
    values = dict(pid=7, tid=tid, seq=seq, ts="2026-11-03T00:00:00Z", elapsed="0.1", file=file, line=42, func="worker", macro=macro)
    values.update(fields)
    return " ".join(f"{key}={value}" for key, value in values.items()) + " snippet=" + snippet + "\n"


class PanicReport(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ascii-panic-report-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.log = self.root / "trace.log"
        self.env = clean_environment()

    def run_report(self, *args, expected=0, env=None):
        result = subprocess.run([BINARY, *map(str, args)], env=self.env if env is None else env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout

    def write(self, *lines):
        self.log.write_text("".join(lines))

    def test_latest_sequence_wins_and_threads_are_sorted(self):
        self.write(record(9, 8, snippet="old"), record(2, 3, macro=1, snippet="two words;"), record(9, 10, macro=2, snippet="newest"), record(9, 1, snippet="stale"))
        output = self.run_report("--log-file", self.log)
        self.assertIn("2 threads", output)
        self.assertLess(output.index("tid=2 seq=3"), output.index("tid=9 seq=10"))
        self.assertIn("expansion (1)", output)
        self.assertIn("invocation (2)", output)
        self.assertIn("two words;", output)
        self.assertIn("newest", output)
        self.assertNotIn("stale", output)
        self.assertNotIn("snippet   : old", output)
        self.assertIn("src/client/main.c:42", output)

    def test_equal_sequences_replace_and_single_thread_summary_is_singular(self):
        self.write(record(4, 3, snippet="before"), record(4, 3, macro=99, snippet="after"))
        output = self.run_report("--log-file=" + str(self.log))
        self.assertIn("(1 thread)", output)
        self.assertIn("none (99)", output)
        self.assertIn("after", output)
        self.assertNotIn("before", output)

    def test_include_exclude_and_repeatable_thread_filters_apply_before_selection(self):
        self.write(record(1, 3, file="src/client/a.c"), record(1, 9, file="src/server/a.c"), record(2, 5, file="src/client/ignore.c"), record(3, 4, file="src/client/b.c"))
        output = self.run_report("--log-file", self.log, "--include", "client", "--exclude", "ignore", "--thread", "1", "--thread", "3")
        self.assertIn("tid=1 seq=3", output)
        self.assertIn("tid=3 seq=4", output)
        self.assertNotIn("tid=2", output)
        self.assertNotIn("seq=9", output)
        output = self.run_report("--log-file", self.log, *sum((["--thread", str(i)] for i in range(1, 7)), []))
        self.assertIn("3 threads", output)
        output = self.run_report("--log-file", self.log, "--thread", "999")
        self.assertIn("No instrumentation records matched", output)

    def test_raw_output_preserves_the_selected_complete_line(self):
        line = record(3, 5, snippet="if (x) { two(); }")
        self.write(line)
        output = self.run_report("--log-file", self.log, "--raw")
        self.assertIn(line, output)
        self.assertNotIn("location  :", output)

    def test_malformed_fields_are_ignored_without_losing_valid_records(self):
        valid = record()
        invalid = ["unrelated diagnostic\n"]
        for field in ("pid", "tid", "seq", "ts", "elapsed", "file", "line", "func", "macro", "snippet"):
            invalid.append(" ".join(token for token in valid.split(" ") if not token.startswith(field + "=")))
        for key in ("pid", "tid", "seq", "line", "macro"):
            for value in ("bad", "18446744073709551616", "-1"):
                invalid.append(record(**{key: value}))
        invalid += [record(line=4294967296), record(macro=4294967296)]
        self.write(*invalid, record(8, 6, snippet="valid survivor"))
        output = self.run_report("--log-file", self.log)
        self.assertIn("(1 thread)", output)
        self.assertIn("valid survivor", output)

    def test_directory_collection_ignores_other_names_and_subdirectories(self):
        (self.root / "ascii-instr-one.log").write_text(record(7, 1))
        (self.root / "ascii-instr-two.log").write_text(record(7, 9) + record(4, 1))
        (self.root / "other.log").write_text(record(99, 1))
        (self.root / "ascii-instr-ignore.txt").write_text(record(98, 1))
        (self.root / "ascii-instr-dir.log").mkdir()
        output = self.run_report("--log-dir", self.root)
        self.assertIn("2 threads", output)
        self.assertIn("tid=7 seq=9", output)
        self.assertNotIn("tid=99", output)
        self.assertNotIn("tid=98", output)

    def test_default_directory_uses_each_supported_environment_variable(self):
        (self.root / "ascii-instr-one.log").write_text(record())
        for key in ("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR", "TMPDIR", "TEMP", "TMP"):
            with self.subTest(key=key):
                self.assertIn("(1 thread)", self.run_report(env=dict(self.env, **{key: str(self.root)})))
        self.assertIn("No instrumentation records matched", self.empty_output())

    def empty_output(self):
        self.write("")
        return self.run_report("--log-file", self.log)

    def test_unsigned_boundaries_and_directory_read_failures(self):
        self.write(record(tid=18446744073709551615, seq=18446744073709551615, line=4294967295, macro=4294967295))
        output = self.run_report("--log-file", self.log, "--thread", "18446744073709551615")
        self.assertIn("tid=18446744073709551615 seq=18446744073709551615", output)
        self.assertIn("src/client/main.c:4294967295", output)
        (self.root / "ascii-instr-broken.log").symlink_to(self.root / "missing")
        self.run_report("--log-dir", self.root, expected=1)

    def test_help_exits_successfully(self):
        self.assertIn("--log-file", self.run_report("--help"))

    def test_missing_inputs_and_invalid_arguments_are_errors(self):
        self.run_report("--log-file", self.root / "missing", expected=1)
        self.run_report("--log-dir", self.root / "missing", expected=1)
        for args in (("--thread", "bad"), ("--thread", "18446744073709551616"), ("--unknown",), ("unexpected",), ("--log-file", str(self.log), "--log-dir", str(self.root))):
            with self.subTest(args=args):
                result = subprocess.run([BINARY, *args], env=self.env, capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    BINARY = str(args.binary.resolve())
    unittest.main(argv=[__file__, *remaining])
