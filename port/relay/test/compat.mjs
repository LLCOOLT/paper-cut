// The relay worker still serves the games already out there: run before every deploy
// (docs/ONLINE_RELAY.md). 0.4.0 and 0.4.1 play lockstep rooms; those players keep using the same
// relay after 0.5.0, so the worker must answer them exactly as the 0.4.1 worker did.
//
// Loads two copies of port/relay/src/index.js (old: the 0.4.1 release's; new: this one) against a
// mock of the Durable Object runtime and plays the same scripted lockstep-room sessions (host,
// guest, seats, watchers, listing, reconnects, refusals, every message route) through each Room,
// and the same reports and room-list requests through each Lobby, then compares everything every
// connection received and every response: they must be the same. The room list is also checked
// to show each version only its own rooms (a 0.4.x client never sees a 0.5.0 room, nor the other
// way). Then every HTTPS request a 0.4.1 game makes (paper notes, leaderboards and their replays and
// checks, the daily seed's boards, the raid boss's weekly boards, bingo rooms), with the refusals it
// can meet, through each worker's front door: the same answers from both. The 0.5.0 role (peer) is
// played last and only reported.
//
//   sh run_compat.sh            (fetches the 0.4.1 worker from its release tag)
//   node compat.mjs old.mjs new.mjs
import { pathToFileURL } from "node:url";

class MockResponse {
  constructor(body, init = {}) {
    this.status = init.status ?? 200;
    this.webSocket = init.webSocket ?? null;
    this.headers = new Headers(init.headers ?? {});
    this.body = body;
  }
  async text() {
    return this.body === null || this.body === undefined ? "" : String(this.body);
  }
}
globalThis.Response = MockResponse;

class MockWS {
  constructor() {
    this.received = []; // what the client got (from the server side's send)
    this.closed = null;
    this.attachment = null;
    this.tags = [];
  }
  send(m) {
    if (this.closed) {
      throw new Error("closed");
    }
    this.received.push(typeof m === "string" ? "T " + m : "B " + Buffer.from(m).toString("hex"));
  }
  close(code, reason) {
    if (!this.closed) {
      this.closed = { code, reason };
    }
  }
  serializeAttachment(a) {
    this.attachment = structuredClone(a);
  }
  deserializeAttachment() {
    return this.attachment === null ? null : structuredClone(this.attachment);
  }
}
globalThis.WebSocketPair = class {
  constructor() {
    this[0] = new MockWS(); // the client's end (unused but for identity)
    this[1] = new MockWS(); // the server's end
  }
};

function makeRoom(RoomClass, reports) {
  const sockets = [];
  const ctx = {
    id: { toString: () => "room-1" },
    acceptWebSocket(ws, tags) {
      ws.tags = tags;
      sockets.push(ws);
    },
    getWebSockets(tag) {
      return sockets.filter((ws) => ws.tags.includes(tag) && !ws.gone);
    },
  };
  const env = {
    LOBBY: {
      idFromName: (n) => n,
      get: () => ({
        fetch: async (url, init) => {
          reports.push(init && init.body ? JSON.parse(init.body) : url);
          return new MockResponse("ok");
        },
      }),
    },
  };
  return { room: new RoomClass(ctx, env), sockets };
}

