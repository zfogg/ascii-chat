# Important terminal notices

Issue #255 uses one layout for important printed messages and interactive prompts.

| Severity | Terminal color | Use |
| --- | --- | --- |
| `UI_NOTICE_INFO` | White | Session strings and join instructions |
| `UI_NOTICE_WARNING` | Yellow | Missing credentials, unavailable features, overwrite confirmations, actionable configuration errors |
| `UI_NOTICE_DANGER` | Red | Identity changes, failed authentication, disabled encryption, privacy exposure |
| `UI_NOTICE_FATAL` | Purple (ANSI magenta) | Fatal errors and their immediate context |

`NOTICE(DANGER, "SERVER IDENTITY CHANGED", "%s", details)` records an ordinary log
message and presents the complete title and body in a box. Do not include borders
or ANSI escapes in the supplied text. Use a literal format string when including
remote messages. The renderer makes terminal control characters visible instead
of executing them. Fingerprints, paths, and commands wrap without ellipses.

`ui_notice_confirm(severity, text, timeout_seconds)` uses the existing yes/no
prompt policy with a No default. This is presentation only: changing a notice
must not change host trust, authentication, timeout, or persistence decisions.
Ordinary text/password prompts also use the shared layout; only their existing
display-safe, masked text reaches the renderer.

The layout uses physical terminal columns, reserves the last column against
autowrap, and caps boxes at 80 columns. It falls back to ASCII borders when UTF-8
is unavailable and to plain wrapping in exceptionally narrow terminals. Color
follows terminal capabilities, `--color`, `--color-mode`, and `--strip-ansi`.
Redirected output has no cursor movement. Terminal boxes do not decorate file
or JSON message bodies; existing log header formatting is retained. Log levels,
quiet mode, and grep filtering still apply to logged notices.
Grep matches the uncolored log template, including severity, source file,
function, and the notice body; box decorations are not part of the filter input.
Session invitations use `NOTICE_ANNOUNCE` to remain visible at every log level;
quiet mode, grep filtering, and JSON output still apply to these announcements.

During live presentation, notices queue above ordinary screens and below input
prompts. Long notices advance a page every ten seconds; resizing restarts the
reading interval. Enter/Escape dismiss a notice through the normal input loop;
quit remains available. Prompt input is never interpreted as notice dismissal.
The display queue holds up to 64 notices; overflow stays in the log and produces
a rate-limited diagnostic. Pending notices are printed in full on controller teardown. Fatal notices retire
the active screens and print synchronously. No new confirmation is required for
ordinary notices. Security prompts retain their original deadlines.

## Coverage checklist

| Family | Covered entry points |
| --- | --- |
| Unknown host | `prompt_unknown_host`: fingerprint prompt, noninteractive refusal, explicit/debug verification bypass |
| Changed host key | `display_mitm_warning`: received/expected fingerprints, known-hosts path, recovery guidance, rejection |
| Missing/removed identity | `prompt_unknown_host_no_identity`, `check_known_host_no_identity`, handshake client/server: unverified peer and identity downgrade |
| Discovery lookup | Expected host-key mismatch rejects the lookup result; insecure lookup without a pinned host key warns about MITM risk |
| ACDS trust | `discovery_keys_verify_change`: old/new fingerprints, operator verification, default-No confirmation and noninteractive refusal |
| Verification failure | Handshake client: invalid server signature, browser-pinned key mismatch, configured server-key mismatch, invalid HMAC |
| Authentication rejection | Handshake reason flags and client protocol: incorrect/missing password, missing/rejected client key, invalid signature |
| Credentials and persistence | Client identity loading, private-key file permissions, known-hosts write/flush failures, discovery identity fallback |
| Encryption | Client/server `--no-encrypt`, encryption mode mismatch, server encryption unavailable, received encryption-policy violation |
| Privacy | Public IP disclosure confirmation/result; discovery disabled when security was not configured |
| TLS downgrade | WebSocket certificate/key configuration falling back to plain WS |
| Session information | LAN-only and global session strings and join commands |
| Operational failures | Invalid startup options, no discovered servers, connection failure, camera/media failure, audio unavailable, discovery/mirror failure |
| Destructive local actions | Config, completions, and man-page overwrite prompts use yellow warnings with their existing No default |
| Fatal failures | `FATAL` context summary and explicit fatal notices use purple boxes; ordinary `log_fatal` records do not retire screens |

Routine packet diagnostics, per-frame errors, backtraces, and debug reports remain
logs. Existing specialized help/update screens retain their own layouts.
WASM uses textual console logging and its existing prompt implementation.

## Verification

Build with the default CMake preset, then run:

```sh
python tests/integration/terminal_notices.py --library build/bin/asciichat.dll
python tests/integration/terminal_prompt.py --library build/bin/asciichat.dll
python tests/integration/prompt_timeouts.py --library build/bin/asciichat.dll
```

The notice suite also accepts a Linux shared-library path. It checks Unicode
and ASCII layouts, seven widths, all four color mappings, complete long
fingerprints, control-character escaping, multiline log retention, grep/JSON
behavior, changed-key rejection, real unknown-host/ACDS prompts, default-No
behavior after resizing, queued live notices, and fatal screen teardown.
Regression cases cover non-terminating fatal-level logging, invitations at
WARN/ERROR/FATAL levels, announcement quiet/grep/JSON behavior, restored terminal
output, and flushing a queued warning before forced exit.
The PTY assertions inspect the rendered terminal cells, not just log output.
The existing prompt suites cover masked input, discarded covered input, absolute
timeouts, noninteractive refusal, and automated responses.
