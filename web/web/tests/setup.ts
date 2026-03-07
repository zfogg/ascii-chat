/**
 * Test setup file for vitest
 * Loads WASM modules and sets up test environment
 */

// Add any global test setup here
// For example, you might want to mock certain browser APIs
// that aren't available in the test environment

// Mock Web Audio API if needed
if (typeof AudioContext === "undefined") {
  global.AudioContext = class MockAudioContext {} as any;
}

// Mock WebSocket if needed
if (typeof WebSocket === "undefined") {
  global.WebSocket = class MockWebSocket {
    constructor(public url: string) {}
    send() {}
    close() {}
    addEventListener() {}
    removeEventListener() {}
  } as any;
}

console.log("[Test Setup] Test environment initialized");
