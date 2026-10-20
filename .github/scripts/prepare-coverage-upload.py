"""Require every coverage shard before publishing one complete Codecov upload."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("coverage_report", Path(__file__).with_name("generate-coverage.py"))
coverage_report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage_report)

EXPECTED_ARTIFACTS = (
    "test-results-Linux-debug",
    "test-results-Linux-release",
    "test-results-macOS-release",
    "integration-test-results-Linux",
    "integration-test-results-macOS",
    "performance-test-results-Linux",
    "performance-test-results-macOS",
)


def prepare(directory):
    reports = []
    lines = {}
    shards = {}
    for name in EXPECTED_ARTIFACTS:
        matches = list((directory / name).rglob("coverage.xml"))
        if len(matches) != 1:
            raise ValueError(f"Expected one coverage.xml for {name}, found {len(matches)}; refusing partial upload")
        report = matches[0]
        covered, valid = coverage_report.validate_report(report)
        junit = list((directory / name).rglob("ctest-results.xml"))
        if len(junit) != 1:
            raise ValueError(f"Missing completed CTest results for {name}; refusing partial upload")
        cases = ET.parse(junit[0]).findall(".//testcase")
        if not cases:
            raise ValueError(f"No tests ran for {name}; refusing partial upload")
        shards[name] = {"covered": covered, "valid": valid, "test_binaries": len(cases)}
        reports.append(report)
        for entry in ET.parse(report).findall(".//class"):
            filename = entry.get("filename")
            for line in entry.findall("lines/line"):
                key = (filename, int(line.get("number")))
                lines[key] = lines.get(key, False) or int(line.get("hits")) > 0

    summary = {"shards": shards}
    for label, prefixes in (("all", ("lib/", "src/", "include/", "tests/")),
                            ("production", ("lib/", "src/", "include/"))):
        values = [hit for (filename, _), hit in lines.items() if filename.startswith(prefixes)]
        covered = sum(values)
        valid = len(values)
        if not valid:
            raise ValueError(f"No {label} source lines in coverage reports")
        summary[label] = {"covered": covered, "valid": valid, "executed_line_percent": 100 * covered / valid}
    return reports, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        reports, summary = prepare(args.directory)
    except (ValueError, OSError, ET.ParseError) as error:
        parser.exit(1, f"Coverage upload preparation failed: {error}\n")
    (args.directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write("files=" + ",".join(report.as_posix() for report in reports) + "\n")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as output:
            output.write("## Complete coverage upload\n\nAll seven coverage shards are present.\n\n")
            for label in ("production", "all"):
                result = summary[label]
                output.write(f"- {label}: {result['covered']}/{result['valid']} executed lines "
                             f"({result['executed_line_percent']:.2f}%)\n")
            output.write("\nCodecov's score also accounts for partially covered branches. "
                         "Original branch data is preserved in each uploaded report.\n")


if __name__ == "__main__":
    main()
