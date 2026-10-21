import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: ".",
  testMatch: "screen-sharing.spec.ts",
  workers: 1,
  timeout: 90_000,
  reporter: "list",
  use: {
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
    ...devices["Desktop Chrome"],
    baseURL: "http://127.0.0.1:4196",
    viewport: { width: 1500, height: 1000 },
    permissions: ["camera", "microphone"],
    launchOptions: {
      args: [
        "--use-fake-device-for-media-stream",
        "--use-fake-ui-for-media-stream",
      ],
    },
  },
  webServer: {
    command: "vp dev --host 127.0.0.1 --port 4196 --strictPort",
    url: "http://127.0.0.1:4196",
    reuseExistingServer: true,
  },
});
