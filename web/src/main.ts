import './style.css';
import { inject } from '@vercel/analytics';

// Initialize Vercel Analytics
inject();

// Simple router
const app = document.querySelector<HTMLDivElement>('#app')!;

const route = window.location.pathname;

// Placeholder for modes - will be implemented in Phase 1
if (route === '/mirror' || route === '/mirror/') {
  app.innerHTML = `
    <div class="h-screen flex items-center justify-center">
      <div class="text-center">
        <h1 class="text-4xl font-bold text-terminal-cyan mb-4">ascii-chat | Mirror Mode</h1>
        <p class="text-terminal-fg mb-8">Coming soon: Webcam → ASCII rendering</p>
      </div>
    </div>
  `;
} else if (route === '/client' || route === '/client/') {
  app.innerHTML = `
    <div class="h-screen flex items-center justify-center">
      <div class="text-center">
        <h1 class="text-4xl font-bold text-terminal-green mb-4">ascii-chat | Client Mode</h1>
        <p class="text-terminal-fg mb-8">Coming soon: WebSocket client</p>
      </div>
    </div>
  `;
} else if (route === '/discovery' || route === '/discovery/') {
  app.innerHTML = `
    <div class="h-screen flex items-center justify-center">
      <div class="text-center">
        <h1 class="text-4xl font-bold text-terminal-magenta mb-4">ascii-chat | Discovery Mode</h1>
        <p class="text-terminal-fg mb-8">Coming soon: WebRTC P2P connections</p>
      </div>
    </div>
  `;
} else {
  // Home page
  app.innerHTML = `
    <div class="h-screen flex items-center justify-center">
      <div class="text-center max-w-2xl px-8">
        <pre class="text-terminal-cyan text-xl mb-8 font-mono">
  __ _ ___  ___ _ _ ___      ___ _  _  _ _____
 / _\` / __|/ __| |_| (_)___ / __| || |/ _|_   _|
| (_| \\__ \\ (__| /_| | |___| (__| __ | |_  | |
 \\__,_|___/\\___|_(_|_|_|    \\___|_||_|\\__| |_|
        </pre>
        <h1 class="text-3xl font-bold text-terminal-fg mb-4">Terminal Video Chat in Your Browser</h1>
        <p class="text-terminal-fg mb-8 text-lg">Real-time video chat with ASCII art rendering, end-to-end encryption, and WebRTC support</p>
        <div class="space-y-4 flex flex-col items-center">
          <a href="/mirror" class="px-6 py-3 bg-terminal-cyan text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64">
            Mirror Mode
          </a>
          <a href="/client" class="px-6 py-3 bg-terminal-green text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64">
            Client Mode
          </a>
          <a href="/discovery" class="px-6 py-3 bg-terminal-magenta text-terminal-bg rounded hover:opacity-80 transition-opacity inline-block w-64">
            Discovery Mode
          </a>
        </div>
        <p class="text-terminal-brightBlack mt-8 text-sm">
          Phase 0: Project scaffolding complete ✓
        </p>
      </div>
    </div>
  `;
}
