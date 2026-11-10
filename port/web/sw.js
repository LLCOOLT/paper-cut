// Paper Cut's service worker: the page installs as an app and
// starts without a connection. Everything is fetched from the site first (the
// browser's own HTTP cache still answers what hasn't changed), and what came
// back is kept for the times the site can't be reached. The player's disc
// image is never fetched: the page reads it from their own files.
"use strict";

const CACHE = "papercut-site";
const SHELL = [
  "./",
  "index.html",
  "papercut.js",
  "papercut-saves.js",
  "papercut.css",
  "manifest.webmanifest",
  "papercut-256.png",
  "Fredoka-Bold.ttf",
  "Fredoka-Medium.ttf",
];

self.addEventListener("install", (e) => {
  e.waitUntil(
    caches
      .open(CACHE)
      .then((c) => Promise.all(SHELL.map((u) => c.add(u).catch(() => {}))))
      .then(() => self.skipWaiting()),
  );
});

self.addEventListener("activate", (e) => e.waitUntil(self.clients.claim()));

// the same file as kept already (same ETag or Last-Modified): not written again (the .wasm is large)
function sameAs(a, b) {
  if (!a || !b) return false;
  const tag = (r, h) => r.headers.get(h);
  if (tag(a, "etag") && tag(a, "etag") === tag(b, "etag")) return true;
  return !!tag(a, "last-modified") && tag(a, "last-modified") === tag(b, "last-modified") &&
    tag(a, "content-length") === tag(b, "content-length");
}

self.addEventListener("fetch", (e) => {
  const req = e.request;
  const url = new URL(req.url);
  if (req.method !== "GET" || url.origin !== location.origin || req.headers.has("range")) return;
  // one copy per file, whatever the query (?fast, ?settings=... are the page's own)
  const key = new Request(url.origin + url.pathname);
  e.respondWith(
    (async () => {
      const cache = await caches.open(CACHE);
      try {
        const res = await fetch(req);
        if (res.ok && res.type === "basic") {
          const kept = await cache.match(key);
          if (!sameAs(kept, res)) e.waitUntil(cache.put(key, res.clone()).catch(() => {}));
        }
        return res;
      } catch (err) {
        const kept = (await cache.match(key)) ?? (req.mode === "navigate" ? await cache.match("index.html") : null);
        if (kept) return kept;
        throw err;
      }
    })(),
  );
});
