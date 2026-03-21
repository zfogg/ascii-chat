import { chromium } from '@playwright/test';
const browser = await chromium.launch();
const page = await browser.newPage();
await page.goto('http://localhost:5173/man3', { waitUntil: 'networkidle' });
await page.waitForTimeout(2000);

console.log('INPUTS:', await page.locator('input').count());
const inputs = await page.locator('input').all();
for (let i = 0; i < inputs.length; i++) {
  const placeholder = await inputs[i].getAttribute('placeholder');
  console.log(`  [${i}] placeholder="${placeholder}"`);
}

console.log('PAGE LINKS:', await page.locator('a[href*="page"]').count());
const links = await page.locator('a[href*="page"]').slice(0, 1);
if (await links.count() > 0) {
  console.log(`  href="${await links.getAttribute('href')}"`);
}

console.log('TITLE:', await page.title());
await browser.close();
