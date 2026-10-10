import { defineConfig } from "@playwright/test";
import base from "./playwright.config";

export default defineConfig({
  ...base,
  testMatch: "video-encoding.spec.ts",
  workers: 1,
  reporter: "list",
  timeout: 90_000,
  use: {
    ...base.use,
    viewport: { width: 1280, height: 900 },
    screenshot: "on",
    trace: "retain-on-failure",
  },
  projects: [
    {
      name: "chrome",
      use: {
        channel: "chrome",
        headless: false,
        permissions: ["camera", "microphone"],
      },
    },
  ],
});
