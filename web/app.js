"use strict";
const $ = (id) => document.getElementById(id);
$("theme").value = window.wifiLedControllerTheme.preference;
$("theme").addEventListener("change", () =>
  window.wifiLedControllerTheme.set($("theme").value),
);
let state = null,
  dirty = false,
  busy = false,
  online = false,
  polling = false,
  wifiEdited = false;
let revision = 0;
let wifiRequest = null;
let wifiOutcome = "";
let firmwareBusy = false;
let systemChoice = "";
const settingsFields = [
  "color",
  "brightness",
  "fade-in",
  "fade-out",
  "hold",
  "use-light",
  "threshold",
];

async function api(path, body) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 8000);
  try {
    const response = await fetch(path, {
      cache: "no-store",
      signal: controller.signal,
      ...(body === undefined
        ? {}
        : {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(body),
          }),
    });
    const result = await response.json();
    if (!response.ok)
      throw new Error(result.error || "Не удалось выполнить запрос");
    return result;
  } finally {
    clearTimeout(timeout);
  }
}

function notice(message = "") {
  $("notice").textContent = message;
  $("notice").hidden = !message;
}
function setDisabled(id, disabled) {
  const element = $(id);
  if (element.disabled !== disabled) element.disabled = disabled;
}
function setText(id, text) {
  if ($(id).textContent !== text) $(id).textContent = text;
}
function setWifiDetails(open) {
  if ($("wifi-details").open !== open) $("wifi-details").open = open;
}
function controls() {
  const unavailable = !online || busy;
  for (const id of ["settings-fields", "mode-picker", "power"])
    setDisabled(id, unavailable);
  const testing = Boolean(state?.network.testing);
  // Set the final disabled state once; do not briefly enable this fieldset on polls.
  setDisabled("wifi-fields", unavailable || testing);
  setDisabled("connect", testing);
  setText("connect", testing ? "Подключаемся…" : "Подключиться");
  setDisabled("scan", Boolean(state?.network.connecting || testing));
  setDisabled("system-fields", unavailable || Boolean(state?.ota?.busy || state?.network.connecting || testing));
  setDisabled("firmware-fields", unavailable || dirty || !state?.ota?.maxSize ||
    Boolean(state?.ota?.busy || state?.network.connecting || testing));
}
function wifiFeedback(kind, title, message, address = "") {
  const panel = $("wifi-feedback");
  panel.hidden = false;
  panel.dataset.kind = kind;
  setText("wifi-feedback-title", title);
  setText("wifi-result", message);
  const link = $("wifi-open");
  link.hidden = !/^\d{1,3}(\.\d{1,3}){3}$/.test(address);
  if (!link.hidden) link.href = `http://${address}/`;
  else link.removeAttribute("href");
}
function focusWifiFeedback() {
  document.activeElement?.blur();
  $("wifi-feedback").focus({ preventScroll: true });
  $("wifi-feedback").scrollIntoView({ block: "center", behavior: "smooth" });
}
function renderWifiFeedback(next, previous) {
  const n = next.network;
  if (n.testing) {
    wifiFeedback(
      "pending",
      "Подключаемся к Wi-Fi…",
      `Проверяем подключение${wifiRequest ? ` к «${wifiRequest.ssid}»` : ""}. Это займёт до 25 секунд. Дождитесь результата.`,
    );
  } else if (n.error) {
    wifiFeedback("error", "Не удалось подключиться", n.error);
    if (!wifiEdited && n.error !== previous?.network.error) setWifiDetails(true);
    if (wifiRequest && wifiOutcome !== "failed") {
      wifiOutcome = "failed";
      setWifiDetails(true);
      focusWifiFeedback();
    }
  } else if (n.connected) {
    const saved = next.storage === "saved";
    const failed = next.storage === "error";
    wifiFeedback(
      failed ? "error" : saved ? "success" : "pending",
      failed
        ? "Wi-Fi подключён, но настройки не сохранены"
        : `Подключено к «${n.ssid}»`,
      `${failed ? "Ошибка записи во Flash. После перезапуска данные могут потеряться." : saved ? "Настройки сохранены и восстановятся после перезапуска." : "Сохраняем настройки в память…"} ${n.apActive ? `Переключите телефон в сеть «${n.ssid}» и откройте http://${n.ip}.` : `Адрес контроллера: http://${n.ip}.`}`,
      n.ip,
    );
    if (
      wifiRequest &&
      wifiRequest.ssid === n.ssid &&
      wifiOutcome !== "success"
    ) {
      wifiOutcome = "success";
      setWifiDetails(false);
      focusWifiFeedback();
    }
  }
}
function describeSettings() {
  $("color-value").textContent = $("color").value.toUpperCase();
  $("brightness-value").value = `${$("brightness").value} %`;
  for (const field of ["fade-in", "fade-out"])
    $(field + "-value").value =
      `${(Number($(field).value) / 1000).toLocaleString("ru-RU", { maximumFractionDigits: 1 })} с`;
  $("threshold-value").value = $("threshold").value;
  $("threshold-wrap").hidden = !$("use-light").checked;
  document
    .querySelectorAll("[data-color]")
    .forEach((button) =>
      button.setAttribute(
        "aria-pressed",
        String(button.dataset.color === $("color").value),
      ),
    );
  if (dirty) $("save-status").textContent = "Есть неприменённые изменения";
}
function populateSettings(s) {
  $("color").value = s.color;
  $("brightness").value = s.brightness;
  $("fade-in").value = s.fadeInMs;
  $("fade-out").value = s.fadeOutMs;
  $("hold").value = s.holdSeconds;
  $("use-light").checked = s.useLightSensor;
  $("threshold").value = s.darkThreshold;
  describeSettings();
}
function render(next) {
  const previous = state;
  state = next;
  online = true;
  $("firmware-version").textContent = next.version || "—";
  $("footer-version").textContent = next.version ? `v${next.version}` : "";
  $("firmware-limit").textContent = next.ota?.maxSize
    ? `файл до ${Math.floor(next.ota.maxSize / 1024)} КБ`
    : "OTA недоступно в этой версии";
  const s = next.settings,
    n = next.network;
  $("connection").textContent = "Контроллер на связи";
  $("connection").className = "badge online";
  $("signal").hidden = !n.connected;
  $("signal").textContent = n.connected ? `Wi-Fi: ${n.rssi} dBm` : "";
  $("signal").title = "Уровень сигнала между контроллером и роутером";
  $("light-title").textContent = next.on ? "Свет включён" : "Свет выключен";
  $("power").setAttribute("aria-pressed", String(next.on));
  const powerText = next.on ? "Выключить" : "Включить";
  $("power-label").textContent = powerText;
  $("power").setAttribute("aria-label", powerText + " подсветку");
  $("light-detail").textContent =
    s.mode === "manual"
      ? "Ручное управление"
      : next.on
        ? `Выключение через ${next.remainingSeconds} с без движения`
        : "Ожидаем движения";
  document.querySelectorAll("input[name=mode]").forEach((input) => {
    input.checked = input.value === s.mode;
  });
  $("auto-badge").textContent = s.mode === "auto" ? "Активна" : "Не активна";
  if (!dirty) populateSettings(s);
  if (next.storage === "error")
    $("save-status").textContent =
      "Ошибка памяти: настройки действуют до перезапуска";
  else if (!dirty)
    $("save-status").textContent =
      next.storage === "pending"
        ? "Сохраняем в память…"
        : "Настройки сохранены";
  $("ambient").textContent = `${next.sensors.ambient} / 1023`;
  for (const key of ["motion1", "motion2"])
    $(key).textContent = next.sensors[key] ? "Движение" : "Нет движения";
  $("warmup").hidden = !next.sensors.warmupSeconds;
  $("warmup").textContent =
    `Прогрев датчиков: ещё ${next.sensors.warmupSeconds} с. Ручное управление доступно.`;
  $("wifi-badge").textContent = n.testing
    ? "Проверяем сеть…"
    : n.connected
      ? "Подключён"
      : n.connecting
        ? "Подключение…"
        : "Сеть настройки";
  $("current-ssid").textContent = n.ssid || "Не настроена";
  $("rssi").textContent = n.connected ? `${n.rssi} dBm` : "—";
  const address = n.connected ? n.ip : n.apActive ? n.apIp : "";
  $("device-address").textContent = address || "—";
  if (/^\d{1,3}(\.\d{1,3}){3}$/.test(address))
    $("device-address").href = `http://${address}/`;
  else $("device-address").removeAttribute("href");
  $("setup-info").hidden = !n.apActive;
  $("setup-info").textContent = n.connected
    ? `Подключено к ${n.ssid}. Переключите телефон в эту сеть и откройте http://${n.ip}. Сеть настройки отключится через минуту после подключения.`
    : `Сеть настройки: ${n.apSsid}. Адрес: http://192.168.4.1 · Пароль точки доступа: wifi-led-controller. Интернет для настройки не нужен.`;
  if (!wifiEdited && !$("ssid").value && n.ssid) $("ssid").value = n.ssid;
  // Open setup on entry only. Polling must respect a user's collapsed form and
  // must not keep rewriting ancestors of the inputs after keyboard dismissal.
  const wasSetup = previous?.network.apActive && !previous?.network.connected;
  if (n.apActive && !n.connected && !wasSetup && !wifiEdited) setWifiDetails(true);
  renderWifiFeedback(next, previous);
  controls();
}

