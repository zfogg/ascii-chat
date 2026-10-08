export interface ConnectionSecurityLine {
  label: string;
  value: string;
}

/** Describes the encryption layers selected by the browser connection mode. */
export function getConnectionSecurityLines(
  urlValue: string,
  discoveryMode = false,
  applicationEncryption?: boolean,
): ConnectionSecurityLine[] {
  let protocol: string;
  try {
    protocol = new URL(urlValue).protocol;
  } catch {
    return [{ label: "Security", value: "Enter a valid WebSocket URL" }];
  }

  if (protocol !== "ws:" && protocol !== "wss:")
    return [{ label: "Security", value: "Unsupported WebSocket URL scheme" }];

  if (discoveryMode) {
    const lines: ConnectionSecurityLine[] = [
      { label: "Media", value: "WebRTC DataChannel (DTLS encrypted)" },
      {
        label: "Signaling",
        value:
          protocol === "wss:"
            ? "WSS (TLS encrypted)"
            : "WS with X25519 + XSalsa20-Poly1305 AEAD",
      },
    ];
    if (protocol === "wss:" && applicationEncryption) {
      lines.push({
        label: "Signaling application encryption",
        value: "X25519 + XSalsa20-Poly1305 AEAD",
      });
    }
    return lines;
  }

  return protocol === "wss:"
    ? applicationEncryption
      ? [
          { label: "Transport", value: "WSS (TLS encrypted)" },
          {
            label: "Additional application encryption",
            value: "X25519 + XSalsa20-Poly1305 AEAD",
          },
        ]
      : [{ label: "Transport", value: "WSS (TLS encrypted)" }]
    : [
        { label: "Transport", value: "WebSocket (WS)" },
        {
          label: "Application encryption",
          value: "X25519 + XSalsa20-Poly1305 AEAD",
        },
      ];
}
