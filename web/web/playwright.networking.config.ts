import { defineConfig, devices } from "@playwright/test";
const port = 32157;
export default defineConfig({
  testDir: "./tests/e2e",
  testMatch: "unified-networking.spec.ts",
  workers: 1,
  timeout: 90_000,
  reporter: [
    ["list"],
    ["json", { outputFile: "../../build/issue-507/browser/results.json" }],
  ],
  outputDir: "../../build/issue-507/browser/artifacts",
  use: {
    ...devices["Desktop Chrome"],
    baseURL: `http://127.0.0.1:${port}`,
    permissions: ["camera", "microphone"],
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
    launchOptions: {
      args: [
        "--use-fake-device-for-media-stream",
        "--use-fake-ui-for-media-stream",
      ],
    },
  },
  webServer: {
    command: `vp dev --host 127.0.0.1 --port ${port} --strictPort`,
    url: `http://127.0.0.1:${port}`,
    reuseExistingServer: false,
    timeout: 120_000,
  },
});
