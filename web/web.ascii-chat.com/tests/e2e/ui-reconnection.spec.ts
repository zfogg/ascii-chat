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
      // Get connection state from the React component by looking at the control bar status text
      // The status text is in: <span class="status text-xs text-terminal-8">{status}</span>

      const statusSpan = document.querySelector('.status');
      if (statusSpan) {
        const text = statusSpan.textContent?.trim() || '';
        // Map the full status text to state names
        if (text === 'Connected') return 'Connected';
        if (text === 'Connecting...') return 'Connecting';
        if (text === 'Connecting') return 'Connecting';
        if (text === 'Performing handshake') return 'Handshake';
        if (text === 'Disconnected') return 'Disconnected';
        if (text.startsWith('Error')) return 'Error';
        return text || 'Unknown';
      }

      // Fallback: search all elements
      const elements = document.querySelectorAll('*');
      for (const el of elements) {
        const text = el.textContent?.trim();
        // Look for leaf nodes with exact state text
        if (el.children.length === 0 && text && text.length < 30) {
          if (text === 'Connected') return 'Connected';
          if (text === 'Connecting') return 'Connecting';
          if (text === 'Performing handshake') return 'Handshake';
          if (text === 'Disconnected') return 'Disconnected';
          if (text === 'Error') return 'Error';
        }
      }

      return 'Unknown';
    });
  };

  const getAllStatuses = async () => {
    return await page.evaluate(() => {
      // Debug: return all text that contains "connect", "handshake", "error", etc
      const results: string[] = [];
      document.querySelectorAll('*').forEach(el => {
        const text = el.textContent?.trim() || '';
        if ((text.includes('connect') || text.includes('handshake') || text.includes('disconnect') || text.includes('error')) && text.length < 100) {
          results.push(text);
        }
      });
      return [...new Set(results)];
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

  // Step 3: Open Connection panel and keep it open
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

  console.log('  Monitoring UI for 12 seconds after disconnect (100ms polling)...');
  const killTime = Date.now();
  const stateTimeline: Array<{ state: string; elapsed: number }> = [];

  while (Date.now() - killTime < 12000) {
    const state = await getConnectionState();
    const elapsed = Date.now() - killTime;

    // Record every state change (not duplicates)
    if (!stateTimeline.length || stateTimeline[stateTimeline.length - 1].state !== state) {
      stateTimeline.push({ state, elapsed });
      console.log(`  +${elapsed}ms: ${state}`);
    }

    // Fast polling (100ms) to catch all state changes
    await page.waitForTimeout(100);
  }

  console.log('\n📋 State progression after server kill:');
  const progression = stateTimeline.map((s, i) => {
    const nextIdx = i + 1;
    const duration = nextIdx < stateTimeline.length
      ? stateTimeline[nextIdx].elapsed - s.elapsed
      : '...';
    return `${s.state}(${s.elapsed}ms, ${duration}ms)`;
  }).join(' → ');
  console.log('  ', progression);

  // Validate: should NOT stay Connected, should show Connecting or Disconnected
  if (stateTimeline.length === 1 && stateTimeline[0].state === 'Connected') {
    console.log('\n✗✗✗ BROKEN: Browser never detected server disconnect!');
    console.log('     Stayed in Connected state the entire time');
  } else if (stateTimeline.map(s => s.state).includes('Connecting')) {
    console.log('\n✓ Browser detected disconnect and entered Connecting state');
  } else if (stateTimeline.map(s => s.state).includes('Disconnected')) {
    console.log('\n✓ Browser detected disconnect and entered Disconnected state');
  } else if (stateTimeline.map(s => s.state).includes('Error')) {
    console.log('\n✗ Browser went to Error state (unexpected)');
  }

  // Step 7: Restart the server and monitor reconnection process
  console.log('\n📋 Step 7: Restart server while monitoring state...');

  // Check state BEFORE restarting server
  let stateBeforeRestart = await getConnectionState();
  console.log(`  State before restart: ${stateBeforeRestart}`);

  // Verify modal is still open
  const modalOpen = await page.evaluate(() => {
    const dialog = document.querySelector('[role="dialog"]');
    return dialog ? 'open' : 'closed';
  });
  console.log(`  Connection modal: ${modalOpen}`);

  // Start monitoring BEFORE the server restart so we catch the full reconnection sequence
  const fullReconnectTimeline: Array<{ state: string; elapsed: number }> = [];
  const reconnectMonitorStart = Date.now();
  let monitoringDone = false;

  // Start server in background (don't await immediately)
  const serverStartPromise = (async () => {
    console.log('  Starting server...');
    await startServer();
    console.log('✓ Server restarted');
  })();

  // Monitor state while server is restarting
  const monitorReconnection = async () => {
    while (Date.now() - reconnectMonitorStart < 15000) {
      const state = await getConnectionState();
      const elapsed = Date.now() - reconnectMonitorStart;

      // Record every state change
      if (!fullReconnectTimeline.length || fullReconnectTimeline[fullReconnectTimeline.length - 1].state !== state) {
        fullReconnectTimeline.push({ state, elapsed });
        console.log(`  +${elapsed}ms: ${state}`);

        // Stop if we reach Connected
        if (state === 'Connected') {
          monitoringDone = true;
          break;
        }
      }

      // Fast polling (100ms) to catch all state changes
      await page.waitForTimeout(100);
    }
  };

  // Run both in parallel
  await Promise.all([serverStartPromise, monitorReconnection()]);

  const reconnectTimeline = fullReconnectTimeline;

  console.log('\n📋 State progression during reconnection:');
  const reconnectProgression = reconnectTimeline.map((s, i) => {
    const nextIdx = i + 1;
    const duration = nextIdx < reconnectTimeline.length
      ? reconnectTimeline[nextIdx].elapsed - s.elapsed
      : '...';
    return `${s.state}(${s.elapsed}ms, ${duration}ms)`;
  }).join(' → ');
  console.log('  ', reconnectProgression);

  // Check if we reached Connected state
  const didReconnect = monitoringDone || reconnectTimeline.some(s => s.state === 'Connected');
  if (!didReconnect) {
    const finalState = await getConnectionState();
    console.log(`✗ Did not reconnect. Final state: ${finalState}`);
  } else {
    console.log(`✓ RECONNECTED to Connected state`);
  }

  await page.screenshot({ path: '/tmp/ui-final-state.png', fullPage: true });

  // Cleanup
  await stopServer();
});
