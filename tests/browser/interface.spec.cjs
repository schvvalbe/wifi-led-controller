const { test, expect } = require("@playwright/test");

async function device(page) {
  const state = {
    settings: {
      mode: "auto",
      useLightSensor: true,
      holdSeconds: 30,
      darkThreshold: 50,
      brightness: 70,
      color: "#ffb464",
      fadeInMs: 700,
      fadeOutMs: 1200,
    },
    on: false,
    remainingSeconds: 0,
    sensors: {
      motion1: false,
      motion2: true,
      ambient: 35,
      dark: true,
      warmupSeconds: 0,
    },
    network: {
      connected: false,
      connecting: false,
      testing: false,
      ssid: "",
      ip: "",
      hostname: "wifi-led-controller-123abc.local",
      rssi: 0,
      apActive: true,
      apSsid: "PhysTechLab-123ABC",
      apIp: "192.168.4.1",
      error: "",
    },
    storage: "saved",
    version: "1.1.0",
    ota: { maxSize: 512000, busy: false, passwordRequired: true },
  };
  const calls = [];
  let offline = false;
  await page.route("**/api/**", async (route) => {
    if (offline) {
      await route.abort();
      return;
    }
    const req = route.request();
    const path = new URL(req.url()).pathname;
    const body = req.method() === "POST" ? req.postDataJSON() : null;
    calls.push({ path, body });
    let response = state;
    if (path === "/api/power") {
      state.on = body.on;
      state.settings.mode = "manual";
    }
    if (path === "/api/settings") {
      if (body.mode === "auto" && state.settings.mode !== "auto")
        state.on = false;
      Object.assign(state.settings, body);
    }
    if (path === "/api/wifi") {
      state.network.testing = true;
      state.network.connecting = true;
    }
    if (path === "/api/wifi/scan")
      response =
        req.method() === "POST"
          ? { scanning: true }
          : {
              scanning: false,
              networks: [
                {
                  ssid: "<img src=x onerror=alert(1)>",
                  rssi: -40,
                  secure: true,
                },
                { ssid: "Home 2.4", rssi: -55, secure: true },
              ],
            };
    await route.fulfill({ json: response });
  });
  await page.goto("/");
  await expect(page.locator("#connection")).toHaveText("Контроллер на связи");
  return {
    state,
    calls,
    disconnect: () => {
      offline = true;
    },
    reconnect: () => {
      offline = false;
    },
  };
}

test("manual override, settings, sensor calibration and responsive layout", async ({
  page,
}, testInfo) => {
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const d = await device(page);
  await expect(page.locator("#setup-info")).toContainText("192.168.4.1");
  await page.locator("#power").click();
  await expect(page.locator("input[value=manual]")).toBeChecked();
  await expect(page.locator("#light-title")).toHaveText("Свет включён");
  await page.locator('[data-color="#5c9dff"]').click();
  await page.locator("#hold").fill("120");
  await page.locator("#calibrate").click();
  await expect(page.locator("#threshold")).toHaveValue("45");
  // Polling must preserve the user's unsaved fields.
  await expect
    .poll(() => d.calls.filter((c) => c.path === "/api/state").length)
    .toBeGreaterThan(1);
  await expect(page.locator("#hold")).toHaveValue("120");
  await page.locator("#save").click();
  await expect(page.locator("#save-status")).toHaveText("Настройки сохранены");
  expect(d.state.settings.holdSeconds).toBe(120);
  expect(d.state.settings.color).toBe("#5c9dff");
  expect(d.state.settings.darkThreshold).toBe(45);
  await page.locator("input[value=auto]").check();
  await expect(page.locator("#auto-badge")).toHaveText("Активна");
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.locator("h1").click();
  await page.screenshot({
    path: `test-results/${testInfo.project.name}.png`,
    fullPage: true,
  });
  expect(errors).toEqual([]);
});

