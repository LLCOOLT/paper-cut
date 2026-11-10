#!/usr/bin/env node
// browser_run.mjs's little sibling for Firefox, over WebDriver BiDi (Firefox
// has no DevTools protocol): headless, opens a page, optionally gives a file
// input its files, evaluates expressions and saves screenshots at set times.
// Console messages are printed as they come.
//
// usage: node firefox_run.mjs [--seconds N] [--file SELECTOR=PATH] [--eval JS@SEC] [--shot FILE@SEC] URL
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const args = process.argv.slice(2);
const opt = { seconds: 20, shots: [], evals: [], files: [] };
let url = null;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  const next = () => args[++i];
  if (a === "--seconds") opt.seconds = Number(next());
  else if (a === "--shot") opt.shots.push(next());
  else if (a === "--eval") opt.evals.push(next());
  else if (a === "--file") opt.files.push(next());
  else if (a === "--browser") opt.browser = next();
  else url = a;
}
const browser = [opt.browser, "C:/Program Files/Mozilla Firefox/firefox.exe", "/usr/bin/firefox",
  "/Applications/Firefox.app/Contents/MacOS/firefox"].filter(Boolean).find((p) => existsSync(p));
const profile = mkdtempSync(join(tmpdir(), "papercut-ff-"));
// WebGPU on, past the blocklist (headless or not)
writeFileSync(join(profile, "user.js"), [
  'user_pref("dom.webgpu.enabled", true);',
  'user_pref("gfx.webgpu.ignore-blocklist", true);',
  'user_pref("gfx.webgpu.force-enabled", true);',
  'user_pref("datareporting.policy.dataSubmissionEnabled", false);',
  'user_pref("browser.shell.checkDefaultBrowser", false);',
  "",
].join("\n"));
const port = 9800 + Math.floor(Math.random() * 100);
const proc = spawn(browser, ["--headless", "--no-remote", "--profile", profile, `--remote-debugging-port=${port}`], {
  stdio: "ignore",
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const start = Date.now();
const stamp = () => ((Date.now() - start) / 1000).toFixed(1).padStart(6);
const at = (s, d) => {
  const m = /^(.*)@([\d.]+)$/s.exec(s);
  return m ? [m[1], Number(m[2])] : [s, d];
};

let ws;
let nextId = 1;
const pending = new Map();
function send(method, params = {}) {
  const id = nextId++;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((res, rej) => pending.set(id, { res, rej }));
}

try {
  for (let i = 0; ; i++) {
    try {
      ws = new WebSocket(`ws://127.0.0.1:${port}/session`);
      await new Promise((r, j) => ((ws.onopen = r), (ws.onerror = j)));
      break;
    } catch {
      if (i > 100) throw new Error("Firefox did not start");
      await sleep(200);
    }
  }
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const p = pending.get(msg.id);
      pending.delete(msg.id);
      msg.type === "error" ? p.rej(new Error(`${msg.error}: ${msg.message}`)) : p.res(msg.result);
    } else if (msg.method === "log.entryAdded") {
      const e = msg.params;
      console.log(`${stamp()} [${e.level}] ${e.text ?? ""}`);
    }
  };
  await send("session.new", { capabilities: {} });
  await send("session.subscribe", { events: ["log.entryAdded"] });
  const { context } = await send("browsingContext.create", { type: "tab" });
  await send("browsingContext.setViewport", { context, viewport: { width: 1280, height: 720 } });
  await send("browsingContext.navigate", { context, url, wait: "complete" });
  await sleep(1500);
  const evaluate = async (expr) => {
    const r = await send("script.evaluate", {
      expression: `(async () => JSON.stringify(await (${expr})))()`,
      target: { context },
      awaitPromise: true,
    });
    return r.type === "exception" ? `!! ${r.exceptionDetails?.text}` : r.result?.value;
  };
  for (const f of opt.files) {
    const eq = f.indexOf("=");
    const node = await send("browsingContext.locateNodes", {
      context,
      locator: { type: "css", value: f.slice(0, eq) },
    });
    await send("input.setFiles", { context, element: node.nodes[0], files: [resolve(f.slice(eq + 1))] });
    console.log(`${stamp()} [file] ${f}`);
  }
  const events = [
    ...opt.evals.map((e) => {
      const [expr, t] = at(e, opt.seconds);
      return { t, run: async () => console.log(`${stamp()} [eval] ${expr} => ${await evaluate(expr)}`) };
    }),
    ...opt.shots.map((s) => {
      const [file, t] = at(s, opt.seconds);
      return {
        t,
        run: async () => {
          const r = await send("browsingContext.captureScreenshot", { context });
          writeFileSync(resolve(file), Buffer.from(r.data, "base64"));
          console.log(`${stamp()} [shot] ${resolve(file)}`);
        },
      };
    }),
  ].sort((a, b) => a.t - b.t);
  for (const e of events) {
    while (Date.now() < start + e.t * 1000) await sleep(200);
    await e.run();
  }
  while (Date.now() < start + opt.seconds * 1000) await sleep(200);
} catch (e) {
  console.error(`${stamp()} [driver] ${e.stack ?? e}`);
} finally {
  try {
    await send("browser.close", {});
  } catch {}
  await sleep(500);
  try {
    proc.kill();
  } catch {}
  await sleep(300);
  try {
    rmSync(profile, { recursive: true, force: true });
  } catch {}
}
process.exit(0);
