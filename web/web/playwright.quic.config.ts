/// <reference types="node" />
import { defineConfig, devices } from "@playwright/test";
import base from "./playwright.config";

export default defineConfig({
  ...base,
  testMatch: "discovery-quic.spec.ts",
  workers: 1,
  retries: 0,
  reporter: "list",
  projects: [
    {
      name: "chromium",
      use: {
        ...devices["Desktop Chrome"],
        permissions: ["camera", "microphone"],
        launchOptions: {
          ...(process.env["ASCII_CHAT_TEST_CHROMIUM"]
            ? { executablePath: process.env["ASCII_CHAT_TEST_CHROMIUM"] }
            : {}),
          args: [
            "--use-fake-device-for-media-stream",
            "--use-fake-ui-for-media-stream",
          ],
        },
      },
    },
  ],
});