test("Wi-Fi scanning escapes names, supports manual SSID and submits credentials", async ({
  page,
}) => {
  const d = await device(page);
  await page.locator("#scan").click();
  await expect(page.locator("#scan-status")).toHaveText("Найдено сетей: 2");
  await expect(page.locator("#networks option")).toHaveCount(3);
  await expect(page.locator("#networks img")).toHaveCount(0);
  await page.locator("#networks").selectOption("Home 2.4");
  await expect(page.locator("#ssid")).toHaveValue("Home 2.4");
  await page.locator("#password").fill("test-pass-123");
  await page.locator("#show-password").check();
  await expect(page.locator("#password")).toHaveAttribute("type", "text");
  await page.locator("#connect").click();
  await expect(page.locator("#wifi-result")).toContainText(
    "Проверяем подключение",
  );
  expect(d.calls.find((c) => c.path === "/api/wifi").body).toEqual({
    ssid: "Home 2.4",
    password: "test-pass-123",
  });
  await expect(page.locator("#password")).toHaveValue("");
});

test("connection loss disables stale controls and recovers", async ({
  page,
}) => {
  const d = await device(page);
  d.disconnect();
  await expect(page.locator("#connection")).toHaveText("Нет связи", {
    timeout: 10000,
  });
  await expect(page.locator("#power")).toBeDisabled();
  d.reconnect();
  await expect(page.locator("#connection")).toHaveText("Контроллер на связи", {
    timeout: 10000,
  });
  await expect(page.locator("#power")).toBeEnabled();
});

test("themes follow the OS, persist explicit choices and show the developer", async ({
  page,
}, testInfo) => {
  await page.emulateMedia({ colorScheme: "dark" });
  await device(page);
  await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
  await expect(page.locator("#theme")).toHaveValue("system");
  await expect(page.locator(".developer-credit")).toHaveText(
    'Разработчик ООО "ФизТехЛаб" · 2026',
  );
  await page.locator("#theme").selectOption("light");
  await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
  await page.reload();
  await expect(page.locator("#theme")).toHaveValue("light");
  await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
  await page.locator("#theme").selectOption("system");
  await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
  await page.emulateMedia({ colorScheme: "light" });
  await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
  await page.locator("#theme").selectOption("dark");
  await expect(page.locator("meta[name=theme-color]")).toHaveAttribute(
    "content",
    "#141a17",
  );
  await page.locator("h1").click();
  await page.screenshot({
    path: `test-results/${testInfo.project.name}-dark.png`,
    fullPage: true,
  });
  await page.screenshot({
    path: `test-results/${testInfo.project.name}-dark-preview.png`,
  });
  await page.locator("#theme").selectOption("light");
  await page.locator("h1").click();
  await page.screenshot({
    path: `test-results/${testInfo.project.name}-light-preview.png`,
  });
  if (testInfo.project.name === "mobile") {
    await page.setViewportSize({ width: 320, height: 720 });
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth,
      ),
    ).toBe(true);
  }
});

test("Wi-Fi feedback is visible, keyboard focus leaves the password, success closes setup", async ({
  page,
}, testInfo) => {
  const d = await device(page);
  await page.locator("#ssid").fill("Home 2.4");
  await page.locator("#password").fill("test-password");
  await page.locator("#password").press("Enter");
  expect(d.calls.filter((call) => call.path === "/api/wifi")).toHaveLength(0);
  await page.locator("#connect").click();
  await expect(page.locator("#wifi-feedback")).toBeInViewport();
  await expect(page.locator("#wifi-feedback-title")).toHaveText(
    "Подключаемся к Wi-Fi…",
  );
  await expect(page.locator("#password")).not.toBeFocused();
  await expect(page.locator("#connect")).toBeDisabled();
  Object.assign(d.state.network, {
    testing: false,
    connecting: false,
    connected: true,
    ssid: "Home 2.4",
    ip: "192.168.1.42",
    rssi: -57,
  });
  d.state.storage = "pending";
  await expect(page.locator("#wifi-details")).not.toHaveAttribute("open", "");
  await expect(page.locator("#wifi-result")).toContainText(
    "Сохраняем настройки",
  );
  d.state.storage = "saved";
  await expect(page.locator("#wifi-result")).toContainText(
    "Настройки сохранены",
  );
  await expect(page.locator("#wifi-open")).toHaveAttribute(
    "href",
    "http://192.168.1.42/",
  );
  await expect(page.locator("#signal")).toHaveText("Wi-Fi: -57 dBm");
  await expect(page.locator("body")).not.toContainText("Работает без облака");
  await expect(page.locator("body")).not.toContainText("ВАШ СВЕТ. ВАШ РИТМ.");
  await expect(page.locator("body")).not.toContainText(
    "Комфортный свет — в одно касание.",
  );
  await page.screenshot({
    path: `test-results/${testInfo.project.name}-wifi-success.png`,
  });
});

