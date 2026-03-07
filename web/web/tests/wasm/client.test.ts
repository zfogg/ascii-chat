/**
 * Unit tests for Client WASM bindings
 *
 * NOTE: These tests verify TypeScript types and enums.
 * Full WASM integration tests require a browser environment
 * and are covered by E2E tests with Playwright.
 */

import { describe, it, expect } from "vitest";
import { ConnectionState, PacketType } from "../../src/wasm/client";

describe("Client WASM Module Types", () => {
  describe("ConnectionState enum", () => {
    it("should have correct state values", () => {
      expect(ConnectionState.DISCONNECTED).toBe(0);
      expect(ConnectionState.CONNECTING).toBe(1);
      expect(ConnectionState.HANDSHAKE).toBe(2);
      expect(ConnectionState.CONNECTED).toBe(3);
      expect(ConnectionState.ERROR).toBe(4);
    });

    it("should be a valid TypeScript enum", () => {
      const state: ConnectionState = ConnectionState.CONNECTED;
      expect(state).toBe(3);
      expect(ConnectionState[state]).toBe("CONNECTED");
    });
  });

  describe("PacketType enum", () => {
    it("should have crypto packet types", () => {
      expect(PacketType.CRYPTO_CLIENT_HELLO).toBe(1000);
      expect(PacketType.CRYPTO_KEY_EXCHANGE_INIT).toBe(1102);
      expect(PacketType.CRYPTO_KEY_EXCHANGE_RESP).toBe(1103);
      expect(PacketType.CRYPTO_HANDSHAKE_COMPLETE).toBe(1108);
    });

    it("should have data packet types", () => {
      expect(PacketType.ENCRYPTED).toBe(1200);
      expect(PacketType.AUDIO_BATCH).toBe(4000);
      expect(PacketType.AUDIO_OPUS_BATCH).toBe(4001);
      expect(PacketType.IMAGE_FRAME).toBe(3001);
      expect(PacketType.ASCII_FRAME).toBe(3000);
    });

    it("should have control packet types", () => {
      expect(PacketType.CLIENT_CAPABILITIES).toBe(5000);
      expect(PacketType.ERROR_MESSAGE).toBe(2003);
      expect(PacketType.PING).toBe(5001);
      expect(PacketType.PONG).toBe(5002);
    });

    it("should have rekey packet types", () => {
      expect(PacketType.CRYPTO_REKEY_REQUEST).toBe(1201);
      expect(PacketType.CRYPTO_REKEY_RESPONSE).toBe(1202);
      expect(PacketType.CRYPTO_REKEY_COMPLETE).toBe(1203);
    });
  });

  describe("Type exports", () => {
    it("should export all necessary types", () => {
      // This test just verifies the types compile
      const state: ConnectionState = ConnectionState.DISCONNECTED;
      const type: PacketType = PacketType.PING;

      expect(state).toBeDefined();
      expect(type).toBeDefined();
    });
  });
});
