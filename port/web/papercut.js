// Paper Cut in the browser: checks for WebGPU, takes the
// player's disc image, and starts the game (ttyd-<us|jp|eu>.js) with it.
// The disc is read where it is (Blob slices, port/src/web/web_pc.c), never
// uploaded or copied whole; saves and settings live in IndexedDB, and come out
// and go back in as a .zip (papercut-saves.js). It installs as an app (PWA:
// manifest.webmanifest, sw.js), with a controller or the touch buttons.
"use strict";

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const VERSIONS = { G8ME01: "us", G8MJ01: "jp", G8MP01: "eu" };
const EXTENSIONS = [".iso", ".gcm", ".ciso", ".gcz", ".rvz", ".wia", ".wbfs", ".nfs", ".tgc"];

function show(id) {
  for (const s of ["picker", "unsupported", "failed"]) $(s).hidden = s !== id;
  $("options").hidden = id !== "picker";
  document.querySelector(".footnote").hidden = id === "failed";
}

function setStatus(text) {
  $("status").textContent = text;
}

async function checkWebGPU() {
  if (!navigator.gpu) return "This browser has no WebGPU.";
  try {
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
    if (!adapter) return "WebGPU is there, but no graphics adapter was offered (it may be turned off or blocklisted).";
    // (without immediate data, Firefox and Safari so far, Aurora passes each draw's offsets in a
    // uniform buffer instead; the shaders read storage buffers in the vertex stage)
    if ((adapter.limits.maxStorageBuffersPerShaderStage ?? 0) < 2) {
      return "This browser's WebGPU is too limited for the game. Update the browser.";
    }
  } catch (e) {
    return `WebGPU failed: ${e.message}`;
  }
  return null;
}

// The game ID (e.g. G8ME01) of a disc image: where each format keeps the disc's header.
async function discId(file) {
  const head = new Uint8Array(await file.slice(0, 0x100).arrayBuffer());
  const text = (a, o, n) => String.fromCharCode(...a.subarray(o, o + n));
  const magic = text(head, 0, 4);
  let at = 0;
  if (magic === "RVZ\x01" || magic === "WIA\x01") at = 0x58;
  else if (magic === "CISO") at = 0x8000;
  else if (magic === "WBFS") at = 0x200;
  else if (head[0] === 0xae && head[1] === 0x0f && head[2] === 0x38 && head[3] === 0xa2) {
    at = new DataView(head.buffer).getUint32(8); // TGC: where the GCM starts
  }
  let s;
  if (at + 6 <= head.length) s = text(head, at, 6);
  else s = text(new Uint8Array(await file.slice(at, at + 6).arrayBuffer()), 0, 6);
  return /^[A-Z0-9]{6}$/.test(s) ? s : null;
}

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error(`This site has no ${src}.`));
    document.head.appendChild(s);
  });
}

