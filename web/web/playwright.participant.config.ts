import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: "./tests/e2e",
  testMatch: "participant-media.spec.ts",
  workers: 1,
  reporter: "list",
  use: {
    ...devices["Desktop Chrome"],
    headless: true,
    permissions: ["camera", "microphone"],
    launchOptions: {
      args: [
        "--use-fake-device-for-media-stream",
        "--use-fake-ui-for-media-stream",
      ],
    },
  },
  webServer: {
    command: "vp dev --port 33198 --strictPort",
    url: "http://localhost:33198",
    reuseExistingServer: false,
    timeout: 120000,
  },
});
