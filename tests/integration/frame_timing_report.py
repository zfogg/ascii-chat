"""Summarize native frame stages without treating camera read wait as CPU work."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
from bisect import bisect_left
import json
from pathlib import Path
import re
import statistics


from ascii_chat_tests.timing import summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text(encoding="utf-8", errors="replace")), indent=2))


if __name__ == "__main__":
    main()