// --- the disc chosen last time (Chrome and Edge keep a file handle in IndexedDB) ---
const handleStore = {
  db: null,
  async open() {
    if (this.db) return this.db;
    this.db = await new Promise((resolve, reject) => {
      const r = indexedDB.open("papercut-page", 1);
      r.onupgradeneeded = () => r.result.createObjectStore("handles");
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
    return this.db;
  },
  async get() {
    try {
      const db = await this.open();
      return await new Promise((resolve) => {
        const r = db.transaction("handles").objectStore("handles").get("disc");
        r.onsuccess = () => resolve(r.result ?? null);
        r.onerror = () => resolve(null);
      });
    } catch {
      return null;
    }
  },
  async set(handle) {
    try {
      const db = await this.open();
      db.transaction("handles", "readwrite").objectStore("handles").put(handle, "disc");
    } catch {}
  },
};

// --- the game ---
let started = false;
let saving = false;
let resaveWanted = false;

function fail(text) {
  console.error(text);
  document.body.classList.remove("playing", "loading");
  $("canvas").hidden = true;
  $("loading").hidden = true;
  $("failed-why").textContent = text;
  show("failed");
}

async function start(file) {
  if (started) return;
  if (!EXTENSIONS.some((e) => file.name.toLowerCase().endsWith(e))) {
    setStatus(`${file.name} doesn't look like a GameCube disc image.`);
    return;
  }
  const id = await discId(file);
  if (id && !VERSIONS[id]) {
    setStatus(`${file.name} (${id}) isn't Paper Mario: The Thousand-Year Door.`);
    return;
  }
  const version = VERSIONS[id] ?? params.get("version") ?? "us";
  started = true;
  setStatus("");
  document.body.classList.add("playing", "loading");
  $("loading").hidden = false;
  $("loading-text").textContent = "Starting…";
  try {
    await loadScript(`ttyd-${version}.js`);
  } catch (e) {
    started = false;
    document.body.classList.remove("playing", "loading");
    $("loading").hidden = true;
    setStatus(e.message);
    return;
  }

  const canvas = $("canvas");
  let frames = 0;
  let lastFatal = null;
  const Module = {
    canvas,
    papercutDisc: file,
    papercutFast: params.has("fast"),
    papercutFrames: 0, // frames handed to the browser (tests read it)
    print: (t) => console.log(t),
    printErr: (t) => {
      if (/FATAL|Assertion|panic/.test(t)) lastFatal = t;
      console.warn(t);
    },
    onAbort: (what) =>
      fail(`${lastFatal ?? what ?? "The program stopped."}\n\nThe browser's console (F12) has the details.`),
    // the first frames shown: the stage gives way to the game
    papercutOnFrame() {
      Module.papercutFrames++;
      if (++frames === 3) {
        document.body.classList.remove("loading");
        $("loading").hidden = true;
      }
    },
    preRun: [
      (m) => {
        // Aurora's user folder (saves, settings) and its caches, kept in IndexedDB
        m.FS.mkdir("/papercut");
        m.FS.mount(m.IDBFS, {}, "/papercut");
        m.addRunDependency("papercut-idb");
        m.FS.syncfs(true, (err) => {
          if (err) console.warn("saves could not be read", err);
          for (const d of ["/papercut/user", "/papercut/cache"]) {
            try {
              m.FS.mkdir(d);
            } catch {}
          }
          // tests: ?settings=skip_intro=1,fullscreen=0 starts a fresh user folder with these settings
          const settings = "/papercut/user/settings.txt";
          let fresh = false;
          if (params.has("settings") && !m.FS.analyzePath(settings).exists) {
            m.FS.writeFile(settings, params.get("settings").split(",").join("\n") + "\n");
            fresh = true;
          }
          // the low-end preset, when it was turned on or off since the last start
          try {
            if (papercutLowEnd.applyTo(m.FS, settings)) fresh = true;
          } catch (e) {
            console.warn("the low-end preset could not be set", e);
          }
          // (a settings.txt the page made is still the game's first start: What's new stays quiet)
          if (fresh) m.ENV.TTYD_WEB_FRESH = "1";
          m.removeRunDependency("papercut-idb");
        });
        m.ENV.TTYD_USER_DIR = "/papercut/user";
        m.ENV.TTYD_CACHE_DIR = "/papercut/cache";
        if (params.has("fast")) m.ENV.TTYD_FAST = "1";
        // tests: ?TTYD_...=... become the program's environment
        for (const [k, v] of params) if (k.startsWith("TTYD_") || k.startsWith("AURORA_")) m.ENV[k] = v;
      },
    ],
    // the user folder back into IndexedDB (web_pc.c every few seconds; the page when hidden or closed)
    papercutSave() {
      if (saving) {
        resaveWanted = true;
        return;
      }
      saving = true;
      Module.FS.syncfs(false, (err) => {
        saving = false;
        if (err) console.warn("saves could not be written", err);
        if (resaveWanted) {
          resaveWanted = false;
          Module.papercutSave();
        }
      });
    },
  };
  show(null);
  canvas.hidden = false;
  canvas.focus();
  let m;
  try {
    m = await createPaperCut(Module);
  } catch (e) {
    fail(String(e?.message ?? e));
    return;
  }
  window.papercut = m; // (the same object as Module, now with the runtime's methods)
  fullscreenOnTouch(canvas);
  addEventListener("pagehide", () => Module.papercutSave());
  document.addEventListener("visibilitychange", () => document.hidden && Module.papercutSave());
  addEventListener("error", (e) => fail(String(e.error?.stack ?? e.message)));
  addEventListener("unhandledrejection", (e) => fail(String(e.reason?.stack ?? e.reason)));
  // keys go to the game, not the page (arrows and space would scroll it)
  addEventListener("keydown", (e) => {
    if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", " ", "Tab"].includes(e.key)) e.preventDefault();
  });
  try {
    m.callMain([`web:${file.name}`]);
  } catch (e) {
    if (e !== "unwind" && e?.name !== "ExitStatus") fail(String(e?.stack ?? e));
  }
}

// A phone or tablet in a browser tab (not the installed app, already full screen): the first
// touch on the game makes it full screen, turned sideways where the browser allows it.
function fullscreenOnTouch(canvas) {
  if (matchMedia("(display-mode: fullscreen), (display-mode: standalone)").matches) return;
  if (!document.documentElement.requestFullscreen) return;
  const once = (e) => {
    if (e.pointerType !== "touch") return;
    canvas.removeEventListener("pointerdown", once);
    if (document.fullscreenElement) return;
    document.documentElement
      .requestFullscreen({ navigationUI: "hide" })
      .then(() => screen.orientation?.lock?.("landscape"))
      .catch(() => {});
  };
  canvas.addEventListener("pointerdown", once);
}

// --- the app: installing, working offline, keeping its storage ---
let installPrompt = null;
function setUpApp() {
  if ("serviceWorker" in navigator && isSecureContext) {
    navigator.serviceWorker.register("sw.js").catch((e) => console.warn("no offline copy", e));
  }
  // saves are in the page's storage: asked to be kept when the browser runs short of space
  navigator.storage?.persist?.().catch(() => {});
  addEventListener("beforeinstallprompt", (e) => {
    e.preventDefault();
    installPrompt = e;
    $("install-row").hidden = false;
  });
  addEventListener("appinstalled", () => ($("install-row").hidden = true));
  $("install").addEventListener("click", async () => {
    if (!installPrompt) return;
    installPrompt.prompt();
    await installPrompt.userChoice.catch(() => null);
    installPrompt = null;
    $("install-row").hidden = true;
  });
  // Safari on iPhone and iPad shows no install button for a page to offer
  const ios = /iPhone|iPad/.test(navigator.userAgent) || (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
  if (ios && !navigator.standalone) $("install-ios").hidden = false;
  // a controller: named on the page once the browser shows it (after a button press)
  addEventListener("gamepadconnected", (e) => {
    if (!started) setStatus(`Controller ready: ${e.gamepad.id.replace(/\s*\(.*$/, "")}`);
  });
}

// --- the saves card: export, import, the low-end preset (papercut-saves.js) ---
function setUpOptions() {
  const say = (t) => ($("saves-status").textContent = t);
  $("export").addEventListener("click", async () => {
    try {
      const zip = await papercutSaves.exportZip();
      if (!zip) {
        say("There are no saves in this browser yet.");
        return;
      }
      const a = document.createElement("a");
      a.href = URL.createObjectURL(zip);
      a.download = `Paper Cut saves ${new Date().toISOString().slice(0, 10)}.zip`;
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(a.href), 60000);
      say("Saved as a .zip with your downloads.");
    } catch (e) {
      say(`The saves could not be read: ${e.message ?? e}`);
    }
  });
  $("import").addEventListener("click", () => $("import-file").click());
  $("import-file").addEventListener("change", async (e) => {
    const files = [...e.target.files];
    e.target.value = "";
    if (!files.length) return;
    try {
      const n = await papercutSaves.importFiles(files, (replaced) =>
        confirm(`This replaces ${replaced.length} file(s) in this browser, such as ${replaced[0]}. Go on?`));
      say(n ? `Brought in ${n} file(s).` : "Nothing was changed.");
    } catch (err) {
      say(String(err.message ?? err));
    }
  });
  const box = $("low-end");
  box.checked = papercutLowEnd.on();
  box.addEventListener("change", () => papercutLowEnd.set(box.checked));
}

async function chooseWithPicker() {
  const [handle] = await showOpenFilePicker({
    id: "papercut-disc",
    types: [{ description: "GameCube disc image", accept: { "application/octet-stream": EXTENSIONS } }],
  });
  await handleStore.set(handle);
  return handle.getFile();
}

(async () => {
  const why = await checkWebGPU();
  if (why) {
    $("unsupported-why").textContent = why;
    show("unsupported");
    return;
  }
  show("picker");
  setUpApp();
  setUpOptions();
  const go = (file) => file && start(file).catch((err) => fail(String(err?.stack ?? err)));

  // the installed app opened with a disc image (manifest.webmanifest's file_handlers)
  if ("launchQueue" in window) {
    launchQueue.setConsumer(async (p) => {
      const h = p.files?.[0];
      if (!h) return;
      await handleStore.set(h);
      go(await h.getFile());
    });
  }

  $("choose").addEventListener("click", async () => {
    if (window.showOpenFilePicker) {
      try {
        go(await chooseWithPicker());
      } catch (e) {
        if (e.name !== "AbortError") setStatus(String(e.message ?? e));
      }
    } else {
      $("disc").click();
    }
  });
  $("disc").addEventListener("change", (e) => go(e.target.files[0]));
  $("reload").addEventListener("click", () => location.reload());

  // the disc chosen last time, one click away (the browser asks again for permission)
  const handle = window.showOpenFilePicker ? await handleStore.get() : null;
  if (handle) {
    const last = $("last");
    last.textContent = `Play ${handle.name}`;
    last.hidden = false;
    $("choose").classList.add("secondary");
    last.addEventListener("click", async () => {
      try {
        if ((await handle.queryPermission({ mode: "read" })) !== "granted" &&
            (await handle.requestPermission({ mode: "read" })) !== "granted") {
          setStatus("The browser wasn't allowed to read it. Choose it again.");
          return;
        }
        go(await handle.getFile());
      } catch (e) {
        setStatus(`${handle.name} can't be read any more (moved or deleted?). Choose it again.`);
      }
    });
  }

  // drag and drop anywhere on the page
  addEventListener("dragover", (e) => {
    e.preventDefault();
    document.body.classList.add("dragging");
  });
  addEventListener("dragleave", () => document.body.classList.remove("dragging"));
  addEventListener("drop", async (e) => {
    e.preventDefault();
    document.body.classList.remove("dragging");
    const file = e.dataTransfer.files[0]; // (before any await: the drop's data goes with the event)
    const pending = e.dataTransfer.items?.[0]?.getAsFileSystemHandle?.();
    if (pending) {
      try {
        const h = await pending;
        if (h?.kind === "file") await handleStore.set(h);
      } catch {}
    }
    go(file);
  });
})();