async function play(modPath) {
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const reports = [];
  const { room, sockets } = makeRoom(mod.Room, reports);
  const log = [];
  const clients = {}; // name -> server-side ws
  const tickets = {}; // name -> its ticket (from what it received)
  let n = 0;
  const norm = (s) => s.replace(/[0-9a-f]{32}/g, "<ticket>");

  async function open(name, role, extra = "") {
    const url = `https://relay/room/K7P2?role=${role}${extra}`;
    const req = new Request(url, { headers: { "CF-Connecting-IP": "203.0.113." + ++n } });
    const res = await room.fetch(req);
    if (res.status === 101) {
      clients[name] = res.webSocket === null ? null : sockets[sockets.length - 1];
      log.push(`${name}: open ${role} -> 101`);
    } else {
      log.push(`${name}: open ${role} -> ${res.status} ${String(res.body).trim()}`);
    }
  }
  async function binary(name, bytes) {
    const buf = new Uint8Array(bytes).buffer;
    await room.webSocketMessage(clients[name], buf);
    log.push(`${name}: sent ${Buffer.from(bytes).toString("hex")}`);
  }
  async function text(name, t) {
    await room.webSocketMessage(clients[name], t);
    log.push(`${name}: sent text ${t}`);
  }
  // the runtime's close of a connection (the client left, or the server closed it)
  async function leave(name, code = 1000) {
    const ws = clients[name];
    ws.gone = true;
    await room.webSocketClose(ws, code);
    log.push(`${name}: left`);
  }
  // what each client received since the last look, and closes the server made
  async function drain() {
    for (const [name, ws] of Object.entries(clients)) {
      if (!ws) {
        continue;
      }
      for (const m of ws.received) {
        const t = m.match(/^T ticket ([0-9a-f]{32})$/);
        if (t) {
          tickets[name] = t[1];
        }
        log.push(`  ${name} got ${norm(m)}`);
      }
      ws.received = [];
      if (ws.closed && !ws.closedSeen) {
        ws.closedSeen = true;
        log.push(`  ${name} closed by the relay ${ws.closed.code} ${ws.closed.reason}`);
        if (!ws.gone) {
          ws.gone = true; // (the runtime then tells the room)
          await room.webSocketClose(ws, ws.closed.code);
        }
      }
    }
    for (const r of reports.splice(0)) {
      log.push(`  report ${norm(JSON.stringify(r))}`);
    }
  }
  const step = async (f) => {
    await f();
    await drain();
    await drain();
  };

  // --- refusals with nobody there ---
  await step(() => open("g0", "guest"));
  await step(() => open("w0", "watch"));
  await step(() => open("s0", "seat"));
  await step(() => open("x0", "nobody"));
  // --- a listed lockstep room: host, guest ---
  await step(() => open("host", "host", "&list=QX9M&name=Bob%20B&game=G8ME01&v=0.4.1&max=4"));
  await step(() => open("host2", "host"));
  await step(() => open("guest", "guest"));
  await step(() => open("guest2", "guest"));
  // the host's messages: an ordinary frame, one of length 254 and 510 (starting 0xFE: the new routing's
  // first byte), one starting 0xFE 0xFE 0xFE 0x05 (no such peer place), a watchers' one
  await step(() => binary("host", [0x05, 0, 0, 0, 1, 2, 3, 4, 5]));
  await step(() => binary("host", [0xfe, 0, 0, 0, 7, ...Array(253).fill(9)]));
  await step(() => binary("host", [0xfe, 1, 0, 0, 7, 8]));
  await step(() => binary("host", [0xfe, 0xfe, 0xfe, 0x05, 1, 2]));
  await step(() => binary("host", [0xff, 0xff, 0xff, 0xff, 9, 9, 9]));
  await step(() => binary("guest", [0x03, 0, 0, 0, 7, 7, 7]));
  // seats (players 3 and 4) and watchers
  await step(() => open("seat0", "seat"));
  await step(() => open("seat1", "seat"));
  await step(() => open("seat2", "seat"));
  await step(() => binary("seat1", [0x02, 0, 0, 0, 0x11, 0x22]));
  await step(() => binary("host", [0xff, 0xff, 0xff, 0xff, 0xaa]));
  for (let i = 0; i < 5; i++) {
    await step(() => open("watch" + i, "watch"));
  }
  await step(() => binary("watch1", [1, 2, 3]));
  await step(() => text("host", "list"));
  await step(() => text("guest", "list"));
  // a guest's drop and its ticket back
  await step(() => leave("guest", 1006));
  await step(() => open("guest", "guest", "&ticket=" + tickets.guest));
  await step(() => binary("host", [0x05, 0, 0, 0, 9, 9, 9, 9, 9]));
  // the guest's ticket used while the old connection is still there (it's replaced)
  const oldGuest = clients.guest;
  await step(() => open("guestB", "guest", "&ticket=" + tickets.guest));
  clients.guest = oldGuest;
  await step(() => binary("guestB", [0x03, 0, 0, 0, 1, 1, 1]));
  // goings
  await step(() => leave("seat0"));
  await step(() => leave("watch0"));
  await step(() => leave("guestB"));
  await step(() => leave("host"));
  return log;
}

