# Page snapshot

```yaml
- generic [ref=e5]:
  - heading "ASCII Chat - Client WASM Demo" [level=1] [ref=e6]
  - paragraph [ref=e7]: WebAssembly client mode with X25519 key exchange and XSalsa20-Poly1305 encryption
  - generic [ref=e8]:
    - heading "Status" [level=2] [ref=e9]
    - paragraph [ref=e10]: WASM initialized successfully
  - generic [ref=e11]:
    - heading "Connection Settings" [level=2] [ref=e12]
    - textbox "ws://localhost:27224" [ref=e14]: ws://localhost:27225
  - generic [ref=e15]:
    - button "Generate Keypair" [ref=e16] [cursor=pointer]
    - button "Connect to Server" [disabled] [ref=e17]
  - generic [ref=e18]:
    - heading "Connection State" [level=2] [ref=e19]
    - generic [ref=e20]:
      - generic [ref=e21]: Disconnected
      - generic [ref=e22]: Connecting
      - generic [ref=e23]: Handshake
      - generic [ref=e24]: Connected
      - generic [ref=e25]: Error
  - generic [ref=e26]:
    - paragraph [ref=e27]:
      - strong [ref=e28]: "WASM Module:"
      - text: client.wasm (1.3MB with Opus codec)
    - paragraph [ref=e29]:
      - strong [ref=e30]: "Crypto:"
      - text: X25519 key exchange, XSalsa20-Poly1305 AEAD
    - paragraph [ref=e31]:
      - strong [ref=e32]: "Audio:"
      - text: Opus codec @ 48kHz mono
    - paragraph [ref=e33]:
      - strong [ref=e34]: "Network:"
      - text: WebSocket with automatic reconnection
```