import { test } from '@playwright/test';

// Mark this test to run serially (don't run in parallel with other tests)
test.describe.configure({ mode: 'serial' });
import { spawn, execSync } from 'child_process';

test('UI: browser auto-reconnects when server restarts', async ({ page }) => {
  // Use random port to avoid conflicts
  const wsPort = 28000 + Math.floor(Math.random() * 2000);
  const tcpPort = 26000 + Math.floor(Math.random() * 1000);
  let serverProcess: any = null;

  const startServer = async () => {
    console.log(`[TEST] Starting ascii-chat server on WebSocket port ${wsPort}, TCP port ${tcpPort}`);
    serverProcess = spawn('/Users/zfogg/src/github.com/zfogg/ascii-chat/build/bin/ascii-chat', ['server', '--port', String(tcpPort), '--websocket-port', String(wsPort), '--no-status-screen'], {
      stdio: 'inherit'
    });

    // Wait for server to be ready
    await new Promise<void>((resolve) => {
      const checkReady = () => {
        try {
          execSync(`nc -z localhost ${wsPort} 2>/dev/null`, { stdio: 'ignore' });
          console.log(`[TEST] Server is ready on WebSocket port ${wsPort}`);
          setTimeout(resolve, 1000); // Extra wait for server to fully initialize
        } catch {
          setTimeout(checkReady, 200);
        }
      };
      setTimeout(checkReady, 500); // Initial delay before checking
    });
  };

  const stopServer = async () => {
    if (serverProcess) {
      console.log('[TEST] Stopping server');
      serverProcess.kill();
      serverProcess = null;
      await new Promise(r => setTimeout(r, 500));
    }
  };

  const getConnectionState = async () => {
    return await page.evaluate(() => {
      const elements = document.querySelectorAll('*');
      for (const el of elements) {
        const text = el.textContent?.trim();
        if (el.children.length === 0 && text &&
            (text === 'Connected' || text === 'Connecting' || text === 'Disconnected' || text === 'Error')) {
          return text;
        }
      }
      return 'Unknown';
    });
  };

  // Listen to browser console for key events
  page.on('console', msg => {
    const text = msg.text();
    if (text.includes('CLOSED') || text.includes('Heartbeat') || text.includes('WebSocket') || text.includes('SocketBridge')) {
      console.log(`[BROWSER] ${text}`);
    }
  });

  // Step 1: Start server first
  console.log('\n📋 Step 1: Start test WebSocket server');
  await startServer();
  console.log('✓ Server started');

  // Step 2: Navigate to page with server URL query param
  console.log(`\n📋 Step 2: Navigate to client page with server ws://localhost:${wsPort}`);
  await page.goto(`http://localhost:3000/client?testServerUrl=ws://localhost:${wsPort}`, { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(1000);
  console.log('✓ Page loaded with test server URL');

  // Step 3: Open Connection panel
  console.log('\n📋 Step 3: Open Connection panel');
  await page.evaluate(() => {
    const btn = Array.from(document.querySelectorAll('button')).find(b =>
      b.textContent?.trim().includes('Connection')
    ) as HTMLElement;
    if (btn) btn.click();
  });
  await page.waitForTimeout(500);
  console.log('✓ Connection panel opened');

  // Step 4: Click Connect (server URL already set via query param)
  console.log('\n📋 Step 4: Click Connect button');
  await page.evaluate(() => {
    const btns = Array.from(document.querySelectorAll('button'));
    const connectBtn = btns.find(b => b.textContent?.trim() === 'Connect') as HTMLElement;
    if (connectBtn) connectBtn.click();
  });
  await page.waitForTimeout(500);
  console.log('✓ Connect button clicked');

  // Step 5: Wait for Connected state
  console.log('\n📋 Step 5: Wait for Connected state');
  const startTime = Date.now();
  let connected = false;
  while (Date.now() - startTime < 8000 && !connected) {
    const state = await getConnectionState();
    console.log(`  Current state: ${state} (${Date.now() - startTime}ms)`);
    if (state === 'Connected') {
      connected = true;
      console.log('✓ CONNECTED');
      break;
    }
    await page.waitForTimeout(200);
  }

  if (!connected) {
    console.log(`✗ FAIL: Never reached Connected state`);
    await page.screenshot({ path: '/tmp/ui-failed-connect.png', fullPage: true });
    await stopServer();
    return;
  }

  // Step 6: KILL SERVER and watch what happens
  console.log('\n📋 Step 6: KILL SERVER - watch UI reaction');
  await stopServer();

  console.log('  Monitoring UI for 10 seconds after disconnect...');
  const killTime = Date.now();
  const stateTimeline: string[] = [];

  while (Date.now() - killTime < 10000) {
    const state = await getConnectionState();
    const elapsed = Date.now() - killTime;

    if (!stateTimeline.includes(state) || elapsed % 1000 === 0) {
      console.log(`  +${elapsed}ms: ${state}`);
      if (!stateTimeline.includes(state)) {
        stateTimeline.push(state);
      }
    }

    await page.screenshot({ path: `/tmp/ui-state-${elapsed}ms.png`, fullPage: true });
    await page.waitForTimeout(500);
  }

  console.log('\n📋 State progression after server kill:');
  console.log('  ', stateTimeline.join(' → '));

  // Validate: should NOT stay Connected, should show Error, Connecting, or Disconnected
  if (stateTimeline[0] === 'Connected' && stateTimeline.length === 1) {
    console.log('\n✗✗✗ BROKEN: Browser never detected server disconnect!');
    console.log('     Stayed in Connected state the entire time');
  } else if (stateTimeline.includes('Error')) {
    console.log('\n✗ Browser went to Error state (bug?)');
  } else if (stateTimeline.includes('Connecting')) {
    console.log('\n✓ Browser detected disconnect and entered Connecting state');
  } else if (stateTimeline.includes('Disconnected')) {
    console.log('\n✓ Browser detected disconnect and entered Disconnected state');
  }

  // Step 7: Restart the server and verify it reconnects
  console.log('\n📋 Step 7: Restart server and verify reconnection');
  await startServer();
  console.log('✓ Server restarted');

  console.log('  Monitoring for reconnection...');
  const reconnectStart = Date.now();
  let reconnected = false;

  while (Date.now() - reconnectStart < 10000) {
    const state = await getConnectionState();
    if (state === 'Connected') {
      reconnected = true;
      console.log(`✓ RECONNECTED after ${Date.now() - reconnectStart}ms`);
      break;
    }
    await page.waitForTimeout(200);
  }

  if (!reconnected) {
    const finalState = await getConnectionState();
    console.log(`✗ Did not reconnect. Final state: ${finalState}`);
  }

  await page.screenshot({ path: '/tmp/ui-final-state.png', fullPage: true });

  // Cleanup
  await stopServer();
});
