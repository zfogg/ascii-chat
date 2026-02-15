/**
 * Unit tests for SocketBridge auto-reconnection
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { SocketBridge } from '../../src/network/SocketBridge';

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

  constructor(public url: string, public protocol: string) {}

  send(data: any) {
    // Mock send
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
    // Call event listeners (primary method)
    const handlers = this.eventListeners.get(event);
    if (handlers) {
      handlers.forEach((handler) => {
        handler({ type: event, ...data });
      });
    }

    // Also call property handlers for backward compatibility
    if (event === 'open' && this.onopen) {
      this.onopen.call(this, { type: 'open' } as any);
    } else if (event === 'message' && this.onmessage) {
      this.onmessage.call(this, { type: 'message', data } as any);
    } else if (event === 'error' && this.onerror) {
      this.onerror.call(this, { type: 'error', ...data } as any);
    } else if (event === 'close' && this.onclose) {
      this.onclose.call(this, { type: 'close', ...data } as any);
    }
  }

  close() {
    this.readyState = MockWebSocket.CLOSED;
    this.dispatchEvent('close', { code: 1000, reason: 'Normal closure', wasClean: true });
  }

  simulateOpen() {
    this.readyState = MockWebSocket.OPEN;
    this.dispatchEvent('open', {});
  }

  simulateClose(code = 1006, reason = 'Abnormal closure', wasClean = false) {
    this.readyState = MockWebSocket.CLOSED;
    this.dispatchEvent('close', { code, reason, wasClean });
  }

  simulateError() {
    this.dispatchEvent('error', { code: 'error', reason: 'Test error' });
  }

  simulateMessage(data: any) {
    this.dispatchEvent('message', { data });
  }
}

// Store created WebSockets to control them in tests
let createdWebSockets: MockWebSocket[] = [];

// Replace global WebSocket
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

  // Mock timers
  vi.useFakeTimers();
});

afterEach(() => {
  vi.restoreAllMocks();
  (global as any).WebSocket = originalWebSocket;
  vi.useRealTimers();
});

describe('SocketBridge Auto-Reconnection', () => {
  it('should connect successfully on first attempt', async () => {
    const stateChanges: string[] = [];
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
      onStateChange: (state) => stateChanges.push(state),
    });

    const connectPromise = bridge.connect();
    expect(createdWebSockets).toHaveLength(1);

    // Simulate successful connection
    createdWebSockets[0]!.simulateOpen();
    await connectPromise;

    expect(stateChanges).toContain('connecting');
    expect(stateChanges).toContain('open');
    expect(bridge.isConnected()).toBe(true);
  });

  it('should auto-reconnect when connection drops unexpectedly', async () => {
    const stateChanges: string[] = [];
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
      onStateChange: (state) => stateChanges.push(state),
    });

    // Initial connection
    const connectPromise = bridge.connect();
    createdWebSockets[0]!.simulateOpen();
    await connectPromise;

    // Simulate unexpected disconnect
    createdWebSockets[0]!.simulateClose(1006, 'Abnormal closure', false);
    expect(stateChanges).toContain('closed');

    // Should have scheduled a reconnect
    expect(createdWebSockets).toHaveLength(1); // No new WS created yet

    // Advance time by 1 second (reconnect delay)
    await vi.advanceTimersByTimeAsync(1000);

    // Now reconnection should have happened
    expect(createdWebSockets).toHaveLength(2);

    // Complete the reconnection
    createdWebSockets[1]!.simulateOpen();
    await vi.runAllTimersAsync();

    expect(bridge.isConnected()).toBe(true);
  });

  it('should use 1-second fixed delay for reconnection', async () => {
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
    });

    const connectPromise = bridge.connect();
    createdWebSockets[0]!.simulateOpen();
    await connectPromise;

    // Disconnect
    createdWebSockets[0]!.simulateClose();

    // Advance time by 500ms - should not reconnect yet
    await vi.advanceTimersByTimeAsync(500);
    expect(createdWebSockets).toHaveLength(1);

    // Advance another 500ms to reach 1000ms total
    await vi.advanceTimersByTimeAsync(500);
    expect(createdWebSockets).toHaveLength(2);
  });

  it('should NOT reconnect after user-initiated close', async () => {
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
    });

    const connectPromise = bridge.connect();
    createdWebSockets[0]!.simulateOpen();
    await connectPromise;

    // User explicitly closes
    bridge.close();

    // Try to reconnect the underlying WebSocket (simulating what might happen)
    createdWebSockets[0]!.simulateClose();

    // Advance time - no reconnection should be scheduled
    await vi.advanceTimersByTimeAsync(2000);
    expect(createdWebSockets).toHaveLength(1);
  });

  it('should retry multiple times in development mode', async () => {
    // Mock NODE_ENV as development
    const originalEnv = process.env.NODE_ENV;
    process.env.NODE_ENV = 'development';

    try {
      const bridge = new SocketBridge({
        url: 'ws://localhost:8080',
      });

      const connectPromise = bridge.connect();
      createdWebSockets[0]!.simulateOpen();
      await connectPromise;

      // Disconnect and retry multiple times
      for (let i = 0; i < 10; i++) {
        createdWebSockets[createdWebSockets.length - 1]!.simulateClose();
        await vi.advanceTimersByTimeAsync(1000);

        if (i < 9) {
          // Should create new WebSocket for retry
          expect(createdWebSockets.length).toBe(i + 2);
        }
      }

      // Should have created multiple WebSocket instances
      expect(createdWebSockets.length).toBeGreaterThan(5);
    } finally {
      process.env.NODE_ENV = originalEnv;
    }
  });

  it('should stop reconnecting after 5 attempts in production mode', async () => {
    const originalEnv = process.env.NODE_ENV;
    process.env.NODE_ENV = 'production';

    try {
      const bridge = new SocketBridge({
        url: 'ws://localhost:8080',
      });

      const connectPromise = bridge.connect();
      createdWebSockets[0]!.simulateOpen();
      await connectPromise;

      // Disconnect and try to reconnect
      createdWebSockets[0]!.simulateClose();

      // Try 5 reconnection attempts
      for (let i = 0; i < 5; i++) {
        await vi.advanceTimersByTimeAsync(1000);
        if (i < 4) {
          createdWebSockets[createdWebSockets.length - 1]!.simulateClose();
        }
      }

      const wsCountAfterFiveAttempts = createdWebSockets.length;

      // Try to reconnect more times - should not create new WebSockets
      for (let i = 0; i < 3; i++) {
        await vi.advanceTimersByTimeAsync(1000);
      }

      expect(createdWebSockets.length).toBe(wsCountAfterFiveAttempts);
    } finally {
      process.env.NODE_ENV = originalEnv;
    }
  });

  it('should handle packet reception during connection', async () => {
    const receivedPackets: Uint8Array[] = [];
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
      onPacket: (packet) => receivedPackets.push(packet),
    });

    const connectPromise = bridge.connect();
    createdWebSockets[0]!.simulateOpen();
    await connectPromise;

    // Send a packet
    const testPacket = new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7, 0x12, 0x34]);
    createdWebSockets[0]!.simulateMessage(testPacket.buffer);

    expect(receivedPackets).toHaveLength(1);
    expect(receivedPackets[0]).toEqual(testPacket);
  });

  it('should call error callback on connection error', async () => {
    const errors: Error[] = [];
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
      onError: (error) => errors.push(error),
    });

    const connectPromise = bridge.connect();
    createdWebSockets[0]!.simulateError();

    try {
      await connectPromise;
    } catch (e) {
      // Expected
    }

    expect(errors).toHaveLength(1);
    expect(errors[0]!.message).toBe('WebSocket error');
  });

  it('should properly track connection state after reconnection', async () => {
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
    });

    // First connection
    const connectPromise = bridge.connect();
    createdWebSockets[0]!.simulateOpen();
    await connectPromise;
    expect(bridge.isConnected()).toBe(true);

    // Disconnect
    createdWebSockets[0]!.simulateClose();
    expect(bridge.isConnected()).toBe(false);

    // Wait for reconnect
    await vi.advanceTimersByTimeAsync(1000);
    createdWebSockets[1]!.simulateOpen();
    await vi.runAllTimersAsync();

    expect(bridge.isConnected()).toBe(true);
  });

  it('should reconnect multiple times after repeated disconnects', async () => {
    const stateChanges: string[] = [];
    const bridge = new SocketBridge({
      url: 'ws://localhost:8080',
      onStateChange: (state) => stateChanges.push(state),
    });

    // Initial connection
    const connectPromise = bridge.connect();
    createdWebSockets[0]!.simulateOpen();
    await connectPromise;
    expect(bridge.isConnected()).toBe(true);

    // Cycle 1: Disconnect and reconnect
    createdWebSockets[0]!.simulateClose();
    expect(bridge.isConnected()).toBe(false);
    await vi.advanceTimersByTimeAsync(1000);
    createdWebSockets[1]!.simulateOpen();
    await vi.runAllTimersAsync();
    expect(bridge.isConnected()).toBe(true);
    expect(createdWebSockets).toHaveLength(2);

    // Cycle 2: Disconnect and reconnect again
    createdWebSockets[1]!.simulateClose();
    expect(bridge.isConnected()).toBe(false);
    await vi.advanceTimersByTimeAsync(1000);
    createdWebSockets[2]!.simulateOpen();
    await vi.runAllTimersAsync();
    expect(bridge.isConnected()).toBe(true);
    expect(createdWebSockets).toHaveLength(3);

    // Cycle 3: One more disconnect and reconnect
    createdWebSockets[2]!.simulateClose();
    expect(bridge.isConnected()).toBe(false);
    await vi.advanceTimersByTimeAsync(1000);
    createdWebSockets[3]!.simulateOpen();
    await vi.runAllTimersAsync();
    expect(bridge.isConnected()).toBe(true);
    expect(createdWebSockets).toHaveLength(4);

    // Verify state progression
    expect(stateChanges).toContain('connecting');
    expect(stateChanges).toContain('open');
    expect(stateChanges).toContain('closed');
    const openCount = stateChanges.filter((s) => s === 'open').length;
    expect(openCount).toBe(4); // 4 successful connections
    const closedCount = stateChanges.filter((s) => s === 'closed').length;
    expect(closedCount).toBe(3); // 3 disconnects
  });
});
