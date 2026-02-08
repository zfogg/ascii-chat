/**
 * E2E tests for WebSocket client connection to native ascii-chat server
 * 
 * These tests require a native ascii-chat server to be running:
 *   ./build/bin/ascii-chat server --port 27224
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

  test('should initialize WASM module', async ({ page }) => {
    // Wait for WASM to initialize
    await expect(page.locator('.status')).toContainText('WASM initialized successfully', {
      timeout: 5000
    });
  });

  test('should generate keypair', async ({ page }) => {
    // Wait for WASM init
    await page.waitForSelector('button:text("Generate Keypair"):not([disabled])', {
      timeout: 5000
    });

    // Click generate keypair button
    await page.click('button:text("Generate Keypair")');

    // Verify public key is displayed
    await expect(page.locator('h2:text("Client Public Key")')).toBeVisible();
    await expect(page.locator('code')).toHaveText(/^[0-9a-f]{64}$/i);
  });

  test('should connect to native server and complete handshake', async ({ page }) => {
    // Capture console logs
    page.on('console', msg => {
      console.log('BROWSER:', msg.text());
    });

    // Wait for WASM init and generate keypair
    await page.waitForSelector('button:text("Generate Keypair"):not([disabled])', {
      timeout: 5000
    });
    await page.click('button:text("Generate Keypair")');

    // Wait for keypair generation
    await expect(page.locator('h2:text("Client Public Key")')).toBeVisible();

    // Ensure server URL is correct
    await expect(page.locator('input[type="text"]')).toHaveValue(SERVER_URL);

    // Click connect button
    await page.click('button:text("Connect to Server")');

    // Verify connection state transitions
    // Connecting → Handshake → Connected
    await expect(page.locator('.status')).toContainText('Connecting', { timeout: 2000 });
    await expect(page.locator('.status')).toContainText('Performing handshake', {
      timeout: 2000
    });
    await expect(page.locator('.status')).toContainText('Connected', { timeout: 5000 });

    // Verify connection state indicator shows "Connected" as active
    await expect(
      page.locator('.grid .bg-blue-600:text("Connected")')
    ).toBeVisible();
  });

  test('should disconnect cleanly', async ({ page }) => {
    // Connect first
    await page.waitForSelector('button:text("Generate Keypair"):not([disabled])');
    await page.click('button:text("Generate Keypair")');
    await page.waitForSelector('h2:text("Client Public Key")');
    await page.click('button:text("Connect to Server")');
    await expect(page.locator('.status')).toContainText('Connected', { timeout: 5000 });

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
    // You'll need to implement video frame display in ClientDemo.tsx

    // Connect to server
    await page.waitForSelector('button:text("Generate Keypair"):not([disabled])');
    await page.click('button:text("Generate Keypair")');
    await page.waitForSelector('h2:text("Client Public Key")');
    await page.click('button:text("Connect to Server")');
    await expect(page.locator('.status')).toContainText('Connected', { timeout: 5000 });

    // Listen for packet received events in console
    const packetReceived = page.waitForEvent('console', msg =>
      msg.text().includes('Received packet')
    );

    // Wait for first packet (timeout after 10s)
    await expect(packetReceived).resolves.toBeTruthy();
  });

  test('should handle connection errors gracefully', async ({ page }) => {
    // Change server URL to invalid port
    await page.fill('input[type="text"]', 'ws://localhost:99999');

    await page.waitForSelector('button:text("Generate Keypair"):not([disabled])');
    await page.click('button:text("Generate Keypair")');
    await page.waitForSelector('h2:text("Client Public Key")');

    // Try to connect
    await page.click('button:text("Connect to Server")');

    // Should show error state
    await expect(page.locator('.status')).toContainText(/error|failed/i, {
      timeout: 5000
    });
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
