"""Exercise the QUIC TLS configure check with incompatible OpenSSL fixtures."""

import argparse
from pathlib import Path
import subprocess
import tempfile


parser = argparse.ArgumentParser()
parser.add_argument("--cmake", required=True)
parser.add_argument("--cc", required=True)
parser.add_argument("--module", required=True)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="ascii-chat-quic-tls-") as temporary:
    root = Path(temporary)
    include = root / "fake" / "openssl"
    include.mkdir(parents=True)
    header = include / "ssl.h"
    module = Path(args.module).resolve().as_posix()
    (root / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(quic_tls_probe C)\n"
        "add_library(OpenSSL::SSL INTERFACE IMPORTED)\n"
        'set_target_properties(OpenSSL::SSL PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/fake")\n'
        f'include("{module}")\n'
        "asciichat_check_quic_tls()\n",
        encoding="utf-8",
    )

    def configure(version, implementation):
        header.write_text(
            f"#define OPENSSL_VERSION_NUMBER {version}\n"
            "typedef struct ssl_st SSL;\n"
            "typedef struct ossl_dispatch_st OSSL_DISPATCH;\n"
            f"{implementation}\n",
            encoding="utf-8",
        )
        return subprocess.run(
            [args.cmake, "-S", str(root), "-B", str(root / "build"), "-G", "Ninja", f"-DCMAKE_C_COMPILER={args.cc}"],
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )

    inline_api = (
        "static inline int SSL_set_quic_tls_cbs(SSL *s, const OSSL_DISPATCH *d, void *p) "
        "{ (void)s; (void)d; (void)p; return 0; }"
    )
    result = configure("0x30600000L", inline_api)
    if result.returncode:
        raise RuntimeError(f"Compatible header fixture was rejected:\n{result.stdout}\n{result.stderr}")

    # Reconfigure the same directory to ensure the cached successful result is
    # not reused after the selected OpenSSL changes.
    for version, implementation in (
        ("0x30400000L", inline_api),
        ("0x30600000L", "int SSL_set_quic_tls_cbs(SSL *, const OSSL_DISPATCH *, void *);"),
    ):
        result = configure(version, implementation)
        output = result.stdout + result.stderr
        if result.returncode == 0 or "QUIC requires OpenSSL 3.6.0 or newer" not in output:
            raise RuntimeError(f"Incompatible OpenSSL fixture was not rejected clearly:\n{output}")
