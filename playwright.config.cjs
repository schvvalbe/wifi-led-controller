const { defineConfig } = require("@playwright/test");
module.exports = defineConfig({
  testDir: "./tests/browser",
  use: {
    baseURL: "http://127.0.0.1:8123",
    headless: true,
    serviceWorkers: "block",
  },
  webServer: {
    command: `"${process.execPath}" scripts/preview.mjs`,
    url: "http://127.0.0.1:8123",
    reuseExistingServer: false,
  },
  projects: [
    {
      name: "mobile-webkit",
      use: { browserName: "webkit", viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true },
    },
    { name: "desktop", use: { viewport: { width: 1440, height: 1050 } } },
    {
      name: "mobile",
      use: {
        viewport: { width: 390, height: 844 },
        isMobile: true,
        hasTouch: true,
      },
    },
  ],
});
