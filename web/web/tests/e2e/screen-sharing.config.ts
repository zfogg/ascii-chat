import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: ".",
  testMatch: "screen-sharing.spec.ts",
  workers: 1,
  timeout: 90_000,
  reporter: "list",
  use: {
    ...devices["Desktop Chrome"],
    baseURL: "http://localhost:4196",
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
    command: "vp dev --port 4196 --strictPort",
    url: "http://localhost:4196",
    reuseExistingServer: true,
  },
});
