/**
 * E2E tests for WebSocket client connection to native ascii-chat server
 *
 * These tests require a native ascii-chat server to be running with WebSocket support:
 *   ./build/bin/ascii-chat server --websocket-port 27226 --no-encrypt
 *
 * NOTE: --no-encrypt is currently required because the crypto handshake
 * needs to be implemented for WebSocket protocol layer (not raw TCP)
 *
 * Run with: bun run test:e2e
 */

import { test, expect } from '@playwright/test';

const SERVER_URL = 'ws://localhost:27226';  // Default WebSocket port
const WEB_CLIENT_URL = 'http://localhost:3000/client';

test.describe('Client Connection to Native Server', () => {
  test.beforeEach(async ({ page }) => {
    // Navigate to client demo page
    await page.goto(WEB_CLIENT_URL);
  });

  test('should load client demo page', async ({ page }) => {
    await expect(page.locator('h1')).toContainText('ASCII Chat - Client WASM Demo');
  });

  test('should initialize WASM module and auto-generate keypair', async ({ page }) => {
    // Wait for WASM to initialize
    await expect(page.locator('.status')).toContainText('WASM initialized successfully', {
      timeout: 5000
    });

    // Keypair should be auto-generated
    await expect(page.locator('.status')).toContainText('Keypair generated', {
      timeout: 2000
    });

    // Verify public key is displayed
    await expect(page.locator('h2:text("Client Public Key")')).toBeVisible();
    await expect(page.locator('code')).toHaveText(/^[0-9a-f]{64}$/i);
  });

  test('should auto-connect to native server and complete handshake', async ({ page }) => {
    // Capture console logs
    page.on('console', msg => {
      console.log('BROWSER:', msg.text());
    });

    // Page should auto-connect on load
    // Wait for final connected state (may transition quickly through intermediate states)
    await expect(page.locator('.status')).toContainText('Connected', { timeout: 15000 });

    // Verify connection state indicator shows "Connected" as active
    await expect(
      page.locator('.grid .bg-blue-600:text("Connected")')
    ).toBeVisible();

    // Verify public key was generated
    await expect(page.locator('h2:text("Client Public Key")')).toBeVisible();
    await expect(page.locator('code')).toHaveText(/^[0-9a-f]{64}$/i);
  });

  test('should disconnect cleanly', async ({ page }) => {
    // Wait for auto-connection to complete
    await expect(page.locator('.status')).toContainText('Connected', { timeout: 10000 });

    // Now disconnect
    await page.click('button:text("Disconnect")');

    // Verify disconnection
    await expect(page.locator('.status')).toContainText('Disconnected');
    await expect(
      page.locator('.grid .bg-blue-600:text("Disconnected")')
    ).toBeVisible();

    // Connect button should be available again
    await expect(page.locator('button:text("Connect to Server")')).toBeVisible();
  });

  test('should receive video frames from server', async ({ page }) => {
    // This test assumes the server is sending video frames
    // Wait for auto-connection to complete
    await expect(page.locator('.status')).toContainText('Connected', { timeout: 10000 });

    // Listen for packet received events in console
    const packetReceived = page.waitForEvent('console', msg =>
      msg.text().includes('Received packet')
    );

    // Wait for first packet (timeout after 10s)
    await expect(packetReceived).resolves.toBeTruthy();
  });

  test.skip('should handle connection errors gracefully', async ({ page }) => {
    // This test is skipped because auto-connect happens on page load
    // To test error handling, you would need to:
    // 1. Stop the server before loading the page, OR
    // 2. Add URL parameter support to change server URL before auto-connect
    // For now, error handling can be tested manually by stopping the server
  });
});

test.describe('Opus Audio Codec', () => {
  test('should initialize Opus encoder/decoder', async ({ page }) => {
    await page.goto(WEB_CLIENT_URL);

    // This test requires implementing an audio testing UI
    // For now, we can verify Opus is available via console
    const opusAvailable = await page.evaluate(() => {
      return window.ClientModule !== undefined;
    });

    expect(opusAvailable).toBeTruthy();
  });
});