test("Wi-Fi failure remains visible and permits correcting the password", async ({
  page,
}) => {
  const d = await device(page);
  await page.locator("#ssid").fill("Home 2.4");
  await page.locator("#password").fill("wrong-password");
  await page.locator("#connect").click();
  d.disconnect();
  await expect(page.locator("#wifi-feedback-title")).toHaveText(
    "Связь с контроллером прервалась",
    { timeout: 10000 },
  );
  Object.assign(d.state.network, {
    testing: false,
    connecting: false,
    error: "Неверный пароль. Прежняя сеть сохранена.",
  });
  d.reconnect();
  await expect(page.locator("#wifi-feedback-title")).toHaveText(
    "Не удалось подключиться",
    { timeout: 10000 },
  );
  await expect(page.locator("#wifi-details")).toHaveAttribute("open", "");
  await expect(page.locator("#password")).toBeEnabled();
  await expect(page.locator("#wifi-result")).toContainText(
    "Прежняя сеть сохранена",
  );
});


const { createHash } = require("node:crypto");
const firmwareImage = () => {
  const bytes = Buffer.alloc(4097, 0x55);
  bytes[0] = 0xe9;
  return bytes;
};
async function chooseFirmware(page, buffer = firmwareImage()) {
  await page.locator("#firmware-file").setInputFiles({ name: "firmware.bin", mimeType: "application/octet-stream", buffer });
  await page.locator("#firmware-password").fill("test-password");
}

test("OTA validates file checksum on plain HTTP and rejects unsuitable files", async ({ page }) => {
  await device(page);
  for (const size of [0, 1, 55, 56, 63, 64, 65, 4097, 500000]) {
    const bytes = Buffer.from(Array.from({ length: size }, (_, i) => i % 256));
    const actual = await page.evaluate((length) => window.firmwareMd5(Uint8Array.from({ length }, (_, i) => i % 256)), size);
    expect(actual).toBe(createHash("md5").update(bytes).digest("hex"));
  }
  let posts = 0;
  await page.route("**/api/ota", async (route) => { posts++; await route.abort(); });
  await chooseFirmware(page, Buffer.alloc(513000));
  await page.locator("#firmware-upload").click();
  await expect(page.locator("#firmware-result")).toContainText("подходящего размера");
  await chooseFirmware(page, Buffer.alloc(100));
  await page.locator("#firmware-upload").click();
  await expect(page.locator("#firmware-result")).toContainText("не образ прошивки");
  expect(posts).toBe(0);
});

test("OTA sends one authenticated image, blocks controls, then reconnects after reboot", async ({ page }, testInfo) => {
  await page.emulateMedia({ reducedMotion: "reduce" });
  const d = await device(page);
  let release;
  const responseGate = new Promise((resolve) => { release = resolve; });
  const posts = [];
  await page.route("**/api/ota", async (route) => {
    if (route.request().method() === "GET") {
      await route.fulfill({ json: { version: "1.1.1", busy: false, maxSize: 512000 } });
      return;
    }
    posts.push(route.request());
    await responseGate;
    d.state.version = "1.1.1";
    await route.fulfill({ status: 202, json: { rebooting: true } });
  });
  await chooseFirmware(page);
  await page.locator("#firmware").screenshot({ path: `test-results/${testInfo.project.name}-ota.png` });
  await page.locator("#firmware-upload").click();
  await expect.poll(() => posts.length).toBe(1);
  await expect(page.locator("#power")).toBeDisabled();
  await expect(page.locator("#firmware-upload")).toBeDisabled();
  await expect(page.locator("#firmware-file")).toBeDisabled();
  const headers = posts[0].headers();
  expect(headers["x-ota-password"]).toBe("test-password");
  expect(headers["x-firmware-size"]).toBe("4097");
  expect(headers["x-firmware-md5"]).toBe(createHash("md5").update(firmwareImage()).digest("hex"));
  // WebKit's inspection protocol omits file/blob bytes from postDataBuffer:
  // https://github.com/microsoft/playwright/issues/6479
  if (testInfo.project.use.browserName !== "webkit")
    expect(posts[0].postDataBuffer().includes(firmwareImage())).toBe(true);
  release();
  await expect(page.locator("#firmware-result")).toContainText("перезапускается");
  await expect(page.locator("#firmware-version")).toHaveText("1.1.1", { timeout: 15000 });
  await expect(page.locator("#firmware-password")).toHaveValue("");
  expect(posts).toHaveLength(1);
});

