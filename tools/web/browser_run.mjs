#!/usr/bin/env node
// Drives a headless Chromium browser (Edge or Chrome) over the DevTools
// protocol, for testing the web build without a window: opens a page,
// optionally sets a file input's files, waits, evaluates expressions,
// saves screenshots and prints the page's console.
//
// usage: node browser_run.mjs [options] URL
//   --browser PATH        msedge.exe / chrome.exe (default: Edge, then Chrome)
//   --seconds N           how long to run (default 20)
//   --shot FILE[@SEC]     screenshot at SEC seconds (repeatable; default at the end)
//   --eval JS[@SEC]       evaluate and print at SEC seconds (repeatable)
//   --file SELECTOR=PATH  give the file input these files (after the page loads)
//   --size WxH            viewport (default 1280x720)
//   --until JS            stop early once this expression is true
//   --keys SEC:KEY,...    press keys (DOM key names, e.g. 5:Enter,6:ArrowDown)
//   --headed              show the window (default headless, never focused)
//   --flag F              extra browser flag (repeatable)
//   --profile DIR         keep the browser profile there (saves persist between runs)
//   --tap SEC:X,Y[;...]   touch the page there (a finger down, then up after 150 ms)
//   --mouse SEC:X,Y[,DY][;...]  click there, or with DY turn the wheel there
//   --cpu FROM-TO         sample the page's CPU from FROM to TO seconds; prints the
//                         functions with the most self time (wasm names included)
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const args = process.argv.slice(2);
const opt = { seconds: 20, shots: [], evals: [], files: [], size: "1280x720", keys: [], flags: [], headed: false };
let url = null;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  const next = () => args[++i];
  if (a === "--browser") opt.browser = next();
  else if (a === "--seconds") opt.seconds = Number(next());
  else if (a === "--shot") opt.shots.push(next());
  else if (a === "--eval") opt.evals.push(next());
  else if (a === "--file") opt.files.push(next());
  else if (a === "--size") opt.size = next();
  else if (a === "--until") opt.until = next();
  else if (a === "--keys") opt.keys.push(...next().split(","));
  else if (a === "--headed") opt.headed = true;
  else if (a === "--flag") opt.flags.push(next());
  else if (a === "--profile") opt.profile = next();
  else if (a === "--cpu") opt.cpu = next().split("-").map(Number);
  else if (a === "--tap") (opt.taps ??= []).push(...next().split(";"));
  else if (a === "--mouse") (opt.mouse ??= []).push(...next().split(";"));
  else url = a;
}
if (!url) {
  console.error("usage: node browser_run.mjs [options] URL");
  process.exit(2);
}
const candidates = [
  opt.browser,
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/usr/bin/google-chrome",
  "/usr/bin/chromium",
].filter(Boolean);
const browser = candidates.find((p) => existsSync(p));
if (!browser) {
  console.error("no browser found; pass --browser");
  process.exit(2);
}
const [w, h] = opt.size.split("x").map(Number);
const profile = opt.profile ? resolve(opt.profile) : mkdtempSync(join(tmpdir(), "papercut-web-"));
const port = 9300 + Math.floor(Math.random() * 500);
const flags = [
  `--remote-debugging-port=${port}`,
  `--user-data-dir=${profile}`,
  "--no-first-run",
  "--no-default-browser-check",
  "--disable-extensions",
  "--enable-unsafe-webgpu",
  "--enable-gpu",
  "--ignore-gpu-blocklist",
  "--autoplay-policy=no-user-gesture-required",
  `--window-size=${w},${h}`,
  ...(opt.headed ? [] : ["--headless=new"]),
  ...opt.flags,
  "about:blank",
];
const proc = spawn(browser, flags, { stdio: "ignore" });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function json(path) {
  for (let i = 0; i < 100; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${port}${path}`);
      return await r.json();
    } catch {
      await sleep(100);
    }
  }
  throw new Error("browser did not start");
}

let ws;
let nextId = 1;
const pending = new Map();
function send(method, params = {}) {
  const id = nextId++;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((res, rej) => pending.set(id, { res, rej }));
}
const start = Date.now();
const stamp = () => ((Date.now() - start) / 1000).toFixed(1).padStart(6);

function at(spec, dflt) {
  const m = /^(.*)@([\d.]+)$/s.exec(spec);
  return m ? [m[1], Number(m[2])] : [spec, dflt];
}

async function evaluate(expr) {
  const r = await send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) return `!! ${r.exceptionDetails.exception?.description ?? r.exceptionDetails.text}`;
  return JSON.stringify(r.result.value);
}

async function shot(file) {
  const r = await send("Page.captureScreenshot", { format: "png" });
  writeFileSync(resolve(file), Buffer.from(r.data, "base64"));
  console.log(`${stamp()} [shot] ${resolve(file)}`);
}

let exitCode = 0;
try {
  const targets = await json("/json/list");
  const page = targets.find((t) => t.type === "page");
  ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const p = pending.get(msg.id);
      pending.delete(msg.id);
      msg.error ? p.rej(new Error(msg.error.message)) : p.res(msg.result);
    } else if (msg.method === "Runtime.consoleAPICalled") {
      const text = msg.params.args.map((a) => a.value ?? a.description ?? "").join(" ");
      console.log(`${stamp()} [${msg.params.type}] ${text}`);
    } else if (msg.method === "Runtime.exceptionThrown") {
      const d = msg.params.exceptionDetails;
      console.log(`${stamp()} [exception] ${d.exception?.description ?? d.text}`);
    } else if (msg.method === "Log.entryAdded") {
      console.log(`${stamp()} [log:${msg.params.entry.level}] ${msg.params.entry.text}`);
    }
  };
  await send("Runtime.enable");
  await send("Log.enable");
  await send("Page.enable");
  await send("DOM.enable");
  await send("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: 1, mobile: false });
  await send("Page.navigate", { url });
  await sleep(1500);

  for (const f of opt.files) {
    const eq = f.indexOf("=");
    const sel = f.slice(0, eq);
    const path = resolve(f.slice(eq + 1));
    const doc = await send("DOM.getDocument");
    const node = await send("DOM.querySelector", { nodeId: doc.root.nodeId, selector: sel });
    await send("DOM.setFileInputFiles", { nodeId: node.nodeId, files: [path] });
    console.log(`${stamp()} [file] ${sel} <- ${path}`);
  }

  const events = [];
  for (const s of opt.shots) {
    const [file, t] = at(s, opt.seconds);
    events.push({ t, run: () => shot(file) });
  }
  for (const e of opt.evals) {
    const [expr, t] = at(e, opt.seconds);
    events.push({ t, run: async () => console.log(`${stamp()} [eval] ${expr} => ${await evaluate(expr)}`) });
  }
  for (const k of opt.keys) {
    const [t, key] = k.split(":");
    events.push({
      t: Number(t),
      run: async () => {
        const code = key.length === 1 ? `Key${key.toUpperCase()}` : key;
        const vk = { Enter: 13, ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39, Escape: 27, Space: 32 }[key] ??
          key.toUpperCase().charCodeAt(0);
        const base = { key: key === "Space" ? " " : key, code, windowsVirtualKeyCode: vk };
        await send("Input.dispatchKeyEvent", { type: "keyDown", ...base });
        await sleep(120);
        await send("Input.dispatchKeyEvent", { type: "keyUp", ...base });
        console.log(`${stamp()} [key] ${key}`);
      },
    });
  }
  for (const tap of opt.taps ?? []) {
    const [t, xy] = tap.split(":");
    const [x, y] = xy.split(",").map(Number);
    events.push({
      t: Number(t),
      run: async () => {
        await send("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
        await send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x, y, id: 1 }] });
        await sleep(150);
        await send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
        console.log(`${stamp()} [tap] ${x},${y}`);
      },
    });
  }
  // the mouse: SEC:X,Y a click, SEC:X,Y,DY a wheel turn there (DY pixels, down positive)
  for (const m of opt.mouse ?? []) {
    const [t, xy] = m.split(":");
    const [x, y, dy] = xy.split(",").map(Number);
    events.push({
      t: Number(t),
      run: async () => {
        await send("Input.dispatchMouseEvent", { type: "mouseMoved", x, y });
        if (dy) {
          await send("Input.dispatchMouseEvent", { type: "mouseWheel", x, y, deltaX: 0, deltaY: dy });
        } else {
          await send("Input.dispatchMouseEvent", { type: "mousePressed", x, y, button: "left", clickCount: 1 });
          await sleep(80);
          await send("Input.dispatchMouseEvent", { type: "mouseReleased", x, y, button: "left", clickCount: 1 });
        }
        console.log(`${stamp()} [mouse] ${xy}`);
      },
    });
  }
  if (opt.cpu) {
    events.push({
      t: opt.cpu[0],
      run: async () => {
        await send("Profiler.enable");
        await send("Profiler.setSamplingInterval", { interval: 200 });
        await send("Profiler.start");
        console.log(`${stamp()} [cpu] sampling`);
      },
    });
    events.push({
      t: opt.cpu[1],
      run: async () => {
        const { profile } = await send("Profiler.stop");
        const self = new Map();
        const dt = profile.timeDeltas;
        const byId = new Map(profile.nodes.map((n) => [n.id, n]));
        let total = 0;
        profile.samples.forEach((id, i) => {
          const n = byId.get(id);
          const name = n.callFrame.functionName || `(${n.callFrame.url.split("/").pop() || "program"})`;
          const d = dt[i] ?? 0;
          total += d;
          self.set(name, (self.get(name) ?? 0) + d);
        });
        const top = [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, 40);
        console.log(`${stamp()} [cpu] ${(total / 1000).toFixed(0)} ms sampled; self time:`);
        for (const [name, t] of top) {
          console.log(`  ${((100 * t) / total).toFixed(1).padStart(5)}%  ${name.slice(0, 140)}`);
        }
      },
    });
  }
  events.sort((a, b) => a.t - b.t);
  const end = start + opt.seconds * 1000;
  for (const e of events) {
    while (Date.now() < start + e.t * 1000) {
      if (opt.until && (await evaluate(opt.until)) === "true") break;
      await sleep(250);
    }
    await e.run();
  }
  while (Date.now() < end) {
    if (opt.until && (await evaluate(opt.until)) === "true") {
      console.log(`${stamp()} [until] ${opt.until}`);
      break;
    }
    await sleep(250);
  }
} catch (e) {
  console.error(`${stamp()} [driver] ${e.stack ?? e}`);
  exitCode = 1;
} finally {
  try {
    await send("Browser.close");
  } catch {}
  await sleep(500);
  try {
    proc.kill();
  } catch {}
  await sleep(300);
  try {
    if (!opt.profile) rmSync(profile, { recursive: true, force: true });
  } catch {}
}
process.exit(exitCode);
