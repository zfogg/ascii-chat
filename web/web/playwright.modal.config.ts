import { defineConfig } from "@playwright/test";
import base from "./playwright.config";

const url = "http://127.0.0.1:4194";

export default defineConfig({
  ...base,
  testMatch: "modal-layout.spec.ts",
  workers: 1,
  retries: 0,
  reporter: "list",
  use: { ...base.use, baseURL: url },
  webServer: {
    command: "vp dev --host 127.0.0.1 --port 4194 --strictPort",
    url,
    reuseExistingServer: true,
  },
});
