import { chromium } from "@playwright/test";

const [base = "http://127.0.0.1:3000"] = process.argv.slice(2);
const browser = await chromium.launch({ headless: true });
try {
  const page = await browser.newPage();
  await page.goto(new URL("/tests/wasm/stereo-audio.html", base).href);
  await page.waitForFunction(() => window.stereoProof, null, {
    timeout: 60000,
  });
  const proof = await page.evaluate(() => window.stereoProof);
  if (!proof.passed) throw new Error(JSON.stringify(proof));
  console.log("PASS real Chromium worklet + C/WASM stereo codec", proof);
} finally {
  await browser.close();
}