async function peers(modPath) {
  // the new role, reported only
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const reports = [];
  const { room, sockets } = makeRoom(mod.Room, reports);
  const out = [];
  // through the worker's own front door and its role check (the 0.4.1 worker answers a peer 400, as
  // the relay out there did), then to the room
  const env = { ROOMS: { idFromName: (name) => name, get: () => ({ fetch: (r) => room.fetch(r) }) } };
  const req = (role) => ({
    url: `https://relay/room/K7P2?role=${role}`,
    method: "GET",
    headers: new Headers({ Upgrade: "websocket" }),
  });
  for (const role of ["host", "peer", "peer", "peer"]) {
    const res = await mod.default.fetch(req(role), env);
    out.push(`${role}: ${res.status} ${res.status === 101 ? "" : String(res.body).trim()}`);
  }
  if (sockets.length < 3) {
    return out; /* (the 0.4.1 worker: no peers in) */
  }
  const host = sockets[0];
  const p0 = sockets[1];
  const p1 = sockets[2];
  out.push("host got " + host.received.map((m) => m.replace(/[0-9a-f]{32}/, "<t>")).join(" | "));
  host.received = [];
  await room.webSocketMessage(p1, new Uint8Array([1, 2, 3]).buffer);
  out.push("peer 1 -> host: " + host.received.join(" | "));
  await room.webSocketMessage(host, new Uint8Array([0xfe, 0xfe, 0xfe, 0, 9, 8]).buffer);
  out.push("host -> peer 0: " + p0.received.filter((m) => m.startsWith("B")).join(" | ") + "; peer 1: " +
           p1.received.filter((m) => m.startsWith("B")).join(" | "));
  return out;
}

// 0.5.0's framed host (the new worker only, checked): told "framed"; its guest's messages wrapped
// [len][0xF2][message]; a framed message over MAX_FRAMED closes its sender, a guest's or a peer's
async function framed(modPath) {
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const { room, sockets } = makeRoom(mod.Room, []);
  const problems = [];
  const open = async (role, extra = "") => {
    const res = await room.fetch(new Request(`https://relay/room/K7P2?role=${role}${extra}`));
    return res.status === 101 ? sockets[sockets.length - 1] : null;
  };
  const host = await open("host", "&framed=1");
  const guest = await open("guest");
  const peer = await open("peer");
  if (!host || !guest || !peer) {
    return ["the framed room couldn't be opened"];
  }
  if (!host.received.includes("T framed")) {
    problems.push("the framed host wasn't told \"framed\": " + host.received.join(" | "));
  }
  host.received = [];
  await room.webSocketMessage(guest, new Uint8Array([5, 0, 0, 0, 0xf1, 0, 1, 2, 3]).buffer);
  if (host.received[0] !== "B 0a000000f205000000f100010203") {
    problems.push("the guest's message wasn't framed: " + host.received.join(" | "));
  }
  host.received = [];
  await room.webSocketMessage(host, new Uint8Array([1, 0, 0, 0, 7]).buffer);
  if (guest.received.filter((m) => m.startsWith("B")).pop() !== "B 0100000007") {
    problems.push("the host's message to the guest changed: " + guest.received.join(" | "));
  }
  await room.webSocketMessage(guest, new Uint8Array((1 << 20) - 63).buffer);
  if (!guest.closed || guest.closed.code !== 1009 || host.received.length !== 0) {
    problems.push("a guest's over-long framed message wasn't refused");
  }
  await room.webSocketMessage(peer, new Uint8Array((1 << 20) - 63).buffer);
  if (!peer.closed || peer.closed.code !== 1009 || host.received.length !== 0) {
    problems.push("a peer's over-long message wasn't refused");
  }
  // an unframed host (0.4.x's lockstep, or before): its guest's messages as they are
  const r2 = makeRoom(mod.Room, []);
  const h2 = (await r2.room.fetch(new Request("https://relay/room/K7P3?role=host"))).status === 101 ? r2.sockets[0] : null;
  const g2 = (await r2.room.fetch(new Request("https://relay/room/K7P3?role=guest"))).status === 101 ? r2.sockets[1] : null;
  if (h2 && g2) {
    if (h2.received.includes("T framed")) {
      problems.push("an unframed host was told \"framed\"");
    }
    h2.received = [];
    await r2.room.webSocketMessage(g2, new Uint8Array([3, 0, 0, 0, 7, 7, 7]).buffer);
    if (h2.received[0] !== "B 03000000070707") {
      problems.push("an unframed host's guest message changed: " + h2.received.join(" | "));
    }
  }
  return problems;
}