async function mutate(path, body) {
  if (busy || !online) return false;
  busy = true;
  revision++;
  controls();
  notice();
  try {
    render(await api(path, body));
    return true;
  } catch (error) {
    notice(
      error.name === "AbortError"
        ? "Контроллер не ответил. Проверьте подключение к его сети."
        : error.message,
    );
    return false;
  } finally {
    busy = false;
    controls();
  }
}

async function poll() {
  if (polling || busy || document.hidden) return;
  polling = true;
  const startedRevision = revision;
  try {
    const next = await api("/api/state");
    if (!busy && startedRevision === revision) render(next);
  } catch (_) {
    if (!busy && startedRevision === revision) {
      online = false;
      $("connection").textContent = "Нет связи";
      $("connection").className = "badge offline";
      $("signal").hidden = true;
      $("light-detail").textContent =
        "Состояние может быть неактуально. Переподключаемся…";
      if (
        wifiRequest &&
        wifiOutcome !== "success" &&
        wifiOutcome !== "failed"
      ) {
        wifiFeedback(
          "pending",
          "Связь с контроллером прервалась",
          `При смене Wi-Fi это возможно. Подключитесь к сети настройки ${state?.network.apSsid || "PhysTechLab-…"} и откройте http://192.168.4.1, чтобы узнать результат. Не отправляйте пароль повторно, пока подключение проверяется.`,
        );
      }
      controls();
    }
  } finally {
    polling = false;
  }
}