test("OTA reports rejection and lost response without an automatic retry", async ({ page }) => {
  await device(page);
  let count = 0;
  let disconnect = false;
  await page.route("**/api/ota", async (route) => {
    count++;
    if (disconnect) await route.abort("failed");
    else await route.fulfill({ status: 403, json: { error: "Неверный пароль обновления." } });
  });
  await chooseFirmware(page);
  await page.locator("#firmware-upload").click();
  await expect(page.locator("#firmware-result")).toHaveText("Неверный пароль обновления.");
  await expect(page.locator("#firmware-password")).toHaveValue("");
  await expect(page.locator("#power")).toBeEnabled();
  disconnect = true;
  await chooseFirmware(page);
  await page.locator("#firmware-upload").click();
  await expect(page.locator("#firmware-result")).toContainText("результат обновления неизвестен");
  await expect(page.locator("#firmware-reconnect")).toBeVisible();
  await expect(page.locator("#firmware-password")).toHaveValue("");
  expect(count).toBe(2);
});


test("Wi-Fi focus leaves text fields on outside tap and keyboard actions", async ({ page }, testInfo) => {
  await device(page);
  await page.locator("#ssid").fill("Home");
  await expect(page.locator("#ssid")).toBeFocused();
  await page.locator("#ssid").press("Enter");
  await expect(page.locator("#password")).toBeFocused();
  await page.locator("#ssid").focus();
  if (testInfo.project.use.hasTouch) await page.locator("#wifi-title").tap();
  else await page.locator("#wifi-title").click();
  await expect(page.locator("#ssid")).not.toBeFocused();
  await page.locator("#ssid").focus();
  await page.locator("#password").click();
  await expect(page.locator("#password")).toBeFocused();
  await page.locator("#password").press("Enter");
  await expect(page.locator("#password")).not.toBeFocused();
  await expect(page.locator("#wifi-feedback")).toBeHidden();
  await page.locator("#connect").click();
  await expect(page.locator("#wifi-feedback-title")).toHaveText("Подключаемся к Wi-Fi…");
});

test("system reset requires confirmation, can be cancelled, and sends only once", async ({ page }, testInfo) => {
  const d = await device(page);
  const commands = [];
  await page.route("**/api/system/reset", async (route) => {
    commands.push(route.request().postDataJSON());
    await route.fulfill({ status: 202, json: { rebooting: true, reset: true } });
  });
  await page.locator("#system-reset").click();
  await expect(page.locator("#system-confirm")).toBeVisible();
  expect(commands).toHaveLength(0);
  await page.locator("#system-cancel").click();
  await expect(page.locator("#system-confirm")).toBeHidden();
  expect(commands).toHaveLength(0);
  await page.locator("#system-reset").click();
  await page.locator("#system-confirm-action").scrollIntoViewIfNeeded();
  await page.screenshot({ path: `test-results/${testInfo.project.name}-system.png` });
  await page.locator("#system-confirm-action").click();
  await expect(page.locator("#system-result")).toContainText("Настройки удалены");
  await expect(page.locator("#system-result")).toContainText(d.state.network.apSsid);
  await expect(page.locator("#system-result")).toContainText("192.168.4.1");
  await expect(page.locator("#system-reset")).toBeDisabled();
  await expect(page.locator("#power")).toBeDisabled();
  expect(commands).toEqual([{ confirm: true }]);
});

