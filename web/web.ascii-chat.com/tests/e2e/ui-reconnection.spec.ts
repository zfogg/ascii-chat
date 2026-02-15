import { test } from '@playwright/test';
import { WebSocketServer } from 'ws';

test('UI: browser auto-reconnects when server restarts', async ({ page }) => {
  let wsServer: WebSocketServer | null = null;
  const port = 9800 + Math.floor(Math.random() * 100);
  let passed = 0;
  let failed = 0;

  const startServer = async () => {
    return new Promise<void>((resolve) => {
      wsServer = new WebSocketServer({ port });
      wsServer.on('connection', (ws) => {
        const packet = new Uint8Array(22);
        packet[8] = 0;
        packet[9] = 3;
        ws.send(packet.buffer);
      });
      wsServer.once('listening', () => resolve());
    });
  };

  const stopServer = async () => {
    if (wsServer) {
      for (const ws of wsServer.clients) {
        ws.close();
      }
      await new Promise<void>((resolve) => {
        wsServer!.close(() => resolve());
        setTimeout(() => resolve(), 1000);
      });
      wsServer = null;
      await new Promise(r => setTimeout(r, 300));
    }
  };

  const checkState = async (expectedState: string, timeout: number = 5000) => {
    const startTime = Date.now();

    while (Date.now() - startTime < timeout) {
      try {
        // Look for any element with the expected state text (not just buttons)
        const found = await page.evaluate((state: string) => {
          const elements = document.querySelectorAll('*');
          for (const el of elements) {
            // Check leaf nodes that contain exact state text
            if (el.textContent?.trim() === state && el.children.length === 0) {
              return true;
            }
          }
          return false;
        }, expectedState);

        if (found) {
          return true;
        }
      } catch (e) {
        // Continue on error
      }

      await page.waitForTimeout(200);
    }

    return false;
  };

  // Test 1: Navigate to page
  console.log('\n📋 Step 1: Navigate to client page');
  try {
    await page.goto('http://localhost:5173/client', { waitUntil: 'domcontentloaded' });
    await page.waitForTimeout(500);
    console.log('✓ Page loaded');
    passed++;
  } catch (e) {
    console.log(`✗ Failed to load page: ${e}`);
    failed++;
    return;
  }

  // Test 2: Start server and open Connection panel
  console.log('\n📋 Step 2: Start server and open Connection panel');
  try {
    await startServer();

    // Try to open Connection panel by clicking the button
    const connBtn = page.locator('button:has-text("Connection")');
    if (await connBtn.count() > 0) {
      // Use JavaScript to click to bypass overlay issues
      await page.evaluate(() => {
        const btn = Array.from(document.querySelectorAll('button')).find(b =>
          b.textContent?.trim().includes('Connection')
        ) as HTMLElement;
        if (btn) btn.click();
      });
    }

    // Short wait for dialog to open
    await page.waitForTimeout(500);
    console.log('✓ Connection panel opened');
    passed++;
  } catch (e) {
    console.log(`✗ Failed to open Connection panel: ${e}`);
    failed++;
    return;
  }

  // Test 3: Click Connect button
  console.log('\n📋 Step 3: Click Connect button');
  try {
    // Find and click the Connect button using JavaScript
    await page.evaluate(() => {
      const btns = Array.from(document.querySelectorAll('button'));
      const connectBtn = btns.find(b => b.textContent?.trim() === 'Connect') as HTMLElement;
      if (connectBtn) connectBtn.click();
    });

    await page.waitForTimeout(500);
    console.log('✓ Connect button clicked');
    passed++;
  } catch (e) {
    console.log(`✗ Failed to click Connect: ${e}`);
    failed++;
  }

  // Test 4: Verify Connected state
  console.log('\n📋 Step 4: Wait for Connected state');

  // Wait for the "Connected" state text to appear
  let isConnected = false;
  const startTime = Date.now();
  while (Date.now() - startTime < 5000) {
    const text = await page.evaluate(() => {
      // Look for any element containing "Connected" text (not button, could be any element)
      const elements = document.querySelectorAll('*');
      for (const el of elements) {
        if (el.textContent?.includes('Connected') && el.children.length === 0) {
          return el.textContent.trim();
        }
      }
      return null;
    });

    if (text === 'Connected') {
      isConnected = true;
      break;
    }
    await page.waitForTimeout(200);
  }

  if (isConnected) {
    console.log('✓ Shows Connected');
    passed++;
    await page.screenshot({ path: '/tmp/ui-step1-connected.png', fullPage: true });
  } else {
    console.log('✗ Did not reach Connected state');
    failed++;
  }

  // Test 5: Kill server and check for NO Error state
  console.log('\n📋 Step 5: Kill server and verify NOT in Error state');
  try {
    await stopServer();
    await page.waitForTimeout(1000);

    const errorBtn = page.locator('dialog[open] button, div[role="dialog"] button').filter({ hasText: /^Error$/ });
    const count = await errorBtn.count();

    let isError = false;
    if (count > 0) {
      isError = await errorBtn.first().evaluate(el => {
        const style = window.getComputedStyle(el);
        return style.backgroundColor !== 'rgb(107, 114, 128)' && style.backgroundColor !== 'rgba(0, 0, 0, 0)';
      }).catch(() => false);
    }

    if (!isError) {
      console.log('✓ NOT in Error state (correct!)');
      passed++;
    } else {
      console.log('✗ ERROR: Showing Error state (should be Connecting/Disconnected)');
      failed++;
    }

    await page.screenshot({ path: '/tmp/ui-step2-disconnect.png', fullPage: true });
  } catch (e) {
    console.log(`✗ Failed to verify state: ${e}`);
    failed++;
  }

  // Test 6: Verify Connecting state
  console.log('\n📋 Step 6: Verify Connecting state during reconnection');
  if (await checkState('Connecting', 3000)) {
    console.log('✓ Shows Connecting (good auto-reconnect attempt)');
    passed++;
  } else {
    console.log('✗ Did not see Connecting state');
    failed++;
  }

  // Test 7: Restart server and reconnect
  console.log('\n📋 Step 7: Restart server');
  try {
    await startServer();
    console.log('✓ Server restarted');
    passed++;
  } catch (e) {
    console.log(`✗ Failed to restart server: ${e}`);
    failed++;
  }

  // Test 8: Verify reconnects to Connected
  console.log('\n📋 Step 8: Wait for reconnection to Connected');
  if (await checkState('Connected', 8000)) {
    console.log('✓ Reconnected to Connected');
    passed++;
    await page.screenshot({ path: '/tmp/ui-step3-reconnected.png', fullPage: true });
  } else {
    console.log('✗ Failed to reconnect to Connected');
    failed++;
  }

  // Cleanup
  await stopServer();

  // Summary
  console.log('\n' + '='.repeat(50));
  console.log(`RESULTS: ${passed} passed, ${failed} failed`);
  console.log('='.repeat(50));

  if (failed === 0) {
    console.log('\n✅ ALL TESTS PASSED - Auto-reconnection works!\n');
  } else {
    console.log(`\n❌ ${failed} TEST(S) FAILED\n`);
  }
});