settingsFields.forEach((id) =>
  $(id).addEventListener("input", () => {
    dirty = true;
    describeSettings();
    controls();
  }),
);
document.querySelectorAll("[data-color]").forEach((button) =>
  button.addEventListener("click", () => {
    $("color").value = button.dataset.color;
    dirty = true;
    describeSettings();
  }),
);
$("calibrate").addEventListener("click", () => {
  if (!state) return;
  $("threshold").value = Math.min(1023, state.sensors.ambient + 10);
  dirty = true;
  describeSettings();
});
$("settings-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const body = {
    color: $("color").value,
    brightness: Number($("brightness").value),
    fadeInMs: Number($("fade-in").value),
    fadeOutMs: Number($("fade-out").value),
    holdSeconds: Number($("hold").value),
    useLightSensor: $("use-light").checked,
    darkThreshold: Number($("threshold").value),
  };
  if (await mutate("/api/settings", body)) {
    dirty = false;
    render(state);
  }
});
$("power").addEventListener("click", () => {
  if (state) mutate("/api/power", { on: !state.on });
});
document.querySelectorAll("input[name=mode]").forEach((input) =>
  input.addEventListener("change", async () => {
    if (!(await mutate("/api/settings", { mode: input.value })) && state)
      render(state);
  }),
);
$("show-password").addEventListener("change", () => {
  $("password").type = $("show-password").checked ? "text" : "password";
});
for (const id of ["ssid", "password"]) {
  $(id).addEventListener("input", () => { wifiEdited = true; });
  $(id).addEventListener("focus", () => { wifiEdited = true; });
}
$("networks").addEventListener("change", () => {
  if ($("networks").value) {
    $("ssid").value = $("networks").value;
    wifiEdited = true;
  }
});
// iOS captive-portal browsers can retain input focus on a non-focusable tap.
// Blur inside the user gesture, without cancelling the next control's action.
document.addEventListener("pointerdown", (event) => {
  const active = document.activeElement;
  if (!active?.matches('input[type="text"], input[type="password"], input[type="number"]')) return;
  const target = event.target;
  if (target === active || target.closest("label")?.control === active) return;
  active.blur();
}, { passive: true });
$("ssid").addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.isComposing) {
    event.preventDefault();
    $("password").focus();
  }
});
$("password").addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.isComposing) {
    event.preventDefault();
    event.target.blur();
  }
});
// Native keyboard actions must never submit network credentials implicitly.
$("wifi-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if ($("wifi-form").contains(document.activeElement)) document.activeElement.blur();
});
$("connect").addEventListener("click", async (event) => {
  event.preventDefault();
  if (busy || !online || state?.network.testing) return;
  const ssid = $("ssid").value,
    password = $("password").value;
  const bytes = (value) => new TextEncoder().encode(value).length;
  if (bytes(ssid) < 1 || bytes(ssid) > 32) {
    notice("Имя сети должно занимать от 1 до 32 байт.");
    return;
  }
  if (
    password &&
    !(
      (bytes(password) >= 8 && bytes(password) <= 63) ||
      /^[a-fA-F0-9]{64}$/.test(password)
    )
  ) {
    notice("Пароль: 8–63 байта или 64 шестнадцатеричных символа.");
    return;
  }
  wifiEdited = true;
  wifiRequest = { ssid };
  wifiOutcome = "";
  wifiFeedback(
    "pending",
    "Отправляем настройки Wi-Fi…",
    `Подключаем контроллер к «${ssid}».`,
  );
  focusWifiFeedback();
  if (await mutate("/api/wifi", { ssid, password })) {
    $("password").value = "";
    $("show-password").checked = false;
    $("password").type = "password";
  } else {
    wifiFeedback(
      "error",
      "Не удалось получить подтверждение",
      "Связь могла прерваться при смене сети. Дождитесь обновления статуса или снова подключитесь к точке доступа контроллера. " +
        $("notice").textContent,
    );
    focusWifiFeedback();
  }
});
$("scan").addEventListener("click", async () => {
  if (busy) return;
  busy = true;
  controls();
  $("scan-status").textContent = "Ищем сети…";
  notice();
  try {
    await api("/api/wifi/scan", {});
    let result;
    for (let attempt = 0; attempt < 20; ++attempt) {
      await new Promise((resolve) => setTimeout(resolve, 700));
      result = await api("/api/wifi/scan");
      if (!result.scanning) break;
    }
    if (result.scanning)
      throw new Error(
        "Поиск занимает слишком много времени. Повторите попытку.",
      );
    $("networks").replaceChildren(
      new Option("Выберите сеть или введите ниже", ""),
    );
    result.networks
      .sort((a, b) => b.rssi - a.rssi)
      .forEach((n) =>
        $("networks").add(
          new Option(
            `${n.ssid} · ${n.rssi} dBm${n.secure ? " · защищена" : " · открытая"}`,
            n.ssid,
          ),
        ),
      );
    $("scan-status").textContent = result.networks.length
      ? `Найдено сетей: ${result.networks.length}`
      : "Сети не найдены. Можно ввести SSID вручную.";
  } catch (error) {
    $("scan-status").textContent = "Поиск не завершён";
    notice(error.message);
  } finally {
    busy = false;
    controls();
  }
});
function uploadFirmware(file, password, md5) {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    request.open("POST", "/api/ota");
    request.timeout = 180000;
    request.setRequestHeader("X-OTA-Password", password);
    request.setRequestHeader("X-Firmware-Size", String(file.size));
    request.setRequestHeader("X-Firmware-MD5", md5);
    request.upload.onprogress = (event) => {
      if (!event.lengthComputable) return;
      const percent = Math.round(event.loaded / event.total * 100);
      $("firmware-progress").value = percent;
      $("firmware-result").textContent = percent === 100
        ? "Файл передан. Проверяем прошивку…"
        : `Загружаем прошивку: ${percent} %`;
    };
    request.onload = () => {
      let response;
      try { response = JSON.parse(request.responseText); } catch (_) {}
      if (request.status === 202 && response?.rebooting) resolve();
      else reject(new Error(response?.error || "Контроллер отклонил прошивку."));
    };
    const uncertain = () => {
      $("firmware-reconnect").hidden = false;
      reject(new Error("Связь прервалась: результат обновления неизвестен. Подождите минуту, затем проверьте контроллер и версию. Файл автоматически повторно не отправляется."));
    };
    request.onerror = uncertain;
    request.ontimeout = uncertain;
    request.onabort = uncertain;
    const body = new FormData();
    body.append("firmware", file, file.name);
    request.send(body);
  });
}