// Paper notes through the worker's route: a note left, read twice, rated, read again, read with
// &fresh=1 (the 0.5.0 games' refetch), rated again (refused), read. Each worker's answers must be the
// same. (out: the transcript)
async function notesRun(modPath) {
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const store = new Map();
  const storage = {
    get: async (k) => structuredClone(store.get(k)),
    put: async (k, v) => void store.set(k, structuredClone(v)),
    delete: async (k) => (Array.isArray(k) ? k.forEach((x) => store.delete(x)) : store.delete(k)),
    list: async ({ prefix }) => new Map([...store].filter(([k]) => k.startsWith(prefix)).sort()),
  };
  const area = new mod.Notes({ id: { toString: () => "notes:gor" }, storage }, {});
  const env = {
    NOTES: {
      idFromName: (n) => n,
      get: () => ({ fetch: async (req) => area.fetch(req) }),
    },
  };
  const out = [];
  const ask = async (method, query, ip, body) => {
    const init = { method, headers: { "CF-Connecting-IP": ip } };
    if (body !== undefined) {
      init.body = body;
    }
    const res = await mod.default.fetch(new Request("https://relay/notes?" + query, init), env);
    const text = await res.text();
    out.push(`notes ${method} ${query}: ${res.status} ${text.trim().split(String.fromCharCode(10)).join(" | ")}`);
    return text;
  };
  await ask("GET", "area=gor", "203.0.113.1");
  const left = await ask("POST", "area=gor", "203.0.113.1", "add gor_01 10 0 -20 3 7");
  const id = left.trim().split(" ")[1] || "";
  await ask("GET", "area=gor", "203.0.113.2");
  await ask("GET", "area=gor", "203.0.113.3");
  await ask("POST", "area=gor", "203.0.113.2", `rate gor_01 ${id} 1`);
  await ask("GET", "area=gor", "203.0.113.2");
  await ask("GET", "area=gor&fresh=1", "203.0.113.2");
  await ask("POST", "area=gor", "203.0.113.2", `rate gor_01 ${id} 1`);
  await ask("GET", "area=gor", "203.0.113.4");
  // (a note's id is new each time: the same in both transcripts as <id>)
  return { out: out.map((line) => line.split(id).join("<id>")) };
}

// The room list's Lobby: rooms report in (a listed 0.4.1 lockstep room, a listed 0.5.0 room, an
// unlisted one), then each version asks for its list
async function lobby(modPath) {
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const store = new Map();
  const storage = {
    get: async (k) => store.get(k),
    put: async (k, v) => void store.set(k, v),
    delete: async (k) => (Array.isArray(k) ? k.forEach((x) => store.delete(x)) : store.delete(k)),
    list: async ({ prefix }) => new Map([...store].filter(([k]) => k.startsWith(prefix))),
  };
  const l = new mod.Lobby({ id: { toString: () => "lobby" }, storage }, {});
  const out = [];
  const report = async (body) => {
    const res = await l.fetch(new Request("https://lobby/report", { method: "POST", body: JSON.stringify(body) }));
    out.push(`report ${body.id}: ${res.status} ${String(res.body).trim()}`);
  };
  const rooms = async (game, v) => {
    const res = await l.fetch(new Request(`https://lobby/rooms?game=${game}&v=${encodeURIComponent(v)}`));
    const text = String(res.body);
    out.push(`rooms ${game} ${v}: ${res.status} ${text.trim().replace(/\n/g, " | ")}`);
    return text;
  };
  await report({ id: "old-room", players: 1, list: { code: "QX9MAAAA", name: "Bob", game: "G8ME01", v: "0.4.1", max: 4, playing: 1 } });
  await report({ id: "new-room", players: 2, list: { code: "K7P2BBBB", name: "Ann", game: "G8ME01", v: "sync-0.5.0", max: 4, playing: 2 } });
  await report({ id: "quiet-room", players: 2, list: null });
  await report({ id: "bad" });
  const oldList = await rooms("G8ME01", "0.4.1");
  const newList = await rooms("G8ME01", "sync-0.5.0");
  await rooms("G8MJ01", "0.4.1");
  await report({ id: "old-room", players: 0 });
  await rooms("G8ME01", "0.4.1");
  const own = oldList.includes("QX9MAAAA") && !oldList.includes("K7P2BBBB") &&
              newList.includes("K7P2BBBB") && !newList.includes("QX9MAAAA");
  return { out, own };
}

