import { defineConfig, devices } from "@playwright/test";
import { fileURLToPath } from "node:url";

export default defineConfig({
  testDir: ".",
  testMatch: ["runtime-settings.spec.ts", "session-ui.spec.ts"],
  workers: 1,
  expect: { timeout: 15000 },
  timeout: 120000,
  reporter: "list",
  use: {
    ...devices["Desktop Chrome"],
    baseURL: "http://127.0.0.1:35127",
    viewport: { width: 1440, height: 1000 },
    permissions: ["camera", "microphone"],
    launchOptions: {
      args: [
        "--use-fake-device-for-media-stream",
        "--use-fake-ui-for-media-stream",
      ],
    },
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
  },
  webServer: {
    cwd: fileURLToPath(new URL("../..", import.meta.url)),
    command: "vp dev --host 127.0.0.1 --port 35127 --strictPort",
    url: "http://127.0.0.1:35127",
    reuseExistingServer: false,
    timeout: 120000,
  },
});
