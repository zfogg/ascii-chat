# Connection security policy

Native clients and the browser use `crypto_policy_resolve()` from
`include/ascii-chat/crypto/policy.h`. Mobile clients should call this same API.
Its result is the existing ACIP mode bitmask: NONE (0), ENCRYPT (1), AUTH (2), or
FULL (3). Authentication and payload encryption are independent.

| Input | Plain TCP / WS | TLS / WSS / WebRTC DTLS |
| --- | --- | --- |
| Default, no credentials | ENCRYPT | NONE |
| Password | FULL | FULL |
| Identity key or pinned server key | FULL | AUTH |
| Explicit encryption | ENCRYPT | ENCRYPT |
| Key and explicit no-encrypt | AUTH | AUTH |
| Password and explicit no-encrypt | AUTH | AUTH |
| No-auth, no credentials | ENCRYPT | NONE |
| No-auth and no-encrypt | NONE | NONE |

An explicit encryption setting takes precedence over transport/password
defaults. Supplying both encrypt and no-encrypt is an error. Supplying
authentication material with no-auth is an error. Keys alone do not force a
second encryption layer over TLS or DTLS.

For example, both peers can authenticate identities without ACIP payload
encryption:

```sh
ascii-chat server --key server_key --client-keys client_key.pub --no-encrypt
ascii-chat client localhost --key client_key --server-key server_key.pub --no-encrypt
```

On plain transports the server must explicitly permit unencrypted payloads
with `--no-encrypt`. A TLS listener permits TLS-only connections by default,
but configured passwords or client allowlists still require authentication.
Server `--encrypt` requires ACIP encryption; server `--no-encrypt` requires
ACIP plaintext. Neither flag disables identity loading or signature checks.

Direct TCP and WebSocket clients begin with PROTOCOL_VERSION and the resolved
mode. The server validates its requirements and echoes that mode. Nonzero modes
continue through capability negotiation, key exchange and authentication.
Application packets are sent only after negotiation succeeds. AUTH retains the
completed crypto context for identity state while transports omit ENCRYPTED
wrappers. WSS continues to encrypt those packets with TLS.

The browser passes its optional encryption override and configured credentials
to WASM. JavaScript does not implement another policy decision table. Its
connection state waits for authentication even when payload encryption is off.
WebRTC always retains DTLS and negotiates the same policy independently for each
media peer using `acip_security_t`. Passwords or explicit encryption add ACIP
ENCRYPTED envelopes; auth-only verifies credentials without those envelopes.
Media is admitted only after peer negotiation succeeds. Discovery signaling has
its own credentials and security context. Both endpoints must support the new
peer negotiation; older WebRTC peers must upgrade.

The CLI rejects `--encrypt --no-encrypt` and `--no-auth` with any authentication
material. The shared policy also rejects these combinations for non-CLI callers.
`--key --server-key` remains valid for mutual authentication. Explicit plaintext
TCP/WS connections retain the ENCRYPTION DISABLED notice; TLS/DTLS connections
do not produce that misleading warning.

For a future iOS caller, fill `crypto_policy_t` using the actual connection's
transport security and user settings, call `crypto_policy_resolve()`, and use the
returned bitmask in negotiation. Do not infer handshake completion from the
encryption bit: AUTH also requires the complete handshake.

Validation:

```sh
cmake --build build --target ascii-chat test-crypto-policy
ctest --test-dir build -R '^crypto-policy$' --output-on-failure
python tests/integration/crypto_policy.py /absolute/path/to/ascii-chat
# From web/web, with the rebuilt WASM bundle:
vp dev --config tests/network/crypto-policy.vite.config.ts
# In another terminal:
vp exec node tests/network/crypto-policy-live.mjs /absolute/path/to/ascii-chat http://127.0.0.1:30831
```

The live browser test records outgoing ACIP packet types, requires a PONG from
the native server, and checks wrong-pin/password rejection before CONNECTED.

WebRTC validation (real native and browser DataChannels):

```sh
cmake --build build --target test-webrtc-security
ctest --test-dir build -R '^webrtc-security$' --output-on-failure
python tests/integration/webrtc_security.py /absolute/path/to/ascii-chat
# From web/web:
vp exec playwright test --config tests/e2e/peer-security.config.ts
```
