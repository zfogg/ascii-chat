import { defineConfig, devices } from "@playwright/test";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
export default defineConfig({
  testDir: ".",
  testMatch: ["recording.spec.ts", "session-loading.spec.ts"],
  workers: 1,
  timeout: 120_000,
  reporter: "list",
  expect: { timeout: 30_000 },
  outputDir:
    process.env["RECORDING_EVIDENCE"] ??
    path.join(tmpdir(), "ascii-recording-evidence"),
  use: {
    ...devices["Desktop Chrome"],
    baseURL: "http://127.0.0.1:4207",
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
    cwd: fileURLToPath(new URL("../..", import.meta.url)),
    command: "vp dev --host 127.0.0.1 --port 4207 --strictPort",
    url: "http://127.0.0.1:4207",
    reuseExistingServer: true,
    timeout: 120_000,
  },
});
