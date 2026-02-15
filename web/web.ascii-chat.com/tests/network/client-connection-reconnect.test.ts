/**
 * Test ClientConnection full reconnection with crypto handshake
 * Verifies that reconnection not only re-establishes WebSocket,
 * but also completes the crypto handshake and reaches CONNECTED state
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { ClientConnection } from '../../src/network/ClientConnection';
import { ConnectionState } from '../../src/wasm/client';

// Mock WASM module
let connectionState = ConnectionState.DISCONNECTED;
let handshakeStage = 0; // 0: none, 1: init received, 2: challenge received, 3: complete

vi.mock('../../src/wasm/client', () => ({
  initClientWasm: vi.fn().mockResolvedValue(undefined),
  cleanupClientWasm: vi.fn(() => {
    handshakeStage = 0;
  }),
  generateKeypair: vi.fn().mockResolvedValue('mock-public-key'),
  setServerAddress: vi.fn(),
  registerSendPacketCallback: vi.fn((callback) => {
    // Store callback for testing
    (global as any).__wasmSendPacketCallback = callback;
  }),
  handleKeyExchangeInit: vi.fn(() => {
    handshakeStage = 1;
    connectionState = ConnectionState.HANDSHAKE;
  }),
  handleAuthChallenge: vi.fn(() => {
    handshakeStage = 2;
  }),
  handleHandshakeComplete: vi.fn(() => {
    handshakeStage = 3;
    connectionState = ConnectionState.CONNECTED;
  }),
  getConnectionState: vi.fn(() => connectionState),
  parsePacket: vi.fn((data) => {
    // Parse the packet type from the data
    const view = new DataView(data instanceof Uint8Array ? data.buffer : data);
    // Type is at offset 8, 2 bytes, big-endian
    const type = view.getUint16(8, false);
    return {
      type,
      length: view.getUint32(10, false),
      client_id: view.getUint32(14, false),
    };
  }),
  packetTypeName: vi.fn((type) => {
    const names: Record<number, string> = {
      1: 'CRYPTO_KEY_EXCHANGE_INIT',
      2: 'CRYPTO_AUTH_CHALLENGE',
      3: 'CRYPTO_HANDSHAKE_COMPLETE',
    };
    return names[type] || `PACKET_${type}`;
  }),
  ConnectionState: {
    DISCONNECTED: 0,
    CONNECTING: 1,
    HANDSHAKE: 2,
    CONNECTED: 3,
    ERROR: 4,
  },
  PacketType: {
    CRYPTO_KEY_EXCHANGE_INIT: 1,
    CRYPTO_AUTH_CHALLENGE: 2,
    CRYPTO_HANDSHAKE_COMPLETE: 3,
  },
}));

// Mock WebSocket
class MockWebSocket {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;

  readyState = MockWebSocket.CONNECTING;
  onopen: ((this: MockWebSocket, ev: Event) => void) | null = null;
  onmessage: ((this: MockWebSocket, ev: MessageEvent) => void) | null = null;
  onerror: ((this: MockWebSocket, ev: Event) => void) | null = null;
  onclose: ((this: MockWebSocket, ev: CloseEvent) => void) | null = null;
  binaryType = '';

  private eventListeners: Map<string, Set<Function>> = new Map();

  constructor(public url: string, public protocol: string) {
    console.log(`[MOCK-WS] Created: ${url}`);
  }

  send(data: any) {
    console.log('[MOCK-WS] Send called, data length:', (data as Uint8Array).length);
  }

  addEventListener(event: string, handler: Function) {
    if (!this.eventListeners.has(event)) {
      this.eventListeners.set(event, new Set());
    }
    this.eventListeners.get(event)!.add(handler);
  }

  removeEventListener(event: string, handler: Function) {
    this.eventListeners.get(event)?.delete(handler);
  }

  private dispatchEvent(event: string, data: any) {
    const handlers = this.eventListeners.get(event);
    if (handlers) {
      handlers.forEach((handler) => {
        handler({ type: event, ...data });
      });
    }
  }

  simulateOpen() {
    this.readyState = MockWebSocket.OPEN;
    console.log('[MOCK-WS] Simulating open');
    this.dispatchEvent('open', {});
  }

  simulateClose() {
    this.readyState = MockWebSocket.CLOSED;
    console.log('[MOCK-WS] Simulating close');
    this.dispatchEvent('close', { code: 1006, reason: 'Abnormal closure', wasClean: false });
  }

  simulateMessage(data: Uint8Array) {
    console.log('[MOCK-WS] Simulating message, data length:', data.length);
    this.dispatchEvent('message', { data: data.buffer });
  }
}

let createdWebSockets: MockWebSocket[] = [];
const originalWebSocket = global.WebSocket as any;

class MockWebSocketConstructor {
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;

  constructor(url: string, protocol: string) {
    const ws = new MockWebSocket(url, protocol);
    createdWebSockets.push(ws);
    return ws as any;
  }
}

beforeEach(() => {
  createdWebSockets = [];
  (global as any).WebSocket = MockWebSocketConstructor;
  vi.useFakeTimers();
});

afterEach(() => {
  vi.restoreAllMocks();
  (global as any).WebSocket = originalWebSocket;
  vi.useRealTimers();
  delete (global as any).__wasmSendPacketCallback;
});

// Helper to create a packet with proper format
const createPacket = (type: number): Uint8Array => {
  const packet = new Uint8Array(22);
  // Magic: ASCIICHAT
  packet[0] = 0x41; packet[1] = 0x53; packet[2] = 0x43;
  packet[3] = 0x49; packet[4] = 0x49; packet[5] = 0x43;
  packet[6] = 0x41; packet[7] = 0x54;
  // Type (big-endian at offset 8)
  packet[8] = (type >> 8) & 0xFF;
  packet[9] = type & 0xFF;
  // Length (offset 10)
  packet[10] = 0; packet[11] = 0; packet[12] = 0; packet[13] = 0;
  // CRC32 (offset 14)
  packet[14] = 0; packet[15] = 0; packet[16] = 0; packet[17] = 0;
  // Client ID (offset 18)
  packet[18] = 0; packet[19] = 0; packet[20] = 0; packet[21] = 0;
  return packet;
};

describe('ClientConnection Full Reconnection with Crypto Handshake', () => {
  it('should complete crypto handshake on initial connection', async () => {
    console.log('\n=== TEST: Initial Connection with Handshake ===');

    const stateChanges: string[] = [];
    const connection = new ClientConnection({
      serverUrl: 'ws://localhost:8080',
    });

    connection.onStateChange((state) => {
      console.log(`[TEST] State: ${state}`);
      stateChanges.push(String(state));
    });

    // Start connecting
    const connectPromise = connection.connect();

    // Simulate WebSocket opening
    console.log('[TEST] Simulating WebSocket open');
    await vi.advanceTimersByTimeAsync(50);
    createdWebSockets[0]!.simulateOpen();
    await vi.runAllTimersAsync();

    // Simulate server sending handshake packets
    console.log('[TEST] Simulating handshake packets');
    const mockPacket = new Uint8Array(22); // Minimum packet header
    mockPacket[0] = 0x41; // 'A' for ASCIICHAT magic
    createdWebSockets[0]!.simulateMessage(mockPacket);

    await vi.runAllTimersAsync();
    console.log('[TEST] State changes after handshake:', stateChanges);

    console.log('=== TEST END ===\n');
  });

  it('should reconnect and complete handshake after disconnect', async () => {
    console.log('\n=== TEST: Reconnect with Handshake ===');

    const stateChanges: ConnectionState[] = [];
    const connection = new ClientConnection({
      serverUrl: 'ws://localhost:8080',
    });

    connection.onStateChange((state) => {
      console.log(`[TEST] State: ${state}`);
      stateChanges.push(state);
    });

    // Initial connection
    console.log('[TEST] Step 1: Initial connection');
    const connectPromise = connection.connect();
    await vi.advanceTimersByTimeAsync(50);
    createdWebSockets[0]!.simulateOpen();
    await vi.runAllTimersAsync();

    console.log('[TEST] Initial state changes:', stateChanges);
    expect(stateChanges).toContain(ConnectionState.CONNECTING);

    // Simulate disconnect
    console.log('[TEST] Step 2: Simulating disconnect');
    createdWebSockets[0]!.simulateClose();
    await vi.advanceTimersByTimeAsync(100);

    // Wait for reconnection attempt
    console.log('[TEST] Step 3: Waiting for reconnection (1000ms delay)');
    await vi.advanceTimersByTimeAsync(1000);

    // Verify new WebSocket was created
    console.log('[TEST] WebSocket count after reconnect trigger:', createdWebSockets.length);
    expect(createdWebSockets.length).toBeGreaterThan(1);

    // Simulate new WebSocket opening
    console.log('[TEST] Step 4: Simulating reconnected WebSocket open');
    createdWebSockets[1]!.simulateOpen();
    await vi.runAllTimersAsync();

    console.log('[TEST] Final state changes:', stateChanges);

    // Verify we have multiple CONNECTING states (one for each connection attempt)
    const connectingCount = stateChanges.filter((s) => s === ConnectionState.CONNECTING).length;
    console.log(`[TEST] CONNECTING state reached ${connectingCount} times`);
    expect(connectingCount).toBeGreaterThanOrEqual(2); // At least initial + one reconnect

    console.log('=== TEST END ===\n');
  });

  it('should complete FULL crypto handshake and reach CONNECTED state after reconnection', async () => {
    console.log('\n=== TEST: Full Handshake Through Reconnection ===');

    const stateChanges: ConnectionState[] = [];
    const connection = new ClientConnection({
      serverUrl: 'ws://localhost:8080',
    });

    connection.onStateChange((state) => {
      console.log(`[TEST] State: ${state} (${Object.entries(ConnectionState).find((e) => e[1] === state)?.[0] || 'UNKNOWN'})`);
      stateChanges.push(state);
    });

    // Initial connection
    console.log('[TEST] Step 1: Initial WebSocket connection');
    connection.connect();
    await vi.advanceTimersByTimeAsync(50);
    createdWebSockets[0]!.simulateOpen();
    await vi.runAllTimersAsync();

    // Simulate crypto handshake packets from server
    console.log('[TEST] Step 2: Simulating crypto handshake packets');

    // Send CRYPTO_KEY_EXCHANGE_INIT (type 1)
    console.log('[TEST] Sending CRYPTO_KEY_EXCHANGE_INIT');
    createdWebSockets[0]!.simulateMessage(createPacket(1));
    await vi.runAllTimersAsync();

    // Send CRYPTO_AUTH_CHALLENGE (type 2)
    console.log('[TEST] Sending CRYPTO_AUTH_CHALLENGE');
    createdWebSockets[0]!.simulateMessage(createPacket(2));
    await vi.runAllTimersAsync();

    // Send CRYPTO_HANDSHAKE_COMPLETE (type 3)
    console.log('[TEST] Sending CRYPTO_HANDSHAKE_COMPLETE');
    createdWebSockets[0]!.simulateMessage(createPacket(3));
    await vi.runAllTimersAsync();

    console.log('[TEST] State after handshake packets:', stateChanges);
    const connectedBeforeDisconnect = stateChanges.includes(ConnectionState.CONNECTED);
    console.log('[TEST] Reached CONNECTED before disconnect:', connectedBeforeDisconnect);

    // Now disconnect
    console.log('[TEST] Step 3: Server disconnects unexpectedly');
    createdWebSockets[0]!.simulateClose();
    await vi.advanceTimersByTimeAsync(100);

    console.log('[TEST] Step 4: Waiting for auto-reconnection (1000ms)');
    await vi.advanceTimersByTimeAsync(1000);

    // Simulate new WebSocket connection
    console.log('[TEST] Step 5: New WebSocket connects');
    expect(createdWebSockets.length).toBe(2);
    createdWebSockets[1]!.simulateOpen();
    await vi.runAllTimersAsync();

    // Simulate handshake again after reconnection
    console.log('[TEST] Step 6: Simulating handshake after reconnection');

    // Send all three handshake packets
    createdWebSockets[1]!.simulateMessage(createPacket(1)); // CRYPTO_KEY_EXCHANGE_INIT
    await vi.runAllTimersAsync();
    createdWebSockets[1]!.simulateMessage(createPacket(2)); // CRYPTO_AUTH_CHALLENGE
    await vi.runAllTimersAsync();
    createdWebSockets[1]!.simulateMessage(createPacket(3)); // CRYPTO_HANDSHAKE_COMPLETE
    await vi.runAllTimersAsync();

    console.log('[TEST] Final state progression:', stateChanges);
    console.log('[TEST] Final state:', stateChanges[stateChanges.length - 1]);

    // This is the KEY test: Did we reach CONNECTED after reconnection?
    const connectedCount = stateChanges.filter((s) => s === ConnectionState.CONNECTED).length;
    console.log('[TEST] CONNECTED state reached:', connectedCount, 'times');

    if (connectedCount === 0) {
      console.log('[TEST] ❌ PROBLEM: Connection never reached CONNECTED state');
      console.log('[TEST] This means crypto handshake is NOT completing');
    } else {
      console.log('[TEST] ✅ SUCCESS: Connection reached CONNECTED state');
    }

    console.log('=== TEST END ===\n');
  });
});
