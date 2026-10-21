import { defineConfig } from "@playwright/test";
import base from "./playwright.config";

// Keep hardware, native-server, and performance diagnostics out of PR checks.
// The transport spec uses real browser WebSockets against a local test server.
export default defineConfig({
  ...base,
  testMatch: [
    "connection-ci.spec.ts",
    "settings-lifecycle.spec.ts",
    "settings-persistence.spec.ts",
    "session-ui.spec.ts",
    "mirror-file-upload.spec.ts",
    "wasm-assets.spec.ts",
    "runtime-settings.spec.ts",
    "face-follow.spec.ts",
    "screen-sharing.spec.ts",
  ],
  grepInvert:
    /connected web client shows its C network logs|native host applies live browser settings/,
  workers: 1,
  timeout: 60_000,
  retries: 0,
  reporter: [
    ["list"],
    ["html", { open: "never" }],
    ["junit", { outputFile: "test-results/playwright.xml" }],
  ],
  use: {
    ...base.use,
    trace: "retain-on-failure",
    video: "retain-on-failure",
  },
  webServer: {
    command: "vp dev --host 127.0.0.1 --port 3000 --strictPort",
    url: "http://127.0.0.1:3000",
    reuseExistingServer: false,
    timeout: 120_000,
  },
});
