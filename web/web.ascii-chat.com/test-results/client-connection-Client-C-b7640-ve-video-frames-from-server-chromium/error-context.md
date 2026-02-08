# Page snapshot

```yaml
- generic [ref=e5]:
  - heading "ASCII Chat - Client WASM Demo" [level=1] [ref=e6]
  - paragraph [ref=e7]: WebAssembly client mode with X25519 key exchange and XSalsa20-Poly1305 encryption
  - generic [ref=e8]:
    - heading "Status" [level=2] [ref=e9]
    - paragraph [ref=e10]: Connecting to server...
  - generic [ref=e11]:
    - heading "Connection Settings" [level=2] [ref=e12]
    - textbox "ws://localhost:27224" [ref=e14]: ws://localhost:27225
  - generic [ref=e15]:
    - button "Generate Keypair" [ref=e16] [cursor=pointer]
    - button "Connect to Server" [active] [ref=e17] [cursor=pointer]
  - generic [ref=e18]:
    - heading "Client Public Key" [level=2] [ref=e19]
    - code [ref=e21]: 03449f3bec39b5ad4dfc32c2cadabe53ae57a079c19c198ec0072e03d75f6743
  - generic [ref=e22]:
    - heading "Connection State" [level=2] [ref=e23]
    - generic [ref=e24]:
      - generic [ref=e25]: Disconnected
      - generic [ref=e26]: Connecting
      - generic [ref=e27]: Handshake
      - generic [ref=e28]: Connected
      - generic [ref=e29]: Error
  - generic [ref=e30]:
    - paragraph [ref=e31]:
      - strong [ref=e32]: "WASM Module:"
      - text: client.wasm (1.3MB with Opus codec)
    - paragraph [ref=e33]:
      - strong [ref=e34]: "Crypto:"
      - text: X25519 key exchange, XSalsa20-Poly1305 AEAD
    - paragraph [ref=e35]:
      - strong [ref=e36]: "Audio:"
      - text: Opus codec @ 48kHz mono
    - paragraph [ref=e37]:
      - strong [ref=e38]: "Network:"
      - text: WebSocket with automatic reconnection
```