// A Durable Object's storage, as the classes use it (a value is kept as a copy, as the runtime does)
function makeStorage() {
  const store = new Map();
  let alarm = null;
  const copy = (v) => (v === undefined ? undefined : structuredClone(v));
  return {
    get: async (k) =>
      Array.isArray(k) ? new Map(k.filter((x) => store.has(x)).map((x) => [x, copy(store.get(x))])) : copy(store.get(k)),
    put: async (k, v) => void store.set(k, copy(v)),
    delete: async (k) => (Array.isArray(k) ? k.filter((x) => store.delete(x)).length : store.delete(k)),
    list: async ({ prefix = "" } = {}) =>
      new Map([...store.keys()].filter((k) => k.startsWith(prefix)).sort().map((k) => [k, copy(store.get(k))])),
    getAlarm: async () => alarm,
    setAlarm: async (t) => void (alarm = t),
    deleteAll: async () => {
      store.clear();
      alarm = null;
    },
  };
}

// a binding's Durable Objects: one per name, made the first time it's asked for
function namespace(Cls, env) {
  const objects = new Map();
  return {
    idFromName: (name) => name,
    get: (id) => {
      if (!objects.has(id)) {
        objects.set(id, new Cls({ id: { toString: () => id }, storage: makeStorage() }, env));
      }
      return objects.get(id);
    },
    objects,
  };
}

