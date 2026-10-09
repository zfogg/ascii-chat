Interactive prompts could wait indefinitely, leaving terminal sessions and automation stuck. Add fixed absolute deadlines: 30 seconds for ordinary prompts and mDNS selection, 60 seconds for secrets, 120 seconds for host/key verification, and 10 seconds for update notices. Expiration declines/cancels input, clears partial secrets, and emits a stderr diagnostic; update notices continue normally.

Remove blocking noninteractive stdin fallbacks, allow explicit automated password answers, make completion-generation timeouts fail, and give native server handshakes enough time for peer prompts. Include the WASM API stub and user documentation. There is no CLI or environment timeout override.

Validation on Windows with Clang/Ninja:
- Built ascii-chat and ascii-chat-shared.
- Native ConPTY deadline regressions cover text, masked/hidden passwords, default-yes confirmations, mDNS, updates, successful input, cancellation, pipes held open, and automated password responses.
- CLI overwrite checks use the fixed 30-second deadline, preserve existing completion/config/manpage files, and exit nonzero on expiration.
- Existing terminal prompt regression passes editing, masking, resize recovery, fingerprint display, and background isolation.
- git diff --check passes.

Linux/macOS runtime tests and a WASM build were not run. Older peers and external network timeouts can still impose shorter deadlines.

Closes #278.