$("firmware-reconnect").addEventListener("click", () => location.reload());
$("firmware-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  if (busy || !online || dirty || state?.ota?.busy || state?.network.connecting || state?.network.testing) return;
  const file = $("firmware-file").files[0];
  if (!file || !/\.bin$/i.test(file.name) || file.size < 16 || file.size > (state?.ota?.maxSize || 0)) {
    $("firmware-result").textContent = "Выберите файл .bin подходящего размера для этого контроллера.";
    return;
  }
  const password = $("firmware-password").value;
  if (!password) return;
  busy = firmwareBusy = true;
  revision++;
  controls();
  $("firmware-reconnect").hidden = true;
  $("firmware-progress").hidden = false;
  $("firmware-progress").value = 0;
  $("firmware-result").textContent = "Проверяем файл…";
  try {
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes[0] !== 0xE9) throw new Error("Это не образ прошивки ESP8266. Выберите firmware.bin из сборки WiFiLedController.");
    await uploadFirmware(file, password, window.firmwareMd5(bytes));
    $("firmware-result").textContent = "Прошивка проверена. Контроллер перезапускается…";
    // Never repeat the POST, including when reboot breaks the network connection.
    await new Promise((resolve) => setTimeout(resolve, 5000));
    for (let attempt = 0; attempt < 15; ++attempt) {
      try {
        const result = await api("/api/ota");
        if (!result.busy && result.version) {
          firmwareBusy = false;
          location.reload();
          return;
        }
      } catch (_) {}
      await new Promise((resolve) => setTimeout(resolve, 2000));
    }
    $("firmware-result").textContent = "Прошивка принята. Подключитесь к сети контроллера и проверьте его доступность.";
    $("firmware-reconnect").hidden = false;
  } catch (error) {
    $("firmware-result").textContent = error.message;
  } finally {
    $("firmware-password").value = "";
    busy = firmwareBusy = false;
    controls();
  }
});

