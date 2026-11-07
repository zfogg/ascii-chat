import assert from "node:assert/strict";
import console from "node:console";
import process from "node:process";
import { URL } from "node:url";
import { chromium, expect } from "@playwright/test";

// Run after prepare_cloudflare.py, against a local server or the published site.
const origin = process.argv[2] || "http://127.0.0.1:4189";
const browser = await chromium.launch();
const errors = [];
async function ready(page) {
  await page.waitForSelector(".node");
  await page.waitForFunction(
    "typeof restoringState !== 'undefined' && !restoringState",
  );
}
async function snapshot(page) {
  return page.evaluate(() => {
    const document = globalThis.document;
    return {
      url: globalThis.location.href,
      transform: document.querySelector("#world").style.transform,
      nodes: [...document.querySelectorAll(".node")].map((el) => [
        el.dataset.id,
        el.className,
      ]),
      edges: [...document.querySelectorAll(".wire")].map((el) =>
        el.getAttribute("class"),
      ),
      title: document.querySelector("#title").textContent,
      details: document.querySelector("#details").innerHTML,
      inspector: globalThis.getComputedStyle(
        document.querySelector(".inspector"),
      ).display,
      scroll: document.querySelector(".inspector").scrollTop,
      expanded: document.body.classList.contains("expanded"),
    };
  });
}
async function survivesReload(page) {
  await page.waitForTimeout(150);
  const before = await snapshot(page);
  await page.reload();
  await ready(page);
  assert.deepEqual(await snapshot(page), before);
}
try {
  for (const width of [1600, 700, 390]) {
    const page = await browser.newPage({ viewport: { width, height: 1000 } });
    page.setDefaultTimeout(10000);
    page.on("pageerror", (error) => errors.push(error.message));
    await page.goto(`${origin}/?campaign=keep#hosting/distribution`);
    await ready(page);
    await expect(page.locator("#detail-close")).toBeVisible();
    await page.locator("#detail-close").click();
    await expect(page.locator(".inspector")).toBeHidden();
    await page.locator(".node").first().click();
    await expect(page.locator(".inspector")).toBeVisible();
    await page.locator("#detail-close").click();
    await page.locator("#plus").click();
    const viewport = await page.locator("#viewport").boundingBox();
    await page.mouse.move(viewport.x + 12, viewport.y + 45);
    await page.mouse.down();
    await page.mouse.move(viewport.x + 58, viewport.y + 72, { steps: 4 });
    await page.mouse.up();
    const params = new URL(page.url()).searchParams;
    assert.equal(params.get("inspector"), "closed");
    assert.ok(params.get("node"));
    for (const key of ["zoom", "x", "y"]) assert.ok(params.has(key));
    assert.equal(params.get("campaign"), "keep");
    await survivesReload(page);

    const oldZoom = await page.locator("#zoom").textContent();
    await page.mouse.move(viewport.x + 60, viewport.y + 60);
    await page.mouse.wheel(0, -100);
    await expect(page.locator("#zoom")).not.toHaveText(oldZoom);
    const wheelTransform = await page.locator("#world").getAttribute("style");
    // An immediate reload must retain the last wheel event.
    await page.reload();
    await ready(page);
    assert.equal(
      await page.locator("#world").getAttribute("style"),
      wheelTransform,
    );

    // A copied URL must reproduce the same view in a fresh browser context.
    const copy = await browser.newPage({ viewport: { width, height: 1000 } });
    await copy.goto(page.url());
    await ready(copy);
    assert.deepEqual(await snapshot(copy), await snapshot(page));
    await copy.close();

    await page.locator("#inspector-toggle").click();
    await expect(page.locator("#reference-trace")).toBeVisible();
    await page.locator("#reference-trace").click();
    await page.locator("#detail-close").click();
    assert.ok(new URL(page.url()).searchParams.get("trace"));
    await survivesReload(page);

    await page.locator("#inspector-toggle").click();
    await page.locator("#reference-back").click();
    await page.locator("#explore").click();
    await page.locator("#detail-close").click();
    assert.ok(new URL(page.url()).searchParams.get("file"));
    await survivesReload(page);
    const edge = page.locator("[data-edge]").first();
    await edge.focus();
    await edge.press("Enter");
    await page.locator("#detail-close").click();
    assert.ok(new URL(page.url()).searchParams.get("edgeSource"));
    await survivesReload(page);

    await page.locator("#expand").click();
    await page.locator("#guide-notes").click();
    await expect(page.locator(".guide-question")).toHaveText(
      "Render and distribute",
    );
    await page.locator(".inspector").evaluate((el) => {
      el.scrollTop = 200;
    });
    await survivesReload(page);
    const rect = await page.locator(".inspector").boundingBox();
    assert.ok(rect.x >= 0 && rect.x + rect.width <= width + 1);
    await expect(page.locator("#detail-close")).toBeInViewport();
    await page.locator("#detail-close").click();
    await survivesReload(page);
    await page.locator("#reset").click();
    const reset = new URL(page.url());
    for (const key of [
      "node",
      "edgeSource",
      "file",
      "trace",
      "zoom",
      "x",
      "y",
      "symbol",
    ])
      assert.equal(reset.searchParams.has(key), false);
    assert.equal(reset.searchParams.get("campaign"), "keep");
    await survivesReload(page);
    console.log(
      `PASS ${width}px: close/reopen, selected node/edge, pan/zoom, file/trace, copied URL, notes scroll, expanded mode, reset and refresh`,
    );
    await page.close();
  }
  const page = await browser.newPage();
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto(
    `${origin}/?zoom=NaN&x=Infinity&y=bad&node=missing&file=missing&trace=missing#%ZZ`,
  );
  await ready(page);
  assert.ok(
    !(await page.locator("#world").getAttribute("style")).includes("NaN"),
  );
  await survivesReload(page);
  assert.deepEqual(errors, []);
  console.log("PASS malformed state falls back safely");
} finally {
  await browser.close();
}
