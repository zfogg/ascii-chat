"""Exercise merged man-page output and overwrite/error paths through the CLI."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def normalize_examples(content):
    # Session examples contain random adjective-noun-noun names. Multi-mode
    # examples randomly choose a mode; preserve their arguments and descriptions.
    text = re.sub(r"\r+\n", "\n", content.decode("utf-8"))
    text = re.sub(r"(?m)^(\.B ascii-chat )?[a-z]+-[a-z]+-[a-z]+(?= |$)",
                  lambda match: (match[1] or "") + "SESSION", text)
    sections = text.split(".SH EXAMPLES\n", 1)
    if len(sections) == 2:
        examples, separator, remainder = sections[1].partition(".SH ")
        examples = re.sub(r"(?m)^(\.B ascii-chat )(server|client|mirror|discovery-service|mcp-server)(?= |$)",
                          r"\1MODE", examples)
        text = sections[0] + ".SH EXAMPLES\n" + examples + separator + remainder
    return text


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ascii-manpage-") as tmp:
        directory = Path(tmp)
        env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
        env.update(CLAUDECODE="1", HOME=tmp, USERPROFILE=tmp, APPDATA=tmp, XDG_CONFIG_HOME=tmp)

        def run(*args, answer=None, success=True):
            invocation_env = dict(env)
            if answer is not None:
                invocation_env["ASCII_CHAT_QUESTION_PROMPT_RESPONSE"] = answer
            result = subprocess.run([str(binary), "--man-page-create", *map(str, args)],
                                    cwd=directory, env=invocation_env, capture_output=True, timeout=20)
            if (result.returncode == 0) != success:
                raise AssertionError(f"Unexpected exit {result.returncode}: {result.stderr[-2000:]!r}")
            return result.stdout

        stdout = normalize_examples(run())
        for section in ("NAME", "SYNOPSIS", "OPTIONS", "EXAMPLES", "ENVIRONMENT"):
            if ".SH " + section + "\n" not in stdout:
                raise AssertionError(f"Missing {section} section")
        for marker in ("AUTO-START:", "AUTO-END:", "MERGE-START:", "MERGE-END:", "MANUAL-START:"):
            if marker in stdout:
                raise AssertionError(f"Control marker leaked into output: {marker}")
        for text in (".SH MCP SERVER", "ascii-chat mcp-server", "render_visualization", "output_path", "render-font-size"):
            if text not in stdout:
                raise AssertionError(f"Missing MCP documentation: {text}")
        output = directory / "ascii-chat.1"
        run(output)
        if normalize_examples(output.read_bytes()) != stdout:
            raise AssertionError("File output differs from stdout")
        output.write_bytes(b"Preserve this existing page\n")
        run(output, answer="n", success=False)
        if output.read_bytes() != b"Preserve this existing page\n":
            raise AssertionError("Declined overwrite modified the file")
        run(output, answer="y")
        if normalize_examples(output.read_bytes()) != stdout:
            raise AssertionError("Confirmed overwrite differs from stdout")
        run(directory / "missing" / "page.1", success=False)
        print("PASS: stdout, file output, declined overwrite, accepted overwrite, open failure")


if __name__ == "__main__":
    main()
