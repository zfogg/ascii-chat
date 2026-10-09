# Interactive prompt deadlines

Prompts use absolute monotonic deadlines. Typing, resizing, invalid input, and
covered screens do not restart the timer. Expiration never accepts a default Yes
or submits a partial password.

| Prompt | Timeout | Expiration |
| --- | ---: | --- |
| Text, confirmation, overwrite, IP disclosure | 30 seconds | Decline/cancel |
| Password or SSH/GPG passphrase | 60 seconds | Clear input and cancel authentication |
| Host identity or discovery-key verification | 120 seconds | Reject trust |
| mDNS server selection | 30 seconds | Cancel connection |
| Update notification | 10 seconds | Continue normally |

These deadlines are fixed; no command-line or environment override is needed.

A timeout removes the prompt and prints a diagnostic to stderr. It returns an
error to the caller rather than terminating inside the input library. Required
startup prompts fail startup; an update notice continues normally; refusing IP
disclosure allows the server to continue without discovery registration.

Without an interactive terminal, prompts return immediately. Explicit
`ASCII_CHAT_QUESTION_PROMPT_RESPONSE` answers are consumed before checking the
terminal, including by password wrappers. Unattended mDNS clients should supply
a server address. Supply authentication credentials through the existing
password/key options rather than waiting for a prompt.

The native server handshake allows two prompt budgets plus 30 seconds of
transport overhead. Each budget uses the longest prompt (120 seconds).
Older servers and external network timeouts may still close the connection sooner.

## Verification

Build the binary and `ascii-chat-shared` target, then run:

```powershell
python tests/integration/prompt_timeouts.py --library build/bin/asciichat.dll
python tests/integration/terminal_prompt.py --library build/bin/asciichat.dll
```

The deadline regression suite uses real terminals and pipes held open without
EOF. It checks text/password/hidden/yes-no/update/mDNS expiration, typing without
extension, secret clearing, successful input, cancellation, and automated
password responses. Use the corresponding shared-library path on POSIX.
