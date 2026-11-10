// The page's saves: the game's user folder lives in IndexedDB
// (Emscripten's IDBFS, mounted at /papercut). Before the game starts the page
// can take it out as a .zip (the same layout as a desktop build's user
// folder: <JAP|USA|EUR>/Card A/*.gci, settings.txt, ...) and put one back, or
// single .gci card files. While the game runs it owns the folder (it writes
// it back every few seconds), so importing is only offered before.
"use strict";

const papercutSaves = (() => {
  const DB = "/papercut"; // IDBFS: the mount point is the database's name
  const DB_VERSION = 21; // as Emscripten's libidbfs.js
  const STORE = "FILE_DATA";
  const USER = "/papercut/user";
  const DIR_MODE = 0o40777;
  const FILE_MODE = 0o100666;

  function open() {
    return new Promise((resolve, reject) => {
      const r = indexedDB.open(DB, DB_VERSION);
      r.onupgradeneeded = () => {
        // as IDBFS makes it (the game opens it the same way)
        const store = r.result.objectStoreNames.contains(STORE)
          ? r.transaction.objectStore(STORE)
          : r.result.createObjectStore(STORE);
        if (!store.indexNames.contains("timestamp")) store.createIndex("timestamp", "timestamp", { unique: false });
      };
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
  }

  // every file under the user folder: [{path (relative), contents}]
  async function readAll() {
    const db = await open();
    try {
      return await new Promise((resolve, reject) => {
        const out = [];
        const t = db.transaction(STORE, "readonly");
        const c = t.objectStore(STORE).openCursor();
        c.onsuccess = () => {
          const cur = c.result;
          if (!cur) return resolve(out);
          const key = String(cur.key);
          const v = cur.value;
          if (key.startsWith(USER + "/") && v && v.contents) {
            out.push({ path: key.slice(USER.length + 1), contents: new Uint8Array(v.contents), time: v.timestamp });
          }
          cur.continue();
        };
        c.onerror = () => reject(c.error);
      });
    } finally {
      db.close();
    }
  }

  // files written into the user folder (with their folders), replacing any of the same name
  async function writeAll(files) {
    const db = await open();
    try {
      await new Promise((resolve, reject) => {
        const t = db.transaction(STORE, "readwrite");
        const store = t.objectStore(STORE);
        const now = new Date();
        const dirs = new Set([USER]);
        for (const f of files) {
          const parts = f.path.split("/");
          for (let i = 1; i < parts.length; i++) dirs.add(USER + "/" + parts.slice(0, i).join("/"));
        }
        for (const d of dirs) store.put({ timestamp: now, mode: DIR_MODE }, d);
        for (const f of files) store.put({ timestamp: now, mode: FILE_MODE, contents: f.contents }, USER + "/" + f.path);
        t.oncomplete = resolve;
        t.onerror = () => reject(t.error);
        t.onabort = () => reject(t.error);
      });
    } finally {
      db.close();
    }
  }

  // --- zip (stored entries out; stored or deflated ones in) ---
  const CRC = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crc32(a) {
    let c = 0xffffffff;
    for (let i = 0; i < a.length; i++) c = CRC[(c ^ a[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }
  function dosTime(d) {
    d = d instanceof Date && !isNaN(d) ? d : new Date();
    return {
      time: (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1),
      date: (Math.max(d.getFullYear() - 1980, 0) << 9) | ((d.getMonth() + 1) << 5) | d.getDate(),
    };
  }

  function zip(files) {
    const enc = new TextEncoder();
    const parts = [];
    const central = [];
    let at = 0;
    for (const f of files) {
      const name = enc.encode(f.path);
      const crc = crc32(f.contents);
      const { time, date } = dosTime(f.time);
      const h = new DataView(new ArrayBuffer(30));
      h.setUint32(0, 0x04034b50, true);
      h.setUint16(4, 20, true);
      h.setUint16(6, 0x0800, true); // names in UTF-8
      h.setUint16(10, time, true);
      h.setUint16(12, date, true);
      h.setUint32(14, crc, true);
      h.setUint32(18, f.contents.length, true);
      h.setUint32(22, f.contents.length, true);
      h.setUint16(26, name.length, true);
      parts.push(h.buffer, name, f.contents);
      const c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true);
      c.setUint16(4, 20, true);
      c.setUint16(6, 20, true);
      c.setUint16(8, 0x0800, true);
      c.setUint16(12, time, true);
      c.setUint16(14, date, true);
      c.setUint32(16, crc, true);
      c.setUint32(20, f.contents.length, true);
      c.setUint32(24, f.contents.length, true);
      c.setUint16(28, name.length, true);
      c.setUint32(42, at, true);
      central.push(c.buffer, name);
      at += 30 + name.length + f.contents.length;
    }
    const size = central.reduce((n, p) => n + p.byteLength, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, files.length, true);
    end.setUint16(10, files.length, true);
    end.setUint32(12, size, true);
    end.setUint32(16, at, true);
    return new Blob([...parts, ...central, end.buffer], { type: "application/zip" });
  }

  async function unzip(buf) {
    const v = new DataView(buf);
    const bytes = new Uint8Array(buf);
    let e = -1;
    for (let i = buf.byteLength - 22; i >= Math.max(0, buf.byteLength - 22 - 65535); i--) {
      if (v.getUint32(i, true) === 0x06054b50) {
        e = i;
        break;
      }
    }
    if (e < 0) throw new Error("That isn't a .zip file.");
    const count = v.getUint16(e + 10, true);
    let p = v.getUint32(e + 16, true);
    const dec = new TextDecoder();
    const out = [];
    for (let n = 0; n < count; n++) {
      if (v.getUint32(p, true) !== 0x02014b50) throw new Error("The .zip file is damaged.");
      const method = v.getUint16(p + 10, true);
      const packed = v.getUint32(p + 20, true);
      const nameLen = v.getUint16(p + 28, true);
      const extraLen = v.getUint16(p + 30, true);
      const commentLen = v.getUint16(p + 32, true);
      const local = v.getUint32(p + 42, true);
      const name = dec.decode(bytes.subarray(p + 46, p + 46 + nameLen)).replace(/\\/g, "/");
      p += 46 + nameLen + extraLen + commentLen;
      if (name.endsWith("/")) continue;
      const start = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true);
      const data = bytes.subarray(start, start + packed);
      let contents;
      if (method === 0) contents = data.slice();
      else if (method === 8) {
        const s = new Blob([data]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
        contents = new Uint8Array(await new Response(s).arrayBuffer());
      } else throw new Error(`${name}: packed in a way this page can't read (method ${method}).`);
      out.push({ path: name, contents });
    }
    return out;
  }

  // a zip of a user folder, wherever it was in the zip: what's under the folder that holds the
  // card folders (<JAP|USA|EUR>/Card A/) or settings.txt; safe names only
  function fromUserFolder(entries) {
    let prefix = null;
    for (const e of entries) {
      const i = e.path.search(/(^|\/)(JAP|USA|EUR)\/Card A\//);
      if (i >= 0) {
        const pre = e.path[i] === "/" ? e.path.slice(0, i + 1) : "";
        if (prefix === null || pre.length < prefix.length) prefix = pre;
      }
    }
    if (prefix === null) {
      for (const e of entries) {
        if (/(^|\/)settings\.txt$/.test(e.path)) {
          const pre = e.path.slice(0, e.path.length - "settings.txt".length);
          if (prefix === null || pre.length < prefix.length) prefix = pre;
        }
      }
    }
    if (prefix === null) prefix = "";
    return entries
      .filter((e) => e.path.startsWith(prefix))
      .map((e) => ({ path: e.path.slice(prefix.length), contents: e.contents }))
      .filter((e) => e.path && !e.path.split("/").some((s) => s === "" || s === "." || s === ".."));
  }

  // a single card file: where the game's GCI folder card keeps it (<maker>-<game>-<name>.gci)
  function gciPath(contents) {
    if (contents.length < 0x40 + 0x2000) return null;
    const text = (o, n) => String.fromCharCode(...contents.subarray(o, o + n)).replace(/\0.*$/s, "");
    const game = text(0, 4);
    const maker = text(4, 2);
    const name = text(8, 32);
    if (!/^[A-Z0-9]{4}$/.test(game) || !/^[A-Z0-9]{2}$/.test(maker) || !/^[\x20-\x7e]+$/.test(name)) return null;
    const region = game[3] === "J" ? "JAP" : game[3] === "P" ? "EUR" : "USA";
    return `${region}/Card A/${maker}-${game}-${name.replace(/[\/\\:*?"<>|]/g, "_")}.gci`;
  }

  return {
    // a .zip of every file in the browser's user folder (null: nothing saved yet)
    async exportZip() {
      const files = await readAll();
      return files.length ? zip(files) : null;
    },
    // .zip files (a user folder) and .gci card files, put into the browser's user folder;
    // asks before replacing; returns what was written (0: nothing, or the player said no)
    async importFiles(list, confirmReplace) {
      const files = [];
      for (const f of list) {
        const buf = await f.arrayBuffer();
        if (/\.zip$/i.test(f.name)) files.push(...fromUserFolder(await unzip(buf)));
        else {
          const path = gciPath(new Uint8Array(buf));
          if (!path) throw new Error(`${f.name} isn't a GameCube card file (.gci) or a .zip of saves.`);
          files.push({ path, contents: new Uint8Array(buf) });
        }
      }
      if (!files.length) throw new Error("There was nothing in it to bring in.");
      const have = new Set((await readAll()).map((f) => f.path));
      const replaced = files.filter((f) => have.has(f.path)).map((f) => f.path);
      if (replaced.length && !confirmReplace(replaced)) return 0;
      await writeAll(files);
      return files.length;
    },
  };
})();

// The low-end preset: lower resolution and effects for Chromebooks and phones. The
// page sets these in the game's settings.txt once, when it is turned on (the values before are
// kept and put back when it is turned off); the game's own settings page changes them as ever.
const papercutLowEnd = (() => {
  const KEYS = {
    resolution: "1", // 640x480 inside (0: the window's size)
    framerate: "60", // no in-between frames
    msaa: "0",
    supersample: "0",
    post_filter: "0",
    aniso: "0",
    texture_upscale: "0",
    hdr: "0",
    paper_shadows: "0",
    paper_edges: "0",
    paper_physics: "0",
    rewind: "0", // (in the browser a capture compares all of the game's memory)
  };
  const PREF = "papercut-low-end"; // "1" / "0"
  const PENDING = "papercut-low-end-pending"; // set at the next start
  const BEFORE = "papercut-low-end-before"; // the values it replaced (JSON)
  const store = {
    get: (k) => {
      try {
        return localStorage.getItem(k);
      } catch {
        return null;
      }
    },
    set: (k, v) => {
      try {
        if (v === null) localStorage.removeItem(k);
        else localStorage.setItem(k, v);
      } catch {}
    },
  };
  // a guess at a slower device: a phone or tablet, a Chromebook, few cores or little memory
  function weak() {
    const ua = navigator.userAgent;
    return /Android|iPhone|iPad|Mobile|CrOS/.test(ua) || matchMedia("(pointer: coarse)").matches ||
      (navigator.deviceMemory ?? 8) <= 4 || (navigator.hardwareConcurrency ?? 8) <= 4;
  }
  function parse(text) {
    const out = new Map();
    for (const line of text.split("\n")) {
      const eq = line.indexOf("=");
      if (eq > 0) out.set(line.slice(0, eq), line.slice(eq + 1).replace(/\r$/, ""));
    }
    return out;
  }
  return {
    on() {
      const v = store.get(PREF);
      if (v === null) {
        // the first visit: on by itself for a slower device
        const guess = weak();
        store.set(PREF, guess ? "1" : "0");
        if (guess) store.set(PENDING, "1");
        return guess;
      }
      return v === "1";
    },
    set(on) {
      if (on === this.on()) return;
      store.set(PREF, on ? "1" : "0");
      // a change made and taken back before a start does nothing
      store.set(PENDING, store.get(PENDING) ? null : "1");
    },
    // into the game's settings (the FS after IndexedDB is read, before the game starts);
    // true: the file was made here (the game's first start)
    applyTo(FS, path) {
      if (!store.get(PENDING)) return false;
      store.set(PENDING, null);
      let text = "";
      const made = !FS.analyzePath(path).exists;
      if (made && !this.on()) {
        store.set(BEFORE, null);
        return false;
      }
      try {
        text = FS.readFile(path, { encoding: "utf8" });
      } catch {}
      const s = parse(text);
      if (this.on()) {
        const before = {};
        for (const [k, v] of Object.entries(KEYS)) {
          before[k] = s.has(k) ? s.get(k) : null;
          s.set(k, v);
        }
        store.set(BEFORE, JSON.stringify(before));
      } else {
        let before = {};
        try {
          before = JSON.parse(store.get(BEFORE) ?? "{}");
        } catch {}
        for (const [k, v] of Object.entries(before)) {
          if (s.get(k) !== KEYS[k]) continue; // changed in the game since: left as the player set it
          if (v === null) s.delete(k);
          else s.set(k, v);
        }
        store.set(BEFORE, null);
      }
      FS.writeFile(path, [...s].map(([k, v]) => `${k}=${v}`).join("\n") + "\n");
      return made;
    },
  };
})();
