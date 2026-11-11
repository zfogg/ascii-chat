import { describe, expect, it } from "vite-plus/test";
import { getConnectionSecurityLines } from "../../src/network/connectionSecurity";

describe("connection security display", () => {
  it("identifies HTTPS as WebTransport with required ACIP encryption", () => {
    expect(
      getConnectionSecurityLines("https://example.test:4433/chat"),
    ).toEqual([
      { label: "Transport", value: "WebTransport (HTTP/3 TLS encrypted)" },
      {
        label: "Application encryption",
        value: "X25519 + XSalsa20-Poly1305 AEAD",
      },
    ]);
  });
  it("does not advertise WebTransport for discovery signaling", () => {
    expect(
      getConnectionSecurityLines("https://example.test/", true)[0]!.value,
    ).toMatch(/Unsupported/);
  });
});
