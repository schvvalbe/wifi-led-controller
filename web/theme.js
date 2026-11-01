// Runs before the stylesheet to avoid flashing the wrong theme at startup.
(() => {
  const media = matchMedia("(prefers-color-scheme: dark)");
  let preference = "system";
  try {
    const stored = localStorage.getItem("wifi-led-controller-theme");
    if (["light", "dark", "system"].includes(stored)) preference = stored;
  } catch (_) {
    /* Storage can be unavailable in captive-portal web views. */
  }
  function apply() {
    const dark =
      preference === "dark" || (preference === "system" && media.matches);
    document.documentElement.dataset.theme = dark ? "dark" : "light";
    document.documentElement.style.colorScheme = dark ? "dark" : "light";
    document.querySelector('meta[name="theme-color"]').content = dark
      ? "#141a17"
      : "#f5f5f2";
  }
  window.wifiLedControllerTheme = {
    get preference() {
      return preference;
    },
    set(value) {
      if (!["light", "dark", "system"].includes(value)) return;
      preference = value;
      try {
        localStorage.setItem("wifi-led-controller-theme", value);
      } catch (_) {}
      apply();
    },
  };
  media.addEventListener("change", apply);
  apply();
})();
