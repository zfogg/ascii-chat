import { defineConfig } from "@playwright/test";
import base from "./playwright.config";

export default defineConfig({
  ...base,
  testMatch: "adaptive-upload.spec.ts",
  workers: 1,
  retries: 0,
  projects: [{ name: "chrome", use: { channel: "chrome" } }],
  reporter: [["list"]],
  use: { ...base.use, baseURL: "http://127.0.0.1:30841" },
  webServer: {
    command: "vp dev --host 127.0.0.1 --port 30841 --strictPort",
    url: "http://127.0.0.1:30841",
    reuseExistingServer: true,
    timeout: 120000,
  },
});
