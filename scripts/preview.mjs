// Static preview only. The automated browser tests supply a mock device API.
import http from "node:http";
import { readFile } from "node:fs/promises";
const files = {
  "/": ["index.html", "text/html"],
  "/app.css": ["app.css", "text/css"],
  "/app.js": ["app.js", "text/javascript"],
  "/theme.js": ["theme.js", "text/javascript"],
  "/md5.js": ["md5.js", "text/javascript"],
};
for (const filename of [
  "manifest.webmanifest",
  "sw.js",
  "icon.svg",
  "icon-192.png",
  "icon-512.png",
  "icon-maskable.png",
  "apple-touch-icon.png",
]) {
  const type = filename.endsWith(".png")
    ? "image/png"
    : filename.endsWith(".svg")
      ? "image/svg+xml"
      : filename.endsWith(".js")
        ? "text/javascript"
        : "application/manifest+json";
  files["/" + filename] = [filename, type];
}
http
  .createServer(async (req, res) => {
    const asset = files[new URL(req.url, "http://localhost").pathname];
    if (!asset) {
      res.writeHead(404);
      res.end();
      return;
    }
    try {
      const data = await readFile(
        new URL(`../web/${asset[0]}`, import.meta.url),
      );
      res.writeHead(200, { "Content-Type": `${asset[1]}; charset=utf-8` });
      res.end(data);
    } catch (_) {
      res.writeHead(500);
      res.end();
    }
  })
  .listen(8123, "127.0.0.1", () =>
    console.log("UI preview: http://127.0.0.1:8123"),
  );
