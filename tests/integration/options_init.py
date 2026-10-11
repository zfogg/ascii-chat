#!/usr/bin/env python3
"""Exercise the complete options initialization pipeline through the CLI.

Usage: python tests/integration/options_init.py build/bin/ascii-chat.exe
Runs each case in a fresh process because option actions may exit immediately.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("ASCII_CHAT_")}
    env.update(CLAUDECODE="1", NO_COLOR="1")
    count = 0
    with tempfile.TemporaryDirectory(prefix="ascii-chat-options-") as directory:
        # Keep generated logs and configuration separate from the user's files.
        env.update(HOME=directory, USERPROFILE=directory, APPDATA=directory,
                   XDG_CONFIG_HOME=directory)

        def check(arguments, success, expected, extra_env=None):
            nonlocal count
            result = subprocess.run(
                [str(binary), *arguments], cwd=directory,
                env={**env, **(extra_env or {})}, capture_output=True,
                encoding="utf-8", errors="replace", timeout=30,
            )
            output = result.stdout + result.stderr
            assert (result.returncode == 0) == success, (
                arguments, result.returncode, output)
            assert expected in output, (arguments, expected, output)
            count += 1
            print(f"PASS: {' '.join(arguments)}")
            return result

        check(["--help"], True, "POSITIONAL ARGUMENTS")
        check(["mirror", "--help"], True, "ascii-chat mirror")
        check(["--version"], True, "ascii-chat v")
        for arguments, message in [
            (["--log-level"], "--log-level requires a value"),
            (["--log-level", "bad"], "Invalid log level 'bad'"),
            (["--log-file"], "requires a file path"),
            (["--completions"], "--completions requires shell name"),
            (["mirror", "--quiet"], "must appear before the mode"),
            (["mirror", "--not-an-option"], "Unknown option: --not-an-option"),
            (["mirror", "--width", "bad"], "not a valid integer"),
            (["mirror", "--seek", "3"], "--seek requires --file or --url"),
            (["mirror", "--pause"], "--pause requires --file or --url"),
            (["mirror", "--file", "-", "--seek", "3"], "--seek cannot be used with stdin"),
            (["mirror", "--url", "bad url"], "--url must be a valid HTTP(S) URL"),
            (["client", "--key", "./missing-key"], "Key file not found"),
            (["--color=false", "mirror", "--color-filter", "rainbow"],
             "--color-filter cannot be used with --color=false"),
        ]:
            check(arguments, False, message)

        # Deferred actions run only after mode parsing, validation, and publication.
        check(["--show-capabilities"], True, "Terminal Capabilities:")
        for mode in [["mirror"], ["client"]]:
            check([*mode, "--width", "100", "--height", "30", "--show-capabilities"],
                  True, "Terminal Size: 100x30")
        for mode in ["server", "discovery-service"]:
            check([mode, "--show-capabilities"], True, "Terminal Capabilities:")
        for prefix in [["--no-check-update"], ["--quiet", "--verbose", "2"],
                       ["--color=false"], ["--log-level", "error"]]:
            check([*prefix, "mirror", "--width", "100", "--height", "30",
                   "--show-capabilities"], True, "Terminal Size: 100x30")
        check(["mirror", "--width", "100", "--height", "30", "--show-capabilities"],
              True, "Terminal Size: 100x30",
              {"ASCII_CHAT_WIDTH": "80", "ASCII_CHAT_HEIGHT": "24"})
        check(["mirror", "--palette-chars", "abc", "--show-capabilities"],
              True, "Terminal Capabilities:")
        # Windows' narrow main() argv depends on the active code page.
        if os.name != "nt":
            check(["mirror", "--palette-chars", "\u2591\u2592\u2593", "--utf8=false",
                   "--show-capabilities"], False, "non-ASCII characters but --utf8=false")
        snapshot = check(["mirror", "--test-pattern", "--audio=false", "--snapshot",
                          "--snapshot-delay", "0", "--width", "40", "--height", "12",
                          "--color-mode", "none", "--splash-screen=false"], True, "")
        assert snapshot.stdout.strip(), "Snapshot should contain a rendered frame"
    print(f"All {count} options initialization checks passed.")


if __name__ == "__main__":
    main()
