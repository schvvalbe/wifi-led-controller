const { test, expect } = require("@playwright/test");
test.use({ serviceWorkers: "allow" });

test("installable manifest and offline shell never cache device commands", async ({
  page,
  context,
  browserName,
}) => {
  // Playwright WebKit's offline flag prevents even cached SW navigations.
  // https://github.com/microsoft/playwright/issues/42775
  test.skip(browserName === "webkit", "WebKit offline-emulation limitation; covered in Chromium");
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  const manifest = await page.evaluate(async () =>
    (await fetch("/manifest.webmanifest")).json(),
  );
  expect(manifest.display).toBe("standalone");
  expect(manifest.icons.map((icon) => icon.sizes)).toContain("192x192");
  expect(manifest.icons.map((icon) => icon.sizes)).toContain("512x512");
  await page.evaluate(async () => {
    await navigator.serviceWorker.ready;
  });
  await expect
    .poll(() =>
      page.evaluate(() => Boolean(navigator.serviceWorker.controller)),
    )
    .toBe(true);
  const cachePaths = await page.evaluate(async () => {
    const cache = await caches.open("wifi-led-controller-shell-v4");
    return (await cache.keys()).map((request) => new URL(request.url).pathname);
  });
  expect(cachePaths).toContain("/app.js");
  expect(cachePaths.some((path) => path.startsWith("/api/"))).toBe(false);
  await context.setOffline(true);
  await page.goto("/?source=homescreen");
  await expect(page.locator("h1")).toHaveText("Подсветка");
  await expect(page.locator("#connection")).toHaveText("Нет связи");
  await expect(page.locator("#power")).toBeDisabled();
  expect(
    await page.evaluate(async () => {
      try {
        await fetch("/api/power", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: '{"on":true}',
        });
        return false;
      } catch (_) {
        return true;
      }
    }),
  ).toBe(true);
  await context.setOffline(false);
  expect(errors).toEqual([]);
});

test("HTTP fallback explains adding a home-screen shortcut", async ({
  page,
}) => {
  await page.addInitScript(() =>
    Object.defineProperty(window, "isSecureContext", { value: false }),
  );
  await page.goto("/");
  await page.locator("#install-app").click();
  await expect(page.locator("#app-help")).toHaveAttribute("open", "");
  await expect(page.locator("#app-instructions")).toContainText("HTTP");
  await expect(page.locator("#app-instructions")).toContainText("HTTPS");
});
