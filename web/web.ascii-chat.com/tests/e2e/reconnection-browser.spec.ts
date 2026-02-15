import { test, expect } from '@playwright/test';
import { WebSocketServer } from 'ws';

test.describe('Browser Reconnection with Server Restart', () => {
  let wsServer: WebSocketServer | null = null;
  let port: number;

  const startServer = async (wsPort: number) => {
    return new Promise<void>((resolve) => {
      wsServer = new WebSocketServer({ port: wsPort });
      wsServer.on('connection', (ws) => {
        console.log(`[E2E Test] Client connected to WebSocket server`);
        // Send handshake complete packet immediately
        const packet = new Uint8Array(22);
        packet[8] = 0;
        packet[9] = 3; // CRYPTO_HANDSHAKE_COMPLETE type
        ws.send(packet.buffer);
      });

      wsServer.once('listening', () => {
        console.log(`[E2E Test] WebSocket server listening on port ${wsPort}`);
        resolve();
      });
    });
  };

  const stopServer = async () => {
    if (wsServer) {
      // Close all client connections
      for (const ws of wsServer.clients) {
        ws.close();
      }

      // Close the server
      const closePromise = new Promise<void>((resolve) => {
        wsServer!.close(() => {
          console.log('[E2E Test] WebSocket server closed');
          resolve();
        });
        // Failsafe timeout
        setTimeout(() => {
          console.log('[E2E Test] WebSocket server close timeout');
          resolve();
        }, 1000);
      });

      await closePromise;
      wsServer = null;
      // Wait for close events to propagate
      await new Promise(r => setTimeout(r, 300));
    }
  };

  test('reconnects to Connected state when server restarts', async ({ page }) => {
    port = 9700 + Math.floor(Math.random() * 200);
    const serverUrl = `ws://localhost:${port}`;

    // Start WebSocket server
    console.log('[E2E Test] Starting WebSocket server...');
    await startServer(port);

    // Navigate to the web client
    console.log('[E2E Test] Navigating to web client...');
    await page.goto('http://localhost:5173/client', { waitUntil: 'domcontentloaded' });
    await page.waitForTimeout(500); // Let page settle

    // Find the server URL input and set it
    console.log('[E2E Test] Setting server URL to:', serverUrl);
    const inputs = page.locator('input[type="text"]');
    const serverUrlInput = inputs.nth(0); // First input should be the server URL
    await serverUrlInput.clear();
    await serverUrlInput.fill(serverUrl);
    console.log('[E2E Test] Server URL set');

    // Click the Connect button (the green one with full width)
    console.log('[E2E Test] Clicking Connect button...');
    const connectButton = page.locator('button').filter({ hasText: /^Connect$/ }).nth(1);
    await connectButton.click();

    // Wait for Connected state - look for the "Connected" button/indicator
    console.log('[E2E Test] Waiting for Connected state...');
    let isConnected = false;
    for (let i = 0; i < 15; i++) {
      const connectedBtn = page.locator('button').filter({ hasText: 'Connected' });
      const isHighlighted = await connectedBtn.evaluate(el => {
        const style = window.getComputedStyle(el);
        return style.backgroundColor || style.color;
      }).catch(() => null);

      console.log(`[E2E Test] Check Connected state ${i + 1}, highlighted: ${isHighlighted ? 'yes' : 'no'}`);

      if (isHighlighted) {
        isConnected = true;
        break;
      }
      await page.waitForTimeout(200);
    }

    expect(isConnected).toBe(true, 'Should be Connected before server restart');
    console.log('[E2E Test] ✓ Connected initially');

    // Kill server
    console.log('[E2E Test] Killing server...');
    await stopServer();

    // Wait and check that we're NOT in Error state (should be Connecting or Disconnected)
    console.log('[E2E Test] Checking state after server dies...');
    await page.waitForTimeout(500);

    const errorBtn = page.locator('button').filter({ hasText: /^Error$/ });
    const errorBtnHighlighted = await errorBtn.evaluate(el => {
      const style = window.getComputedStyle(el);
      return style.backgroundColor && style.backgroundColor !== 'rgb(107, 114, 128)'; // Not default gray
    }).catch(() => false);

    console.log(`[E2E Test] Error button highlighted: ${errorBtnHighlighted}`);
    expect(errorBtnHighlighted).toBe(false, 'Should NOT be in Error state after disconnect');

    // Wait for it to attempt reconnecting (should show Connecting state)
    console.log('[E2E Test] Waiting for Connecting state...');
    let connectingFound = false;
    for (let i = 0; i < 10; i++) {
      const connectingBtn = page.locator('button').filter({ hasText: 'Connecting' });
      const isConnecting = await connectingBtn.evaluate(el => {
        const style = window.getComputedStyle(el);
        return style.backgroundColor && style.backgroundColor !== 'rgb(107, 114, 128)';
      }).catch(() => false);

      console.log(`[E2E Test] Status check ${i + 1}, Connecting: ${isConnecting}`);
      if (isConnecting) {
        connectingFound = true;
        break;
      }
      await page.waitForTimeout(500);
    }

    expect(connectingFound).toBe(true, 'Should show Connecting state during reconnection');
    console.log('[E2E Test] ✓ Showing Connecting state (no Error)');

    // Restart server
    console.log('[E2E Test] Restarting server...');
    await startServer(port);

    // Wait for reconnection to complete (should reach Connected again)
    console.log('[E2E Test] Waiting for reconnection to complete...');
    let reconnectedFound = false;
    for (let i = 0; i < 20; i++) {
      const connectedBtn = page.locator('button').filter({ hasText: 'Connected' });
      const isHighlighted = await connectedBtn.evaluate(el => {
        const style = window.getComputedStyle(el);
        return style.backgroundColor && style.backgroundColor !== 'rgb(107, 114, 128)';
      }).catch(() => false);

      console.log(`[E2E Test] Reconnection check ${i + 1}, Connected: ${isHighlighted}`);
      if (isHighlighted) {
        reconnectedFound = true;
        break;
      }
      await page.waitForTimeout(300);
    }

    expect(reconnectedFound).toBe(true, 'Should reconnect to Connected after server restart');
    console.log('[E2E Test] ✓ Reconnected after server restart');

    // Cleanup
    await stopServer();
  });
});
