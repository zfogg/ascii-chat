# Release installer integration tests

These tests download real GitHub releases and execute the installed binary.
They cover a fresh install, a piped reinstall, removal of obsolete managed files,
temporary-file cleanup on success and HTTP failure, and protection of unmanaged
files. The PowerShell test also verifies persistent and current-session PATH
entries and restores PATH afterward. Both suites use disposable directories.

```bash
bash tests/installers/smoke.sh web/www/public/install.sh
```

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/installers/smoke.ps1 -Installer web/www/public/install.ps1
```

Run the Bash suite as both root and a regular user in a Linux container, and on
macOS with the system Bash (3.2). Test Windows with both PowerShell 5.1 and 7.
The tests require network access to GitHub. Bash also needs curl, tar, and either
sha256sum or shasum. No preinstalled Gum is needed.

## Installer behavior

The scripts live in `web/www/public` so the website build serves `/install.sh`
and `/install.ps1`. They select x64 or ARM64 archives from the latest stable
release. Set `ASCII_CHAT_VERSION=v0.12.17` to select a specific release.

- Bash defaults to `~/.local`, or `/usr/local` when run as root (including
  `curl ... | sudo bash`). `ASCII_CHAT_INSTALL_PREFIX` overrides that prefix.
  The archive lives in `PREFIX/lib/ascii-chat`; `PREFIX/bin/ascii-chat` links to
  its executable. When needed, the installer offers a separate `Y/n` prompt
  (Enter means yes) for installed Bash and Zsh shells, reading `/dev/tty` so
  piped installs work. It checks the current PATH and each shell's interactive
  and login startup PATH before offering changes. Bash uses `.bashrc` and its
  first existing login profile (`.bash_profile`, `.bash_login`, or `.profile`),
  defaulting to `.bash_profile`; Zsh uses `${ZDOTDIR:-$HOME}/.zshrc`.
  Marked, guarded blocks prevent duplicate edits and duplicate PATH entries
  when profiles are sourced again. Symlinked profiles are left for manual edits.
  If PATH changes, reload the shell with `exec $SHELL`, open a new terminal,
  or run the printed export command in the current one.
  Noninteractive and sudo installs print instructions without editing profiles.
- PowerShell defaults to `%LOCALAPPDATA%\Programs\ascii-chat`, or
  `%ProgramFiles%\ascii-chat` in an Administrator shell. It updates User or
  Machine PATH respectively, plus the current session. If PATH changes, open a
  new Windows terminal to load it in other sessions.
  `ASCII_CHAT_INSTALL_DIR` overrides the installation directory.
- The whole archive is retained, including bundled documentation and examples.
  Config files in the user's home are untouched. An ownership marker protects
  unrelated installations from replacement. Existing package-manager installs
  should be updated through that package manager.
- Gum 2.0.2 is downloaded temporarily with a pinned SHA-256 checksum. Windows
  ARM64 uses Gum's x64 binary through Windows emulation; ascii-chat itself uses
  the native ARM64 release. Windows verifies the release digest when GitHub
  supplies one. Bash downloads releases over HTTPS; it does not verify GPG
  signatures. These installers do not claim signature verification.
- Downloads and extraction finish, and `--version` succeeds, before replacing
  the installed directory. Replacement retains the old directory for rollback
  until installation succeeds. Temporary archives, Gum, and previous managed
  files are then removed. Force-killing the process or losing power can prevent
  cleanup; ordinary errors and Bash INT/TERM are handled.

To remove an installation made by these scripts, remove its managed directory
and the Bash symlink or Windows PATH entry. Do not use the archive's package
uninstaller: its layout assumes a conventional package installation.

## PATH prompt regression tests

```bash
python3 tests/installers/path_profiles.py web/www/public/install.sh
# Explicitly test macOS's bundled Bash 3.2:
TEST_BASH=/bin/bash python3 tests/installers/path_profiles.py web/www/public/install.sh
```

These use real Bash and Zsh with isolated home directories and a pseudo-terminal.
They cover default acceptance, decline, existing shell-specific PATH entries,
idempotent reruns and repeated sourcing, paths needing shell quoting, Bash login
profile selection, ZDOTDIR, and noninteractive/sudo behavior. They require
Python 3 and Zsh; they do not download releases or edit the user's real profiles.
