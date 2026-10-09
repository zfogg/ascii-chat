import { defineConfig } from "@playwright/test";
export default defineConfig({
  testDir: "./tests/e2e",
  testMatch: [
    "test-pattern-parity.spec.ts",
    "mirror-frame-cadence.spec.ts",
    "mirror-rendering.spec.ts",
    "client-test2-fps.spec.ts",
  ],
  workers: 1,
  reporter: [
    ["list"],
    ["json", { outputFile: "test-results/pattern-results.json" }],
  ],
  use: {
    baseURL: "http://localhost:3810",
    headless: true,
    viewport: { width: 1440, height: 1000 },
  },
  webServer: {
    command: "vp dev --port 3810 --strictPort",
    url: "http://localhost:3810",
    reuseExistingServer: true,
  },
});
