# ICU vcpkg overlay

Based on ICU 78.1#1 from the repository's pinned vcpkg baseline
`530d2d2f0864684f20b32a0596459646906d35fe`.
The upstream port and its patches are retained. The local change supplies
`INSTALL=/usr/bin/install -c` on Windows: Autoconf otherwise records a relative
`install-sh` path that fails from ICU's nested installation directories under
MSYS. Remove this overlay when the pinned upstream port includes a working fix.