test("system reboot reconnects and preserves settings", async ({ page }) => {
  const d = await device(page);
  const commands = [];
  await page.route("**/api/system/reboot", async (route) => {
    commands.push(route.request().postDataJSON());
    await route.fulfill({ status: 202, json: { rebooting: true } });
  });
  await page.locator("#system-reboot").click();
  await page.locator("#system-confirm-action").click();
  await expect(page.locator("#system-result")).toContainText("Контроллер перезагружен", { timeout: 10000 });
  await expect(page.locator("#system-reboot")).toBeEnabled();
  expect(d.state.settings.brightness).toBe(70);
  expect(commands).toEqual([{ confirm: true }]);
});

test("lost reset response never repeats the destructive command", async ({ page }) => {
  await device(page);
  let commands = 0;
  await page.route("**/api/system/reset", async (route) => {
    commands++;
    await route.abort();
  });
  await page.locator("#system-reset").click();
  await page.locator("#system-confirm-action").click();
  await expect(page.locator("#system-reconnect")).toBeVisible();
  await expect(page.locator("#system-reset")).toBeDisabled();
  await page.locator("#system-reconnect").click();
  await expect(page.locator("#system-reset")).toBeEnabled();
  expect(commands).toBe(1);
});


for (const dismissal of ["Done", "native blur"]) {
test(`Wi-Fi draft stays untouched after ${dismissal} and periodic status updates`, async ({ page }) => {
  const d = await device(page);
  await page.locator("#ssid").fill("Home 2.4");
  await page.locator("#password").fill("test-password");
  if (dismissal === "Done") await page.locator("#password").press("Enter");
  else await page.locator("#password").evaluate((input) => input.blur());
  await expect(page.locator("#password")).not.toBeFocused();
  expect(d.calls.filter((call) => call.path === "/api/wifi")).toHaveLength(0);
  await page.evaluate(() => {
    window.wifiFormChanges = [];
    window.wifiFocusChanges = [];
    const details = document.getElementById("wifi-details");
    new MutationObserver((records) => window.wifiFormChanges.push(...records.map((r) => r.target.id || r.target.nodeName)))
      .observe(details, { attributes: true, childList: true, subtree: true, characterData: true });
    details.addEventListener("focusin", (event) => window.wifiFocusChanges.push(event.target.id));
  });
  const count = d.calls.filter((call) => call.path === "/api/state").length;
  // Wait through three real polling cycles, not just the immediate blur event.
  await expect.poll(() => d.calls.filter((call) => call.path === "/api/state").length, { timeout: 10000 }).toBeGreaterThanOrEqual(count + 3);
  await expect(page.locator("#ssid")).not.toBeFocused();
  await expect(page.locator("#password")).not.toBeFocused();
  await expect(page.locator("#ssid")).toHaveValue("Home 2.4");
  await expect(page.locator("#password")).toHaveValue("test-password");
  expect(await page.evaluate(() => window.wifiFormChanges)).toEqual([]);
  expect(await page.evaluate(() => window.wifiFocusChanges)).toEqual([]);
  await page.locator("#connect").click();
  await expect.poll(() => d.calls.filter((call) => call.path === "/api/wifi").length).toBe(1);
});

}

test("Wi-Fi setup stays collapsed while polling in setup mode", async ({ page }) => {
  const d = await device(page);
  await page.locator("#ssid").fill("Home 2.4");
  await page.locator("#password").fill("test-password");
  await page.locator("#wifi-details summary").click();
  await expect(page.locator("#wifi-details")).not.toHaveAttribute("open", "");
  const count = d.calls.filter((call) => call.path === "/api/state").length;
  await expect.poll(() => d.calls.filter((call) => call.path === "/api/state").length).toBeGreaterThan(count);
  await expect(page.locator("#wifi-details")).not.toHaveAttribute("open", "");
});
