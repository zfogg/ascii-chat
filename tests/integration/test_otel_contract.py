"""Capture the native adapter in process and use Spaniel's offline verifier."""

import argparse
import importlib
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
importlib.import_module("_environment")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("--verifier", default=shutil.which("spaniel-assert"))
    args = parser.parse_args()
    if not args.verifier:
        print(
            "SKIP: install the pinned Spaniel spaniel-assert CLI to verify offline traces"
        )
        return 77
    with tempfile.TemporaryDirectory(prefix="ascii-chat-otel-") as temporary:
        capture = pathlib.Path(temporary) / "contract.pb"
        subprocess.run(
            [str(args.binary.resolve()), str(capture)], check=True, timeout=60
        )
        subprocess.run(
            [
                args.verifier,
                "--format",
                "otlp-proto",
                "--input",
                str(capture),
                "--spec",
                str(pathlib.Path(__file__).with_name("otel_contract.json")),
            ],
            check=True,
            timeout=30,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
