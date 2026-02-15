import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { WebSocketServer, WebSocket as WsWebSocket } from 'ws';
import { ClientConnection } from '../../src/network/ClientConnection';

(global as any).WebSocket = WsWebSocket;

vi.mock('../../src/wasm/client', () => ({
  initClientWasm: vi.fn().mockResolvedValue(undefined),
  cleanupClientWasm: vi.fn(),
  generateKeypair: vi.fn().mockResolvedValue('test-key'),
  setServerAddress: vi.fn(),
  registerSendPacketCallback: vi.fn(),
  handleKeyExchangeInit: vi.fn(),
  handleAuthChallenge: vi.fn(),
  handleHandshakeComplete: vi.fn(),
  getConnectionState: vi.fn(() => 3),
  parsePacket: vi.fn((data) => {
    const view = new DataView(data instanceof Uint8Array ? data.buffer : data);
    return { type: view.getUint16(8, false), length: 0, client_id: 0 };
  }),
  packetTypeName: vi.fn(t => 'PACKET'),
  ConnectionState: { DISCONNECTED: 0, CONNECTING: 1, HANDSHAKE: 2, CONNECTED: 3, ERROR: 4 },
  PacketType: { CRYPTO_HANDSHAKE_COMPLETE: 3 },
}));

describe('Reconnection with Server Restart', () => {
  let port: number;
  let server: WebSocketServer | null = null;

  const startServer = async () => {
    server = new WebSocketServer({ port });
    server.on('connection', (ws) => {
      const packet = new Uint8Array(22);
      packet[8] = 0; packet[9] = 3;
      ws.send(packet.buffer);
    });
    await new Promise<void>(r => server!.once('listening', () => r()));
  };

  const stopServer = async () => {
    if (server) {
      // Close all client connections explicitly
      for (const ws of server.clients) {
        ws.close();
      }

      // Close the server itself with a timeout
      const closePromise = new Promise<void>((resolve) => {
        server!.close(() => {
          resolve();
        });
        // Failsafe: resolve after 1 second even if close callback doesn't fire
        setTimeout(() => {
          resolve();
        }, 1000);
      });

      await closePromise;
      server = null;
      // Wait for close events to propagate
      await new Promise(r => setTimeout(r, 100));
    }
  };

  beforeEach(async () => {
    port = 9400 + Math.floor(Math.random() * 600);
    await startServer();
  });

  afterEach(async () => {
    await stopServer();
  });

  it('reaches CONNECTED even when server restarts during reconnection', async () => {
    const states: number[] = [];
    const connection = new ClientConnection({
      serverUrl: `ws://localhost:${port}`,
    });

    connection.onStateChange((state) => {
      console.log(`[State] ${state}`);
      states.push(state);
    });

    // Connect
    console.log('[1] Connecting to running server...');
    await connection.connect();
    await new Promise(r => setTimeout(r, 100));
    expect(states).toContain(3); // CONNECTED
    console.log('[✓] Connected');

    // Kill server
    console.log('[2] Killing server...');
    await stopServer();
    await new Promise(r => setTimeout(r, 100));

    // Now server is DOWN - client tries to reconnect but fails
    console.log('[3] Server is DOWN - client attempting reconnect (will fail)...');
    await new Promise(r => setTimeout(r, 1500)); // Wait past first reconnect attempt

    console.log('[4] Restarting server...');
    await startServer();

    // Client should successfully reconnect
    console.log('[5] Waiting for successful reconnection...');
    await new Promise(r => setTimeout(r, 1500));

    console.log('[Final states]', states);
    const connectedCount = states.filter(s => s === 3).length;
    console.log(`[Result] Reached CONNECTED: ${connectedCount} times`);

    if (connectedCount >= 2) {
      console.log('[✅] PASSED - Reconnected after server restart');
    } else {
      console.log('[❌] FAILED - Stuck at Connecting');
    }

    expect(connectedCount).toBeGreaterThanOrEqual(2);
  });
});
