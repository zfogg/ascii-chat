import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { WebSocket as WsWebSocket } from "ws";
import { ClientConnection } from "../../src/network/ClientConnection";
import { getConnectionState, ConnectionState } from "../../src/wasm/client";
import { ServerFixture, getRandomPort } from "../e2e/server-fixture";

(global as any).WebSocket = WsWebSocket;

describe("Reconnection with Server Restart", () => {
  let server: ServerFixture | null = null;
  let serverUrl: string = "";
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
      console.log("[Teardown] Server stopped");
    }
  });

  it("reaches CONNECTED even when server restarts during reconnection", async () => {
    const states: number[] = [];
    const stateNames: string[] = [];
    const connection = new ClientConnection({
      serverUrl,
    });

    connection.onStateChange((state) => {
      const stateName = ConnectionState[state];
      console.log(`[State] ${stateName}`);
      states.push(state);
      stateNames.push(stateName);
    });

    // Test Phase 1: Initial connection
    console.log("[1] Connecting to running server...");
    await connection.connect();
    await new Promise((r) => setTimeout(r, 1000));

    const afterInitialConnect = states.length;
    console.log(`[After initial connect] States: ${stateNames.join(" → ")}`);

    // Verify initial connection sequence
    expect(states[states.length - 1]).toBe(ConnectionState.CONNECTED);
    expect(stateNames).toContain("CONNECTING");
    expect(stateNames).toContain("HANDSHAKE");
    expect(stateNames).toContain("CONNECTED");
    console.log("[✓] Initial connection: CONNECTING → HANDSHAKE → CONNECTED");

    // Test Phase 2: Server dies - wait for reconnection attempts
    console.log("[2] Killing server...");
    await server!.stop();
    server = null;
    await new Promise((r) => setTimeout(r, 500));

    console.log("[3] Waiting for reconnection attempts...");
    await new Promise((r) => setTimeout(r, 2500));

    const afterReconnectAttempts = states.length;
    const reconnectAttemptStates = stateNames.slice(afterInitialConnect);
    console.log(
      `[After reconnect attempts] New states: ${reconnectAttemptStates.join(" → ")}`,
    );

    // Should have started reconnecting (CONNECTING state)
    expect(reconnectAttemptStates).toContain("CONNECTING");
    console.log("[✓] Reconnection attempts started: saw CONNECTING state");

    // Test Phase 3: Server restarts and client reconnects
    console.log("[4] Restarting server on same port...");
    server = new ServerFixture(serverPort);
    await server.start();
    serverUrl = server.getUrl();
    console.log(`[Server restarted]`);

    console.log("[5] Waiting for successful reconnection...");
    await new Promise((r) => setTimeout(r, 5000));

    const finalStates = stateNames.join(" → ");
    console.log(`[Final UI state sequence] ${finalStates}`);
    console.log(`[Final WASM state] ${ConnectionState[getConnectionState()]}`);

    // Verify final state is CONNECTED
    expect(states[states.length - 1]).toBe(ConnectionState.CONNECTED);
    expect(ConnectionState[states[states.length - 1]]).toBe("CONNECTED");
    console.log("[✓] Final UI state: CONNECTED");

    // Verify we saw reconnection states
    const connectingCount = stateNames.filter((s) => s === "CONNECTING").length;
    expect(connectingCount).toBeGreaterThan(1);
    console.log(
      `[✓] Multiple CONNECTING states during reconnection (${connectingCount} total)`,
    );

    // Verify we returned to HANDSHAKE during reconnection
    const postReconnectStates = stateNames.slice(afterReconnectAttempts);
    expect(postReconnectStates).toContain("HANDSHAKE");
    console.log("[✓] Reconnection handshake: saw HANDSHAKE state");

    // Verify WASM state matches UI state
    const wasmConnected = getConnectionState() === ConnectionState.CONNECTED;
    expect(wasmConnected).toBe(true);
    console.log("[✓] WASM state synchronized with UI state");

    console.log("[✅] PASSED - Full reconnection cycle verified");
  }, 15000); // 15 second timeout for server startup/shutdown
});
