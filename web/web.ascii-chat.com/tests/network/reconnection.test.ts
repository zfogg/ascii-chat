import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { WebSocket as WsWebSocket } from 'ws';
import { ClientConnection } from '../../src/network/ClientConnection';
import {
  getConnectionState,
  ConnectionState,
} from '../../src/wasm/client';
import { ServerFixture, getRandomPort } from '../e2e/server-fixture';

(global as any).WebSocket = WsWebSocket;

describe('Reconnection with Server Restart', () => {
  let server: ServerFixture | null = null;
  let serverUrl: string = '';
  let serverPort: number = 0;

  beforeEach(async () => {
    serverPort = getRandomPort();
    server = new ServerFixture(serverPort);
    await server.start();
    serverUrl = server.getUrl();
    console.log(`[Setup] Server started on ${serverUrl}`);
  });

  afterEach(async () => {
    if (server) {
      await server.stop();
      console.log('[Teardown] Server stopped');
    }
  });

  it('reaches CONNECTED even when server restarts during reconnection', async () => {
    const states: number[] = [];
    const connection = new ClientConnection({
      serverUrl,
    });

    connection.onStateChange((state) => {
      console.log(`[State] ${state} (${ConnectionState[state]}) (WASM state: ${getConnectionState()} ${ConnectionState[getConnectionState()]})`);
      states.push(state);
    });

    // Connect
    console.log('[1] Connecting to running server...');
    await connection.connect();
    await new Promise(r => setTimeout(r, 1000));
    console.log(`[After initial connect] UI state: ${states[states.length - 1]}, WASM state: ${getConnectionState()}`);
    expect(getConnectionState()).toBe(ConnectionState.CONNECTED);
    console.log('[✓] Connected (WASM confirmed CONNECTED)');

    // Kill server
    console.log('[2] Killing server...');
    await server!.stop();
    server = null;
    await new Promise(r => setTimeout(r, 500));

    // Now server is DOWN - client tries to reconnect but fails
    console.log('[3] Server is DOWN - client attempting reconnect (will fail)...');
    await new Promise(r => setTimeout(r, 2000)); // Wait for reconnect attempts

    console.log('[4] Restarting server on same port...');
    server = new ServerFixture(serverPort);
    await server.start();
    serverUrl = server.getUrl();
    console.log(`[Server restarted on ${serverUrl}]`);

    // Client should successfully reconnect (it will keep trying)
    console.log('[5] Waiting for successful reconnection...');
    await new Promise(r => setTimeout(r, 5000));

    console.log('[Final states]', states);
    console.log(`[Final WASM state] ${getConnectionState()} (${ConnectionState[getConnectionState()]})`);

    // The real test: is the WASM state machine in CONNECTED?
    const wasmConnected = getConnectionState() === ConnectionState.CONNECTED;
    console.log(`[Result] WASM CONNECTED: ${wasmConnected}`);

    if (wasmConnected) {
      console.log('[✅] PASSED - WASM state machine reached CONNECTED after reconnect');
    } else {
      console.log(`[❌] FAILED - WASM stuck in state ${getConnectionState()} (${ConnectionState[getConnectionState()]})`);
    }

    expect(wasmConnected).toBe(true);
  }, 15000); // 15 second timeout for server startup/shutdown
});
