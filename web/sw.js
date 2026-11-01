"use strict";
const CACHE = "wifi-led-controller-shell-v4";
const ASSETS = [
  "/",
  "/app.css",
  "/app.js",
  "/theme.js",
  "/md5.js",
  "/manifest.webmanifest",
  "/icon.svg",
  "/icon-192.png",
  "/icon-512.png",
  "/icon-maskable.png",
  "/apple-touch-icon.png",
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches
      .open(CACHE)
      .then(async (cache) => {
        // The ESP serves one HTTP client at a time; avoid a parallel precache burst.
        for (const url of ASSETS) await cache.add(url);
      })
      .then(() => self.skipWaiting()),
  );
});
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) =>
        Promise.all(
          keys
            .filter(
              (key) => key.startsWith("wifi-led-controller-shell-") && key !== CACHE,
            )
            .map((key) => caches.delete(key)),
        ),
      )
      .then(() => self.clients.claim()),
  );
});
self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  // Device state and commands NEVER enter the cache, including Wi-Fi credentials.
  if (
    event.request.method !== "GET" ||
    url.origin !== self.location.origin ||
    url.pathname.startsWith("/api/")
  )
    return;
  if (!ASSETS.includes(url.pathname)) return;
  event.respondWith(
    (async () => {
      const cache = await caches.open(CACHE);
      try {
        const response = await fetch(event.request);
        if (response.ok)
          await cache.put(url.pathname, response.clone()).catch(() => {});
        return response;
      } catch (error) {
        const cached = await cache.match(url.pathname);
        if (cached) return cached;
        throw error;
      }
    })(),
  );
});
