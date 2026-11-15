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

// what the Lobby says of the relay's day when a 0.5.1 host asks, and the units the rooms told it (split())
const budgetAnswer = { text: "budget 0 100000" };
const meterUnits = { n: 0 };

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
          // (the relay's own count of its day, 0.5.1: nothing any game gets, and the 0.4.1 worker has none;
          // checked on its own, split())
          if (String(url).endsWith("/meter")) {
            meterUnits.n += JSON.parse(init.body).units;
            return new MockResponse("budget 0 100000");
          }
          if (String(url).endsWith("/budget")) {
            return new MockResponse(budgetAnswer.text);
          }
          // (the relay's health counts, 0.5.1: on their own, or riding on a report as "h"; nothing any game
          // gets, and the 0.4.1 worker has none; checked on their own, health.mjs)
          if (String(url).endsWith("/count")) {
            return new MockResponse("ok");
          }
          const body = init && init.body ? JSON.parse(init.body) : null;
          if (body) {
            delete body.h;
          }
          reports.push(body || url);
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

// 0.5.1's split host (the new worker only, checked): &caps=1 told "caps split" (and "busy" when the day is
// mostly used; turned away when it's nearly all used, a reconnect never); one SPLIT message's parts each go
// as the host's own message to that player would; a bad one closes the host and sends nothing; a host
// that never asked has its messages as before; and what came in reaches the Lobby's count as it closes
async function split(modPath) {
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const problems = [];
  const le32 = (n) => [n & 255, (n >> 8) & 255, (n >> 16) & 255, (n >>> 24) & 255];
  const part = (place, bytes) => [place, ...le32(bytes.length), ...bytes];
  const B = (bytes) => "B " + Buffer.from(bytes).toString("hex");
  const { room, sockets } = makeRoom(mod.Room, []);
  const open = async (role, extra = "") => {
    const res = await room.fetch(new Request(`https://relay/room/K7P2?role=${role}${extra}`));
    return res.status === 101 ? sockets[sockets.length - 1] : res.status;
  };
  budgetAnswer.text = "budget 0 100000";
  meterUnits.n = 0;
  const host = await open("host", "&framed=1&caps=1");
  const guest = await open("guest");
  const p0 = await open("peer");
  const p1 = await open("peer");
  if (typeof host !== "object" || typeof guest !== "object" || typeof p0 !== "object" || typeof p1 !== "object") {
    return ["the split room couldn't be opened"];
  }
  if (!host.received.includes("T caps split") || host.received.includes("T busy")) {
    problems.push("the caps host wasn't told \"caps split\" (alone): " + host.received.join(" | "));
  }
  const g = [5, 0, 0, 0, 3, 1, 2, 3, 4];
  const q0 = [1, 0, 0, 0, 3];
  const q1 = [2, 0, 0, 0, 3, 9];
  for (const ws of [guest, p0, p1]) {
    ws.received = [];
  }
  await room.webSocketMessage(host, new Uint8Array([0xfd, 0xfd, 0xfd, 0xfd, ...part(0xff, g), ...part(1, q1),
                                                    ...part(0, q0)]).buffer);
  if (guest.received.join() !== B(g) || p0.received.join() !== B(q0) || p1.received.join() !== B(q1)) {
    problems.push("the parts didn't each go to their player as they are: guest " + guest.received.join(" | ") +
                  "; peer 0 " + p0.received.join(" | ") + "; peer 1 " + p1.received.join(" | "));
  }
  for (const ws of [guest, p0, p1]) {
    ws.received = [];
  }
  // a part longer than what's left: the host closed, nothing sent (not even the good part before it)
  await room.webSocketMessage(host, new Uint8Array([0xfd, 0xfd, 0xfd, 0xfd, ...part(0xff, g), 0, ...le32(99), 1]).buffer);
  if (!host.closed || host.closed.code !== 1007 || guest.received.length + p0.received.length !== 0) {
    problems.push("a bad split message wasn't refused whole");
  }
  // a host that never asked: SPLIT bytes go to its guest as any message of its (as before)
  const r2 = makeRoom(mod.Room, []);
  await r2.room.fetch(new Request("https://relay/room/K7P3?role=host&framed=1"));
  await r2.room.fetch(new Request("https://relay/room/K7P3?role=guest"));
  const [h2, g2] = r2.sockets;
  if (h2.received.includes("T caps split")) {
    problems.push("a host that didn't ask was told \"caps split\"");
  }
  g2.received = [];
  const raw = [0xfd, 0xfd, 0xfd, 0xfd, ...part(0xff, g)];
  await r2.room.webSocketMessage(h2, new Uint8Array(raw).buffer);
  if (g2.received.join() !== B(raw)) {
    problems.push("an old host's message changed: " + g2.received.join(" | "));
  }
  // the count: four connections (20 each) and three messages in (the bad one among them), told as they close
  meterUnits.n = 0;
  for (const ws of [guest, p0, p1, host]) {
    ws.gone = true;
    await room.webSocketClose(ws, 1000);
  }
  if (meterUnits.n !== 4 * 20 + 2) {
    problems.push(`the day's count: ${meterUnits.n} units told, ${4 * 20 + 2} came`);
  }
  // the day mostly used: a new caps host told "busy"; nearly all used: turned away (429), its reconnect let in
  budgetAnswer.text = "budget 85000 100000";
  const r3 = makeRoom(mod.Room, []);
  await r3.room.fetch(new Request("https://relay/room/K7P4?role=host&caps=1"));
  if (!r3.sockets[0] || !r3.sockets[0].received.includes("T busy")) {
    problems.push("a host at 85% of the day wasn't told \"busy\"");
  }
  budgetAnswer.text = "budget 96000 100000";
  const r4 = makeRoom(mod.Room, []);
  const full = await r4.room.fetch(new Request("https://relay/room/K7P5?role=host&caps=1"));
  if (full.status !== 429) {
    problems.push(`a new host at 96% of the day: ${full.status}, not 429`);
  }
  const old = await r4.room.fetch(new Request("https://relay/room/K7P5?role=host"));
  if (old.status !== 101) {
    problems.push(`an older game's host at 96% of the day: ${old.status} (it's never refused by the count)`);
  }
  // the Lobby: counts added up for the day, "budget <requests> <limit>"
  const store = new Map();
  const storage = {
    get: async (k) => store.get(k),
    put: async (k, v) => void store.set(k, v),
    delete: async (k) => store.delete(k),
    list: async ({ prefix }) => new Map([...store].filter(([k]) => k.startsWith(prefix))),
  };
  const l = new mod.Lobby({ id: { toString: () => "lobby" }, storage }, {});
  await l.fetch(new Request("https://lobby/meter", { method: "POST", body: JSON.stringify({ units: 2000 }) }));
  await l.fetch(new Request("https://lobby/meter", { method: "POST", body: JSON.stringify({ units: 380 }) }));
  const b = String((await l.fetch(new Request("https://lobby/budget"))).body).trim();
  if (!b.startsWith("budget 122 100000")) {
    problems.push("the Lobby's count: " + b + " (2,380 units and three requests: 122 of 100000)");
  }
  budgetAnswer.text = "budget 0 100000";
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

// Taking a note back (0.5.1, "take <MAP> <ID>"), through the worker's front door: only from the address that
// left it (another's is refused "own", 403), it frees its place in the three a map, a taken note is gone (404)
// for a second take and for a rating, and the GET no longer lists it; malformed takes are "bad" (400). On the
// 0.4.1 worker a take is "bad: add or rate" (400): the words the game says "can't take notes back yet" for.
// Returns the problems (none: as it should be).
async function takes(modPath, oldPath) {
  const problems = [];
  const run = async (path) => {
    const mod = await import(pathToFileURL(path).href + "?" + Math.random());
    const env = {};
    env.NOTES = namespace(mod.Notes, env);
    return async (who, method, body) => {
      const init = { method, headers: { "CF-Connecting-IP": "192.0.2." + who } };
      if (body !== undefined) {
        init.body = body;
      }
      const res = await mod.default.fetch(new Request("https://relay/notes?area=gor", init), env);
      return { status: res.status, text: (await res.text()).trim() };
    };
  };
  const expect = (what, got, status, start) => {
    if (got.status !== status || !got.text.startsWith(start)) {
      problems.push(`${what}: ${got.status} ${got.text} (wanted ${status} ${start}...)`);
    }
  };
  const call = await run(modPath);
  const ids = [];
  for (let k = 0; k < 3; k++) {
    const left = await call(1, "POST", `add gor_01 ${k} 0 0 3 5`);
    expect("add " + k, left, 200, "ok ");
    ids.push(left.text.split(" ")[1]);
  }
  expect("a fourth add", await call(1, "POST", "add gor_01 9 0 0 3 5"), 409, "mine:");
  expect("another's take", await call(2, "POST", `take gor_01 ${ids[0]}`), 403, "own:");
  expect("a take with no id", await call(1, "POST", "take gor_01"), 400, "bad:");
  expect("a take of a bad id", await call(1, "POST", "take gor_01 xyz"), 400, "bad:");
  expect("a take on another area's map", await call(1, "POST", `take hei_01 ${ids[0]}`), 400, "bad:");
  expect("a take of no such note", await call(1, "POST", "take gor_01 0123456789ab"), 404, "gone:");
  expect("a rating before", await call(2, "POST", `rate gor_01 ${ids[0]} 1`), 200, "ok 1");
  expect("the author's take", await call(1, "POST", `take gor_01 ${ids[0]}`), 200, "ok taken");
  expect("the same take again", await call(1, "POST", `take gor_01 ${ids[0]}`), 404, "gone:");
  expect("a rating of a taken note", await call(3, "POST", `rate gor_01 ${ids[0]} 1`), 404, "gone:");
  const list = await call(4, "GET");
  if (list.text.includes(ids[0]) || !list.text.startsWith("notes 2")) {
    problems.push("the list after the take: " + list.text.replace(/\n/g, " | "));
  }
  expect("an add in the freed place", await call(1, "POST", "add gor_01 9 0 0 3 5"), 200, "ok ");
  expect("then a fourth again", await call(1, "POST", "add gor_01 8 0 0 3 5"), 409, "mine:");
  // every one of a map's notes taken: the map's store goes, and its list is empty
  const solo = await call(5, "POST", "add gor_02 1 1 1 0 0");
  expect("taking a map's only note", await call(5, "POST", `take gor_02 ${solo.text.split(" ")[1]}`), 200, "ok taken");
  const after = await call(4, "GET");
  if (after.text.includes("gor_02")) {
    problems.push("gor_02 still listed after its only note was taken: " + after.text.replace(/\n/g, " | "));
  }
  // the 0.4.1 worker: the words the game reads as "can't take notes back yet"
  const old = await run(oldPath);
  const oldLeft = await old(1, "POST", "add gor_01 1 0 0 3 5");
  expect("0.4.1's take", await old(1, "POST", `take gor_01 ${oldLeft.text.split(" ")[1]}`), 400, "bad: add or rate");
  return problems;
}

// A device's code (0.5.1: "add ... <CODE>", "take <MAP> <ID> <CODE>"): a coded note is taken back by its code from
// any address (an address that changed), never by another code on the same address (a household, a school, a
// carrier's shared address) nor by a plain take from its address; a note from before codes is still taken back from
// its address (a coded take too); three a map by code, six a map by address; no rating of a note left from your own
// address; the code itself is never kept, never answered; a malformed code is "bad" (400). The 0.4.1 worker
// refuses a coded add and take "bad: add or rate" (400), which the game falls back from. Returns the problems.
async function codes(modPath, oldPath) {
  const problems = [];
  const A = "0123456789abcdef0123456789abcdef", B = "fedcba9876543210fedcba9876543210";
  const C = "00112233445566778899aabbccddeeff", D = "a".repeat(64);
  const run = async (path) => {
    const mod = await import(pathToFileURL(path).href + "?" + Math.random());
    const env = { HASH_SALT: "test-salt" };
    const store = new Map();
    const storage = {
      get: async (k) => structuredClone(store.get(k)),
      put: async (k, v) => void store.set(k, structuredClone(v)),
      delete: async (k) => (Array.isArray(k) ? k.forEach((x) => store.delete(x)) : store.delete(k)),
      list: async ({ prefix }) => new Map([...store].filter(([k]) => k.startsWith(prefix)).sort()),
    };
    const area = new mod.Notes({ id: { toString: () => "notes:gor" }, storage }, env);
    env.NOTES = { idFromName: (n) => n, get: () => ({ fetch: async (req) => area.fetch(req) }) };
    const call = async (who, method, body) => {
      const init = { method, headers: { "CF-Connecting-IP": "198.51.100." + who } };
      if (body !== undefined) {
        init.body = body;
      }
      const res = await mod.default.fetch(new Request("https://relay/notes?area=gor", init), env);
      return { status: res.status, text: (await res.text()).trim() };
    };
    return { call, store };
  };
  const expect = (what, got, status, start) => {
    if (got.status !== status || !got.text.startsWith(start)) {
      problems.push(`${what}: ${got.status} ${got.text} (wanted ${status} ${start}...)`);
    }
    return got.text.split(" ")[1];
  };
  const { call, store } = await run(modPath);
  // device A leaves a note from address 1, moves to address 2, and takes it back
  const a1 = expect("A's add", await call(1, "POST", `add gor_01 1 0 0 3 5 ${A}`), 200, "ok ");
  expect("A's take from a new address", await call(2, "POST", `take gor_01 ${a1} ${A}`), 200, "ok taken");
  // devices A and B on one address: neither takes the other's, nor does a plain take from that address
  const a2 = expect("A's second add", await call(3, "POST", `add gor_01 2 0 0 3 5 ${A}`), 200, "ok ");
  const b1 = expect("B's add, same address", await call(3, "POST", `add gor_01 3 0 0 3 5 ${B}`), 200, "ok ");
  expect("B takes A's", await call(3, "POST", `take gor_01 ${a2} ${B}`), 403, "own:");
  expect("A takes B's", await call(3, "POST", `take gor_01 ${b1} ${A}`), 403, "own:");
  expect("a plain take of a coded note, its address", await call(3, "POST", `take gor_01 ${a2}`), 403, "own:");
  expect("B rates A's, same address", await call(3, "POST", `rate gor_01 ${a2} 1`), 409, "own:");
  expect("another address rates A's", await call(4, "POST", `rate gor_01 ${a2} 1`), 200, "ok 1");
  expect("A rates its own from elsewhere: allowed, one vote an address", await call(5, "POST", `rate gor_01 ${a2} 1`), 200, "ok 2");
  // a legacy (address-authored) note: still taken back from its address, with or without a code
  const l1 = expect("a legacy add", await call(6, "POST", "add gor_01 4 0 0 3 5"), 200, "ok ");
  const l2 = expect("a legacy add", await call(6, "POST", "add gor_01 5 0 0 3 5"), 200, "ok ");
  expect("a legacy note, a coded take from another address", await call(7, "POST", `take gor_01 ${l1} ${A}`), 403, "own:");
  expect("a legacy note, a plain take from another address", await call(7, "POST", `take gor_01 ${l1}`), 403, "own:");
  expect("a legacy note, a plain take from its address", await call(6, "POST", `take gor_01 ${l1}`), 200, "ok taken");
  expect("a legacy note, a coded take from its address", await call(6, "POST", `take gor_01 ${l2} ${C}`), 200, "ok taken");
  // three a map by code (from any address), six a map by address (whatever the codes)
  for (let k = 0; k < 2; k++) {
    expect("C's add " + k, await call(10 + k, "POST", `add gor_02 ${k} 0 0 3 5 ${C}`), 200, "ok ");
  }
  expect("C's third, a third address", await call(12, "POST", `add gor_02 2 0 0 3 5 ${C}`), 200, "ok ");
  expect("C's fourth", await call(13, "POST", `add gor_02 3 0 0 3 5 ${C}`), 409, "mine:");
  for (let k = 0; k < 6; k++) {
    const code = (k.toString(16) + "1").repeat(16);
    expect("address 20, code " + k, await call(20, "POST", `add gor_03 ${k} 0 0 3 5 ${code}`), 200, "ok ");
  }
  expect("address 20, a seventh code", await call(20, "POST", `add gor_03 9 0 0 3 5 ${D}`), 409, "mine:");
  expect("address 20, a plain add", await call(20, "POST", "add gor_03 9 0 0 3 5"), 409, "mine:");
  expect("a code on another map", await call(20, "POST", `add gor_04 9 0 0 3 5 ${D}`), 200, "ok ");
  // malformed codes
  expect("a short code", await call(1, "POST", `add gor_01 1 0 0 3 5 ${A.slice(1)}`), 400, "bad:");
  expect("a code not hex", await call(1, "POST", `add gor_01 1 0 0 3 5 ${"g".repeat(32)}`), 400, "bad:");
  expect("a take's short code", await call(1, "POST", `take gor_01 ${b1} abc`), 400, "bad:");
  expect("a take's extra word", await call(1, "POST", `take gor_01 ${b1} ${A} x`), 400, "bad: add or rate");
  // the codes themselves are kept nowhere, and the list answers no author
  const kept = JSON.stringify([...store]);
  for (const code of [A, B, C, D]) {
    if (kept.includes(code)) {
      problems.push("a code kept in the store");
    }
  }
  const list = await call(30, "GET");
  if (/[0-9a-f]{32}/.test(list.text)) {
    problems.push("the list shows an author: " + list.text.replace(/\n/g, " | "));
  }
  // the 0.4.1 worker: a coded add or take is refused, so the game leaves the note without it
  const old = await run(oldPath);
  expect("0.4.1's coded add", await old.call(1, "POST", `add gor_01 1 0 0 3 5 ${A}`), 400, "bad: add or rate");
  const o1 = expect("0.4.1's plain add", await old.call(1, "POST", "add gor_01 1 0 0 3 5"), 200, "ok ");
  expect("0.4.1's coded take", await old.call(1, "POST", `take gor_01 ${o1} ${A}`), 400, "bad: add or rate");
  return problems;
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
  // (/health: "Paper Cut relay" up to 0.5.0, the relay's health report since 0.5.1; no game asks it: health.mjs)
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

// 0.5.1's guards (the new worker only, checked): the host's "drop guest" / "drop peer N" [block]
// (closed 4001, the host told "gone"/"peergone N", the ticket refused, a blocked address refused),
// a flooding connection closed 1008, an address's room connections and other requests limited
// before any Durable Object, a host's room placed by a location hint and told "where"
async function guards(modPath) {
  const mod = await import(pathToFileURL(modPath).href + "?" + Math.random());
  const { room, sockets } = makeRoom(mod.Room, []);
  const problems = [];
  const open = async (role, ip, extra = "") => {
    const res = await room.fetch(new Request(`https://relay/room/K7P4?role=${role}${extra}`,
                                             { headers: { "CF-Connecting-IP": ip } }));
    return res.status === 101 ? sockets[sockets.length - 1] : String(res.status) + " " + String(res.body).trim();
  };
  const ticketOf = (ws) => (ws.received.find((m) => m.startsWith("T ticket ")) || "").slice(9);
  const host = await open("host", "198.51.100.1", "&framed=1");
  const squat = await open("guest", "198.51.100.66");
  const peer = await open("peer", "198.51.100.3");
  if (typeof host === "string" || typeof squat === "string" || typeof peer === "string") {
    return ["the guards' room couldn't be opened"];
  }
  const squatTicket = ticketOf(squat);
  const hostTicket = ticketOf(host);
  host.received = [];
  // a guest's own "drop" is nothing
  await room.webSocketMessage(squat, "drop peer 0");
  if (peer.closed) {
    problems.push("a guest could drop a peer");
  }
  await room.webSocketMessage(host, "drop guest block");
  if (!squat.closed || squat.closed.code !== 4002 || !host.received.includes("T gone")) {
    problems.push("drop guest: " + JSON.stringify(squat.closed) + " " + host.received.join(" | "));
  }
  squat.gone = true;
  await room.webSocketClose(squat, 4002);
  if (host.received.filter((m) => m === "T gone").length !== 1) {
    problems.push("the dropped guest's close told the host again");
  }
  let again = await open("guest", "198.51.100.66", "&ticket=" + squatTicket);
  if (typeof again !== "string" || !again.startsWith("403 blocked")) {
    problems.push("a blocked squatter (with its ticket) wasn't refused: " + (typeof again === "string" ? again : "101"));
  }
  again = await open("guest", "198.51.100.66");
  if (typeof again !== "string" || !again.startsWith("403 blocked")) {
    problems.push("a blocked address wasn't refused: " + (typeof again === "string" ? again : "101"));
  }
  const real = await open("guest", "198.51.100.2");
  if (typeof real === "string") {
    problems.push("the real guest wasn't let in after the squatter: " + real);
  }
  // drop peer 0 without block: its ticket refused, its address not
  const peerTicket = ticketOf(peer);
  host.received = [];
  await room.webSocketMessage(host, "drop peer 0");
  if (!peer.closed || peer.closed.code !== 4001 || !host.received.includes("T peergone 0")) {
    problems.push("drop peer 0: " + JSON.stringify(peer.closed) + " " + host.received.join(" | "));
  }
  again = await open("peer", "198.51.100.3", "&ticket=" + peerTicket);
  if (typeof again !== "string" || !again.startsWith("403 removed")) {
    problems.push("a dropped peer's ticket wasn't refused");
  }
  again = await open("peer", "198.51.100.3");
  if (typeof again === "string") {
    problems.push("a dropped (not blocked) peer's address was refused: " + again);
  }
  // the host's refusals kept across its reconnect
  const host2 = await open("host", "198.51.100.1", "&framed=1&ticket=" + hostTicket);
  again = await open("guest", "198.51.100.66");
  if (typeof host2 === "string" || typeof again !== "string" || !again.startsWith("403 blocked")) {
    problems.push("the host's reconnect forgot its refusals");
  }
  // a flood: closed 1008
  if (typeof real !== "string") {
    const realNow = Date.now;
    let t = 1_000_000_000_000;
    Date.now = () => t;
    for (let s = 0; s < 5 && !real.closed; s++) {
      for (let i = 0; i < 150 && !real.closed; i++) {
        await room.webSocketMessage(real, new Uint8Array([1, 0, 0, 0, 7]).buffer);
      }
      t += 1000;
    }
    Date.now = realNow;
    if (!real.closed || real.closed.code !== 1008) {
      problems.push("a flooding guest wasn't closed: " + JSON.stringify(real.closed));
    }
    // (one count for both: the flood's messages are the day's meter's too, on the same attachment)
    const a = real.deserializeAttachment();
    if (!a || !a.r || !a.r.fast || !(a.n > 20)) {
      problems.push("the flood cap and the day's count aren't the same counting: " + JSON.stringify(a));
    }
  }
  // a game's pace (20 a second for a minute) is never closed
  {
    const r2 = makeRoom(mod.Room, []);
    await r2.room.fetch(new Request("https://relay/room/K7P5?role=host"));
    await r2.room.fetch(new Request("https://relay/room/K7P5?role=guest"));
    const g = r2.sockets[1];
    const realNow = Date.now;
    let t = 1_000_000_000_000;
    Date.now = () => t;
    for (let i = 0; i < 20 * 60; i++) {
      await r2.room.webSocketMessage(g, new Uint8Array([1, 0, 0, 0, 7]).buffer);
      t += 50;
    }
    Date.now = realNow;
    if (g.closed) {
      problems.push("a game's pace was taken for a flood");
    }
  }
  // a removed guest whose game left before the host's "drop guest" (told first, it leaves at once): the drop
  // finds nobody there and still refuses that place's last ticket, across the host's reconnect too
  {
    const r3 = makeRoom(mod.Room, []);
    const at = async (role, extra = "") => {
      const res = await r3.room.fetch(new Request(`https://relay/room/K7P6?role=${role}${extra}`));
      return res.status === 101 ? r3.sockets[r3.sockets.length - 1] : String(res.status) + " " + String(res.body).trim();
    };
    const h = await at("host", "&framed=1");
    const g = await at("guest");
    if (typeof h === "string" || typeof g === "string") {
      problems.push("the early-leaver's room couldn't be opened");
    } else {
      const gTicket = ticketOf(g);
      g.gone = true;
      await r3.room.webSocketClose(g, 1000);
      await r3.room.webSocketMessage(h, "drop guest");
      const back = await at("guest", "&ticket=" + gTicket);
      if (typeof back !== "string" || !back.startsWith("403 removed")) {
        problems.push("a guest gone before its drop came back with its ticket: " + (typeof back === "string" ? back : "101"));
      }
      const other = await at("guest");
      if (typeof other === "string") {
        problems.push("a new guest was refused after an early leaver's drop: " + other);
      }
    }
  }
  // the front door: an address's room connections and requests limited before any Durable Object
  {
    let asked = 0;
    let hinted = null;
    const env = {
      ROOMS: { idFromName: (n) => n, get: (id, opts) => ({ fetch: async (r) => {
        asked++;
        hinted = opts ? opts.locationHint : null;
        return new MockResponse(r.headers.get("X-Papercut-Where") || "", { status: 200 });
      } }) },
      LOBBY: { idFromName: (n) => n, get: () => ({ fetch: async () => { asked++; return new MockResponse("rooms 0"); } }) },
    };
    const req = (path, ip, cf) => {
      const r = new Request("https://relay" + path, { headers: { Upgrade: "websocket", "CF-Connecting-IP": ip } });
      if (cf) {
        Object.defineProperty(r, "cf", { value: cf });
      }
      return r;
    };
    let refusedAt = 0;
    for (let i = 1; i <= 40 && refusedAt === 0; i++) {
      const res = await mod.default.fetch(req("/room/ABCD?role=guest", "192.0.2.9"), env);
      if (res.status === 403) {
        refusedAt = i;
      }
    }
    if (refusedAt !== 31 || asked !== 30) {
      problems.push(`room connections: refused at ${refusedAt}, ${asked} reached a room`);
    }
    const other = await mod.default.fetch(req("/room/ABCD?role=guest", "192.0.2.10"), env);
    if (other.status === 403) {
      problems.push("another address was limited too");
    }
    asked = 0;
    refusedAt = 0;
    for (let i = 1; i <= 130 && refusedAt === 0; i++) {
      const res = await mod.default.fetch(req("/rooms?game=G8ME01&v=x", "192.0.2.11"), env);
      if (res.status === 403) {
        refusedAt = i;
      }
    }
    if (refusedAt !== 121 || asked !== 120) {
      problems.push(`requests: refused at ${refusedAt}, ${asked} reached the lobby`);
    }
    hinted = undefined;
    const placed = await mod.default.fetch(
      req("/room/WXYZ?role=host", "192.0.2.12", { continent: "EU", longitude: "2.35", latitude: "48.8", colo: "CDG" }), env);
    if (hinted !== "weur" || String(placed.body).trim() !== "CDG weur") {
      problems.push(`a host's room placed by hint ${hinted}, told "${placed.body}"`);
    }
    await mod.default.fetch(req("/room/WXYZ?role=guest", "192.0.2.13", { continent: "NA", longitude: "-120" }), env);
    if (hinted !== null) {
      problems.push("a guest's connection gave a location hint");
    }
  }
  return problems;
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
const splitProblems = await split(newPath);
console.log("--- split host and the day's count (the new worker) ---");
console.log(splitProblems.length === 0 ? "split host: as it should be" : "SPLIT: " + splitProblems.join("\nSPLIT: "));
const guardProblems = await guards(newPath);
console.log("--- guards (the new worker) ---");
console.log(guardProblems.length === 0 ? "guards: as they should be" : "GUARDS: " + guardProblems.join("\nGUARDS: "));
const takeProblems = await takes(newPath, oldPath);
console.log("--- notes taken back (the new worker; the 0.4.1 one's refusal) ---");
console.log(takeProblems.length === 0 ? "takes: as they should be" : "TAKES: " + takeProblems.join("\nTAKES: "));
const codeProblems = await codes(newPath, oldPath);
console.log("--- notes by a device's code (the new worker; the 0.4.1 one's refusal) ---");
console.log(codeProblems.length === 0 ? "codes: as they should be" : "CODES: " + codeProblems.join("\nCODES: "));
process.exit(same && framedProblems.length === 0 && splitProblems.length === 0 && guardProblems.length === 0 &&
             takeProblems.length === 0 && codeProblems.length === 0 ? 0 : 1);
