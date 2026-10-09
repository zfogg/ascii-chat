Interactive prompts could wait indefinitely, leaving terminal sessions and automation stuck. Add absolute deadlines: 30 seconds for ordinary prompts and mDNS selection, 60 seconds for secrets, 120 seconds for host/key verification, and 10 seconds for update notices. Expiration declines/cancels input, clears partial secrets, and emits a stderr diagnostic; update notices continue normally.

Add --prompt-timeout / ASCII_CHAT_PROMPT_TIMEOUT overrides, including early overwrite actions. Remove blocking noninteractive stdin fallbacks, allow explicit automated password answers, make completion-generation timeouts fail, and give native server handshakes enough time for peer prompts. Include the WASM API stub and user documentation.

Validation on Windows with Clang/Ninja:
- Built ascii-chat and ascii-chat-shared.
- Native ConPTY deadline regressions cover text, masked/hidden passwords, default-yes confirmations, mDNS, updates, successful input, cancellation, pipes held open, and automated password responses.
- CLI overwrite checks preserve existing completion/config/manpage files and exit nonzero on expiration; invalid timeout values are rejected.
- Existing terminal prompt regression passes editing, masking, resize recovery, fingerprint display, and background isolation.
- git diff --check passes.

Linux/macOS runtime tests and a WASM build were not run. Longer client overrides require a corresponding server allowance; older peers can still impose shorter network deadlines.

Closes #278.