function chooseSystemAction(action) {
  systemChoice = action;
  $("system-confirm").hidden = false;
  $("system-confirm-text").textContent = action === "reset"
    ? "Удалить все настройки, включая имя и пароль Wi-Fi? Отменить сброс нельзя. Контроллер перезапустится в режиме настройки."
    : "Перезагрузить контроллер? Применённые настройки сохранятся.";
  if (dirty) $("system-confirm-text").textContent += " Неприменённые изменения в форме будут потеряны.";
  $("system-confirm-action").textContent = action === "reset" ? "Удалить всё и перезапустить" : "Перезагрузить сейчас";
  $("system-cancel").focus({ preventScroll: true });
  $("system-confirm").scrollIntoView({ block: "center", behavior: "smooth" });
}
$("system-reboot").addEventListener("click", () => chooseSystemAction("reboot"));
$("system-reset").addEventListener("click", () => chooseSystemAction("reset"));
$("system-cancel").addEventListener("click", () => {
  const action = systemChoice;
  systemChoice = "";
  $("system-confirm").hidden = true;
  $("system-" + action).focus({ preventScroll: true });
});
$("system-reconnect").addEventListener("click", async () => {
  try {
    const next = await api("/api/state");
    busy = false;
    $("system-reconnect").hidden = true;
    $("system-result").textContent = "Контроллер на связи.";
    render(next);
  } catch (_) {
    $("system-result").textContent = "Нет связи. Подключитесь к сети контроллера. После сброса: PhysTechLab-… → http://192.168.4.1, пароль wifi-led-controller.";
  }
});
$("system-confirm-action").addEventListener("click", async () => {
  if (!systemChoice || busy || !online) return;
  const action = systemChoice;
  systemChoice = "";
  busy = true;
  revision++;
  controls();
  $("system-confirm").hidden = true;
  $("system-result").textContent = action === "reset" ? "Очищаем настройки…" : "Перезагружаем…";
  $("system-result").focus({ preventScroll: true });
  try {
    const result = await api(`/api/system/${action}`, { confirm: true });
    if (!result.rebooting) throw new Error("Контроллер не подтвердил перезагрузку.");
    dirty = false;
    online = false;
    $("connection").textContent = "Перезапуск…";
    $("connection").className = "badge offline";
    $("signal").hidden = true;
    $("system-result").textContent = action === "reset"
      ? `Настройки удалены. Подключитесь к сети ${state?.network.apSsid || "PhysTechLab-…"} (пароль wifi-led-controller) и откройте http://192.168.4.1.`
      : "Контроллер перезапускается. Восстанавливаем связь…";
    if (action === "reset") {
      $("system-reconnect").hidden = false;
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 5000));
    for (let attempt = 0; attempt < 15; ++attempt) {
      try {
        const next = await api("/api/state");
        busy = false;
        render(next);
        $("system-result").textContent = "Контроллер перезагружен. Настройки сохранены.";
        return;
      } catch (_) {}
      await new Promise((resolve) => setTimeout(resolve, 2000));
    }
    $("system-result").textContent = "Перезагрузка принята. Связь пока не восстановлена. Проверьте подключение к сети контроллера.";
  } catch (error) {
    $("system-result").textContent = `${error.message} Проверьте связь и состояние устройства перед повторной попыткой.`;
    // A dropped response may mean the device already restarted: never repeat POST.
  }
  $("system-reconnect").hidden = false;
});