// The boards and the rest a 0.4.1 game asks the relay over HTTPS (port/settings/features: notes.cpp,
// boards.cpp, daily.cpp, raid.cpp, bingo.cpp), through the worker's front door, each request as that
// version makes it, with the refusals it can meet: every answer's status, type and body. The ids the
// relay makes up (random) are numbered by first appearance, the same in both transcripts when the
// answers are.
async function boards(modPath) {
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const env = {};
  env.NOTES = namespace(mod.Notes, env);
  env.BOARDS = namespace(mod.Board, env);
  env.DAILY = namespace(mod.Daily, env);
  env.RAID = namespace(mod.Raid, env);
  env.BINGO = namespace(mod.Bingo, env);
  const out = [];
  const ids = new Map();
  const norm = (s) =>
    s.replace(/\b[0-9a-f]{12}\b/g, (id) => {
      if (!ids.has(id)) {
        ids.set(id, "<id" + (ids.size + 1) + ">");
      }
      return ids.get(id);
    });
  // The worker's clock steps 1 ms per request, the same for both workers: notes with the same rating
  // are listed newest first, and two posts in the same real millisecond or not made the order differ
  // between the two transcripts (1 run in 5).
  const realNow = Date.now;
  const base = realNow();
  let tick = 0;
  Date.now = () => base + tick;
  const call = async (who, method, path, body) => {
    tick++;
    const init = { method, headers: { "CF-Connecting-IP": "198.51.100." + who } };
    if (body !== undefined) {
      init.body = body;
    }
    const res = await mod.default.fetch(new Request("https://relay" + path, init), env);
    const b = res.body;
    const text = typeof b === "string" ? b.trim() : b == null ? "" : "bytes " + Buffer.from(b).toString("hex");
    out.push(norm(`${who} ${method} ${path}${body !== undefined && typeof body === "string" ? " [" + body + "]" : ""}` +
                  ` -> ${res.status} ${res.headers.get("content-type") || "-"} ${text.replace(/\n/g, " | ")}`));
    return text;
  };
  const idIn = (text, at = 1) => (text.split(/\s+/)[at] || "");
  const now = Date.now();
  const ymd = (ms) => new Date(ms).toISOString().slice(0, 10).replace(/-/g, "");
  const week = Math.floor((Math.floor(now / 86400e3) + 3) / 7);
  const boss = week % 6;

  // --- the front door ---
  await call(1, "GET", "/");
  await call(1, "GET", "/health");
  await call(1, "PUT", "/notes?area=gor");
  await call(1, "GET", "/nothing");
  await call(1, "GET", "/notes?x=" + "a".repeat(2100));

  // --- paper notes (notes.cpp) ---
  await call(1, "GET", "/notes?area=gor");
  await call(1, "GET", "/notes?area=GOR");
  await call(1, "GET", "/notes");
  const n1 = idIn(await call(1, "POST", "/notes?area=gor", "add gor_01 120 0 -45 3 17"));
  await call(1, "POST", "/notes?area=gor", "add gor_01 -300 10 80 12 900");
  await call(1, "POST", "/notes?area=gor", "add gor_02 0 0 0 0 0");
  await call(1, "POST", "/notes?area=gor", "add gor_01 1 1 1 1 1");
  await call(1, "POST", "/notes?area=gor", "add gor_01 2 2 2 2 2");
  await call(1, "POST", "/notes?area=gor", "add hei_01 0 0 0 0 0");
  await call(1, "POST", "/notes?area=gor", "add gor_01 0 0 0 256 0");
  await call(1, "POST", "/notes?area=gor", "add gor_01 30000 0 0 1 1");
  await call(1, "POST", "/notes?area=gor", "add gor_01 0 0");
  await call(1, "POST", "/notes?area=gor", "write gor_01 hello");
  await call(1, "POST", "/notes?area=gor", "x".repeat(300));
  await call(1, "POST", "/notes?area=gor", `rate gor_01 ${n1} 1`);
  await call(2, "POST", "/notes?area=gor", `rate gor_01 ${n1} 1`);
  await call(2, "POST", "/notes?area=gor", `rate gor_01 ${n1} -1`);
  await call(2, "POST", "/notes?area=gor", `rate gor_01 ${n1} 2`);
  await call(2, "POST", "/notes?area=gor", "rate gor_01 0123456789ab 1");
  for (const who of [3, 4, 5, 6]) {
    await call(who, "POST", "/notes?area=gor", `rate gor_01 ${n1} -1`);
  }
  await call(7, "POST", "/notes?area=gor", `rate gor_01 ${n1} 1`);
  await call(8, "GET", "/notes?area=gor");
  await call(8, "GET", "/notes?area=hei");

  // --- leaderboards (boards.cpp) ---
  const q = (board = "pit100", game = "G8ME01", v = "0.4.1") => `board=${board}&game=${game}&v=${v}`;
  const replay = (n) => new Uint8Array([0x78, 0x9c, ...Array.from({ length: n }, (_, i) => (i * 37 + n) & 0xff)]);
  await call(1, "GET", "/board?" + q());
  await call(1, "GET", "/board?" + q("pit99"));
  await call(1, "GET", "/board?" + q("pit100", "G8ME"));
  await call(1, "GET", "/board?" + q("pit100", "G8ME01", ""));
  await call(1, "GET", "/board?" + q("bossrush-mid-50", "G8MP01"));
  const r1 = idIn(await call(1, "POST", "/board?" + q() + "&frames=54000&name=Bob%20B", replay(40)));
  await call(1, "POST", "/board?" + q() + "&frames=60000&name=Bob", replay(40));
  const r2 = idIn(await call(2, "POST", "/board?" + q() + "&frames=48000&name=Ann", replay(200000)));
  await call(3, "POST", "/board?" + q() + "&frames=50000&name=", replay(30));
  await call(4, "POST", "/board?" + q() + "&name=NoFrames", replay(30));
  await call(4, "POST", "/board?" + q() + "&frames=0&name=Zero", replay(30));
  await call(4, "POST", "/board?" + q() + "&frames=50000&name=Short", replay(5));
  await call(4, "POST", "/board?" + q() + "&frames=50000&name=NotZlib", new Uint8Array(40).fill(1));
  await call(4, "POST", "/board?" + q() + "&frames=50000&name=Big", replay((1 << 20) + 10));
  await call(1, "POST", "/board?" + q() + "&frames=52000&name=Bob%20B", replay(50));
  await call(5, "GET", "/board?" + q());
  await call(5, "GET", "/board?" + q("pit100", "G8ME01", "0.4.0"));
  await call(5, "GET", "/replay?" + q() + "&id=" + r1);
  const big = await call(5, "GET", "/replay?" + q() + "&id=" + r2);
  out.push("replay 2 back whole: " + (big === "bytes " + Buffer.from(replay(200000)).toString("hex")));
  out[out.length - 2] = out[out.length - 2].slice(0, 200); // (the long replay's bytes: the line above checks them)
  await call(5, "GET", "/replay?" + q() + "&id=000000000000");
  await call(6, "POST", "/check?" + q() + "&id=" + r2, "48000");
  await call(7, "POST", "/check?" + q() + "&id=" + r2, "47999");
  await call(7, "POST", "/check?" + q() + "&id=" + r2, "48000");
  await call(2, "POST", "/check?" + q() + "&id=" + r2, "48000");
  await call(8, "POST", "/check?" + q() + "&id=" + r2, "soon");
  await call(8, "POST", "/check?" + q() + "&id=000000000000", "48000");
  await call(8, "GET", "/check?" + q() + "&id=" + r2);
  await call(8, "POST", "/replay?" + q() + "&id=" + r2, "x");
  for (let who = 10; who < 22; who++) {
    await call(who, "POST", "/board?" + q("bossrush-late") + `&frames=${70000 - who * 100}&name=P${who}`, replay(20 + who));
  }
  await call(30, "POST", "/board?" + q("bossrush-late") + "&frames=99999&name=Late", replay(20));
  await call(30, "GET", "/board?" + q("bossrush-late"));

  // --- the daily seed's boards (daily.cpp) ---
  const today = ymd(now);
  const dq = (name, ms, game = "G8ME01") => `?name=${name}&ms=${ms}&game=${game}&v=0.4.1`;
  await call(1, "GET", "/daily/" + today);
  await call(1, "GET", "/daily/20261340");
  await call(1, "GET", "/daily/2026111");
  await call(1, "GET", "/daily/" + today + dq("Bob", 3600000));
  await call(1, "GET", "/daily/" + today + dq("Bob", 4000000));
  await call(1, "GET", "/daily/" + today + dq("Bob", 3000000));
  await call(2, "GET", "/daily/" + today + dq("Ann%20%26%20Co", 3000000, "G8MJ01"));
  await call(2, "GET", "/daily/" + today + dq("Cy", 2000000, "??????"));
  await call(3, "GET", "/daily/" + today + dq("Fast", 1000));
  await call(3, "GET", "/daily/" + today + dq("Slow", 7 * 24 * 3600e3 + 1));
  await call(3, "GET", "/daily/" + today + dq("", 3600000));
  await call(3, "GET", "/daily/" + today + dq("NoMs", ""));
  for (const name of ["A1", "A2", "A3", "A4"]) {
    await call(4, "GET", "/daily/" + today + dq(name, 5000000));
  }
  await call(5, "GET", "/daily/" + ymd(now - 2 * 86400e3) + dq("Late", 3600000));
  await call(5, "GET", "/daily/" + ymd(now - 3 * 86400e3) + dq("TooLate", 3600000));
  await call(5, "GET", "/daily/" + ymd(now + 86400e3) + dq("Early", 3600000));
  await call(6, "GET", "/daily/" + today);
  await call(6, "GET", "/daily/" + ymd(now - 2 * 86400e3));
  await env.DAILY.get(today).alarm(); // (the day's board dropped, as its alarm does)
  await call(6, "GET", "/daily/" + today);

  // --- the raid boss's weekly boards (raid.cpp) ---
  const rq = (name, b, turns, frames, players, game = "G8ME01") =>
    `?name=${name}&boss=${b}&turns=${turns}&frames=${frames}&players=${players}&game=${game}&v=0.4.1`;
  await call(1, "GET", "/raid/" + week);
  await call(1, "GET", "/raid/abc");
  await call(1, "GET", "/raid/1234567");
  await call(1, "GET", "/raid/" + week + rq("Bob%20%26%20Ann", boss, 12, 40000, 2));
  await call(1, "GET", "/raid/" + week + rq("Bob%20%26%20Ann", boss, 14, 30000, 2));
  await call(1, "GET", "/raid/" + week + rq("Bob%20%26%20Ann", boss, 12, 39000, 2));
  await call(2, "GET", "/raid/" + week + rq("Four", boss, 9, 90000, 4));
  await call(2, "GET", "/raid/" + week + rq("WrongBoss", (boss + 1) % 6, 9, 90000, 4));
  await call(2, "GET", "/raid/" + week + rq("Alone", boss, 9, 90000, 1));
  await call(2, "GET", "/raid/" + week + rq("Five", boss, 9, 90000, 5));
  await call(2, "GET", "/raid/" + week + rq("Quick", boss, 3, 90000, 2));
  await call(2, "GET", "/raid/" + week + rq("Blink", boss, 9, 100, 2));
  await call(2, "GET", "/raid/" + week + rq("", boss, 9, 90000, 2));
  for (const name of ["R1", "R2", "R3", "R4", "R5"]) {
    await call(3, "GET", "/raid/" + week + rq(name, boss, 20, 80000, 3));
  }
  await call(4, "GET", "/raid/" + (week - 1) + rq("LastWeek", (week - 1) % 6, 10, 50000, 2));
  await call(4, "GET", "/raid/" + (week - 2) + rq("Older", (week - 2) % 6, 10, 50000, 2));
  await call(4, "GET", "/raid/" + (week + 1) + rq("Next", (week + 1) % 6, 10, 50000, 2));
  await call(5, "GET", "/raid/" + week);
  await call(5, "GET", "/raid/" + (week - 1));

  // --- lockout bingo rooms (bingo.cpp) ---
  await call(1, "GET", "/bingo/R7Q2?name=Bob&seed=PARTY-0000ABCD");
  await call(2, "GET", "/bingo/R7Q2?name=Ann&seed=PARTY-0000ABCD&claim=3,7");
  await call(1, "GET", "/bingo/R7Q2?name=Bob&seed=PARTY-0000ABCD&claim=3,4,99,-1,x");
  await call(3, "GET", "/bingo/R7Q2?name=&seed=PARTY-0000ABCD");
  await call(3, "GET", "/bingo/NEW1?name=Cy&seed=bad%20seed");
  await call(3, "GET", "/bingo/NEW1?name=Cy&seed=open-world_1");
  await call(3, "GET", "/bingo/bad!");
  for (let i = 0; i < 8; i++) {
    await call(10 + i, "GET", `/bingo/FULL?name=P${i}&seed=S1`);
  }
  await call(20, "GET", "/bingo/FULL?name=Ninth&seed=S1");
  Date.now = realNow;
  return out;
}

