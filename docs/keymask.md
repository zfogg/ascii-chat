# Keymask public-key art

Keymask is ascii-chat's deterministic, monochrome public-key emblem. It supplements
the full SHA-256 fingerprint and existing authentication checks. A similar-looking
picture is not proof that two keys match.

Use `--key-art=auto|on|off` (or `ASCII_CHAT_KEY_ART`). The default, `auto`, shows art
on interactive stderr terminals outside snapshot mode. `on` also allows ASCII art
in redirected output. `off` keeps the textual fingerprint. Quiet, grep, and JSON
output retain their existing notice policies.

## Stable v1 mapping

1. Hash the 32 public-key bytes with SHA-256. Do not hash the file, key comment,
   source URL, GPG identifier, private key, or converted ephemeral exchange key.
2. Read the 32 digest bytes in order, most significant bit first within each byte.
   Each pair of bytes supplies one row of a 16 by 16 bitmap.
3. Append the horizontal reflection of each row, producing a 32 by 16 bitmap.
4. Render one bits as `█` in UTF-8 or `#` in ASCII, and zero bits as spaces.
   Surround it with a fixed ASCII border: 34 columns and 18 rows overall.
5. Display the algorithm/key type, `Keymask v1 / SHA-256`, and the complete
   lowercase hexadecimal `SHA256:` fingerprint.

Every digest bit occupies a unique position in the left half, so different digests
produce different exact bitmaps. This does not establish perceptual uniqueness or
change the security properties of the underlying hash. No smoothing, rescaling,
color dependence, or removal of isolated bits is performed.

SSH, raw X25519, and GPG Ed25519 public-key objects use the same mapping. Identical
public bytes produce identical art regardless of file, agent, or network source.
The algorithm label distinguishes Ed25519 from X25519; GPG metadata is not part of
the hash. The fingerprint convention is ascii-chat's existing SHA-256 of raw key
bytes, not OpenSSH's hash of its serialized SSH key blob.

## Display behavior

- Servers announce their configured public identities at startup; discovery-service
  announces its public identity too.
- Clients show the presented server identity after the existing key checks. The
  label explicitly says unverified if signature verification was not enabled.
- Servers show a client's public identity after handshake completion, labeled with
  its connection ID and authenticated/unverified status. Anonymous clients have no
  persistent key art.
- Unknown-host prompts include art when it fits. Short terminals (fewer than 38
  rows) keep just the fingerprint so the question and input remain accessible.
- Changed-host warnings show received and stored identities. When several stored
  keys exist, the first nonzero candidate is labeled as such. Missing stored
  identities are described as unavailable, never visualized as a zero key.
- If the available notice content width is below 34 columns, only the fingerprint
  is shown. The existing UI controller handles later terminal resizes and prompt
  minimum dimensions.

Trust decisions, known-host file format, and network packets are unchanged.

## Implementation and verification

`lib/crypto/key_identity/keymask.c` contains the allocation-free bitmap renderer
and shared public-key digest function. `display.c` formats identity cards and
applies terminal/notice policy. Public headers mirror that directory under
`include/ascii-chat/crypto/key_identity/`.

Build using the default CMake preset, then run against the compiled shared library:

```sh
python tests/integration/keymask.py --library build/bin/asciichat.dll --terminal --binary build/bin/ascii-chat.exe
```

Use the corresponding `.so`/`.dylib` path on other platforms. The core checks use
only Python's standard library. `--terminal` additionally requires `pyte` and
`pywinpty` on Windows, or `pexpect` on POSIX. `--contact-sheet output.png` uses
Pillow to render 36 deterministic examples from the native renderer.

The tests cover fixed vectors, all 256 individual bit positions, ASCII/UTF-8
equivalence, buffer boundaries, SHA-256 parity, public-key imports, narrow layouts,
stored-key comparisons, noninteractive rejection, and real PTY/ConPTY notices and
trust prompts. `--binary` additionally exercises a real loopback server/client
authenticated snapshot with temporary SSH keys (requires `ssh-keygen`), checking
both identity cards and that snapshot stdout stays clean. They use temporary fixtures and isolate the user config directory.

## Example contact sheet

These 36 synthetic digest fixtures are rendered by the compiled C implementation.

![36 Keymask v1 examples](../images/keymask-contact-sheet.png)