window.addEventListener("beforeunload", (event) => {
  if (dirty || firmwareBusy) {
    event.preventDefault();
    event.returnValue = "";
  }
});
document.addEventListener("visibilitychange", poll);
describeSettings();
poll();
setInterval(poll, 2000);

// Installation progressively enhances the same local UI. HTTP on an ESP's LAN
// address is not a secure context, so it cannot register a service worker.
let installPrompt = null;
const standalone = matchMedia("(display-mode: standalone)");
function appStatus() {
  const installed = standalone.matches || navigator.standalone === true;
  $("install-app").hidden = installed;
  $("app-status").textContent = installed
    ? "Открыто как приложение"
    : "Быстрый доступ с главного экрана телефона или рабочего стола.";
}
const isIos =
  /iPad|iPhone|iPod/.test(navigator.userAgent) ||
  (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
$("app-instructions").textContent = isIos
  ? "В Safari откройте меню «Поделиться» → «На экран Домой». Если доступно, включите «Открывать как веб-приложение»."
  : "Откройте меню браузера → «Добавить на главный экран» или «Установить приложение». На ПК этот пункт может находиться в меню «Приложения». Доступность отдельного окна зависит от браузера.";
if (!window.isSecureContext)
  $("app-instructions").textContent +=
    " При прямом HTTP-подключении доступен ярлык; офлайн-кэш и автоматическое предложение установки требуют HTTPS.";
window.addEventListener("beforeinstallprompt", (event) => {
  event.preventDefault();
  installPrompt = event;
  $("install-app").textContent = "Установить приложение";
});
$("install-app").addEventListener("click", async () => {
  if (!installPrompt) {
    $("app-help").open = true;
    return;
  }
  const prompt = installPrompt;
  installPrompt = null;
  try {
    await prompt.prompt();
    await prompt.userChoice;
  } finally {
    $("install-app").textContent = "Добавить на экран";
  }
});
window.addEventListener("appinstalled", () => {
  installPrompt = null;
  $("install-app").hidden = true;
  $("app-status").textContent = "Приложение установлено";
});
standalone.addEventListener("change", appStatus);
appStatus();
if ("serviceWorker" in navigator && window.isSecureContext) {
  navigator.serviceWorker.register("/sw.js").catch(() => {
    $("app-status").textContent =
      "Интерфейс доступен. Офлайн-кэш не удалось подготовить.";
  });
}
