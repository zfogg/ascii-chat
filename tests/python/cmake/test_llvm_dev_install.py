"""Exercise the Windows LLVM development archive installer without network access."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[3]
INSTALLER = ROOT / "scripts" / "install-llvm-dev.ps1"
MEMBERS = (
    "include/llvm/Support/CommandLine.h",
    "include/llvm-c/DataTypes.h",
    "include/clang-c/Index.h",
    "lib/LLVMSupport.lib",
    "lib/LLVMDemangle.lib",
    "lib/libclang.lib",
    "bin/libclang.dll",
)
pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="Windows installer")


@pytest.fixture
def archive(tmp_path: Path) -> Path:
    tar = Path(os.environ["SystemRoot"]) / "System32" / "tar.exe"
    if not tar.is_file() or shutil.which("pwsh") is None:
        pytest.skip("Windows tar and PowerShell 7 are required")
    prefix = "clang+llvm-23.1.3-x86_64-pc-windows-msvc"
    for member in MEMBERS:
        path = tmp_path / prefix / member
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(member, encoding="utf-8")
    result = tmp_path / "llvm.tar.xz"
    subprocess.run(
        [str(tar), "-cJf", str(result), "-C", str(tmp_path), prefix],
        check=True,
        capture_output=True,
    )
    return result


def run_installer(
    archive: Path, destination: Path, digest: str
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "pwsh",
            "-NoProfile",
            "-File",
            str(INSTALLER),
            "-Architecture",
            "x86_64",
            "-Destination",
            str(destination),
            "-ArchivePath",
            str(archive),
            "-ExpectedSha256",
            digest,
        ],
        capture_output=True,
        text=True,
        check=False,
    )


def test_extracts_only_verified_development_files(
    archive: Path, tmp_path: Path
) -> None:
    destination = tmp_path / "llvm development é"
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    result = run_installer(archive, destination, digest)
    assert result.returncode == 0, result.stdout + result.stderr
    for member in MEMBERS:
        assert (destination / member).read_text(encoding="utf-8") == member

    before = (destination / MEMBERS[0]).stat().st_mtime_ns
    result = run_installer(archive, destination, digest)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (destination / MEMBERS[0]).stat().st_mtime_ns == before


def test_rejects_incorrect_checksum_before_extraction(
    archive: Path, tmp_path: Path
) -> None:
    destination = tmp_path / "rejected"
    result = run_installer(archive, destination, "0" * 64)
    assert result.returncode != 0
    assert "SHA-256 mismatch" in result.stderr
    assert not (destination / MEMBERS[0]).exists()


def test_rejects_reusing_a_different_architecture(archive: Path, tmp_path: Path) -> None:
    destination = tmp_path / "llvm"
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    result = run_installer(archive, destination, digest)
    assert result.returncode == 0, result.stdout + result.stderr
    identity = destination / ".ascii-chat-llvm-identity.json"
    identity.write_text('{"version": "23.1.3", "architecture": "aarch64"}', encoding="utf-8")
    result = run_installer(archive, destination, digest)
    assert result.returncode != 0
    assert "aarch64" in result.stderr