const [oldPath, newPath] = process.argv.slice(2);
const a = await play(oldPath);
const b = await play(newPath);
{
  const la = await lobby(oldPath);
  const lb = await lobby(newPath);
  a.push(...la.out);
  b.push(...lb.out);
  if (!lb.own) {
    console.log("ROOM LIST: a version sees another version's rooms (or not its own)");
    b.push("room list mixed");
  }
}
{
  const ba = await boards(oldPath);
  const bb = await boards(newPath);
  let differ = ba.length !== bb.length;
  for (let i = 0; i < Math.max(ba.length, bb.length); i++) {
    differ ||= ba[i] !== bb[i];
  }
  console.log(`boards: ${ba.length} requests (notes, leaderboards, daily, raid, bingo), ` +
              (differ ? "DIFFERENT" : "the same answers from both workers"));
  a.push(...ba);
  b.push(...bb);
}
{
  const na = await notesRun(oldPath);
  const nb = await notesRun(newPath);
  a.push(...na.out);
  b.push(...nb.out);
  console.log(`NOTES: ${na.out.length} requests (&fresh=1 among them), the 0.4.1 worker's and this one's compared`);
}
let same = a.length === b.length;
for (let i = 0; i < Math.max(a.length, b.length); i++) {
  if (a[i] !== b[i]) {
    same = false;
    console.log(`DIFFERS at line ${i}:\n  0.4.1: ${a[i]}\n  new:   ${b[i]}`);
  }
}
console.log(`lockstep rooms: ${a.length} events, ${same ? "the same with both workers" : "DIFFERENT"}`);
console.log("--- the 0.4.1 worker's transcript ---");
console.log(a.join("\n"));
console.log("--- peers (the new worker) ---");
console.log((await peers(newPath)).join("\n"));
console.log("--- peers (the 0.4.1 worker) ---");
console.log((await peers(oldPath)).slice(0, 4).join("\n"));
const framedProblems = await framed(newPath);
console.log("--- framed host (the new worker) ---");
console.log(framedProblems.length === 0 ? "framed host: as it should be" : "FRAMED: " + framedProblems.join("\nFRAMED: "));
process.exit(same && framedProblems.length === 0 ? 0 : 1);
