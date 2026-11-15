// The relay's health counts (0.5.1, GET /health and /health.json; docs/ONLINE_RELAY.md, "The relay's
// health"): rooms, joins, refusals, reconnects, drops, guards and errors played through the worker's front door
// against a mock of the Durable Object runtime (Rooms and one real Lobby on a mock storage), then the report
// read back and checked count by count. Also: nothing personal in what's kept or shown (no room code, name,
// address or ticket), the 14 days kept (older ones let go), a room's counts sent on their own at most once a
// minute and charged in the day's meter, and /health rate-limited like the room list.
//
//   node health.mjs new.mjs        (run_compat.sh runs it on this folder's worker)
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
    this.received = [];
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
    this[0] = new MockWS();
    this[1] = new MockWS();
  }
};

function makeStorage() {
  const store = new Map();
  const copy = (v) => (v === undefined ? undefined : structuredClone(v));
  return {
    store,
    get: async (k) =>
      Array.isArray(k) ? new Map(k.filter((x) => store.has(x)).map((x) => [x, copy(store.get(x))])) : copy(store.get(k)),
    put: async (k, v) => void store.set(k, copy(v)),
    delete: async (k) => (Array.isArray(k) ? k.filter((x) => store.delete(x)).length : store.delete(k)),
    list: async ({ prefix = "" } = {}) =>
      new Map([...store.keys()].filter((k) => k.startsWith(prefix)).sort().map((k) => [k, copy(store.get(k))])),
  };
}

const mod = await import(pathToFileURL(process.argv[2]).href + "?" + Math.random());
const problems = [];
const check = (ok, what) => {
  if (!ok) {
    problems.push(what);
  }
};

// a clock the test moves (the rooms' connection times, the flood's seconds, the once-a-minute sending)
const realNow = Date.now;
let clock = realNow();
Date.now = () => clock;
const minutes = (n) => {
  clock += n * 60e3;
};

const env = { RELAY_DAY_REQUESTS: "1000" };
const lobbyStorage = makeStorage();
const lobby = new mod.Lobby({ id: { toString: () => "lobby" }, storage: lobbyStorage }, env);
const lobbyAsked = { count: 0, report: 0 };
env.LOBBY = {
  idFromName: (n) => n,
  get: () => ({
    fetch: async (url, init) => {
      const req = url instanceof Request ? url : new Request(url, init);
      const path = new URL(req.url).pathname;
      if (path === "/count") {
        lobbyAsked.count++;
      }
      if (path === "/report") {
        lobbyAsked.report++;
      }
      return lobby.fetch(req);
    },
  }),
};
const rooms = new Map(); // code -> { room, sockets }
env.ROOMS = {
  idFromName: (n) => n,
  get: (id) => ({
    fetch: async (req) => {
      if (!rooms.has(id)) {
        const sockets = [];
        const ctx = {
          id: { toString: () => "room-" + id },
          acceptWebSocket(ws, tags) {
            ws.tags = tags;
            sockets.push(ws);
          },
          getWebSockets(tag) {
            return sockets.filter((ws) => ws.tags.includes(tag) && !ws.gone);
          },
        };
        rooms.set(id, { room: new mod.Room(ctx, env), sockets });
      }
      return rooms.get(id).room.fetch(req);
    },
  }),
};

// --- old days: one 20 days back (let go when today's is made), one 5 days back (shown) ---
const dayOf = (ago) => new Date(realNow() - ago * 86400e3).toISOString().slice(0, 10);
lobbyStorage.store.set("h:" + dayOf(20), { c: { "rooms.opened": 9 }, peak: { players: 9, rooms: 9 }, live: { players: 0, rooms: 0 } });
lobbyStorage.store.set("h:" + dayOf(5), { c: { "rooms.opened": 3, "join.host": 3 }, peak: { players: 4, rooms: 2 }, live: { players: 0, rooms: 0 } });

let ipN = 0;
const front = async (path, { ws = true, ip = null } = {}) => {
  const headers = { "CF-Connecting-IP": ip || "203.0.113." + (++ipN % 250) };
  if (ws) {
    headers.Upgrade = "websocket";
  }
  return mod.default.fetch(new Request("https://relay" + path, { headers }), env);
};
// a player's connection: its server side, or the refusal ("<status> <words>")
const open = async (code, query, ip) => {
  const res = await front(`/room/${code}?${query}`, { ip });
  if (res.status !== 101) {
    return res.status + " " + String(res.body).trim();
  }
  const s = rooms.get(code).sockets;
  return s[s.length - 1];
};
const ticketOf = (ws) => (ws.received.find((m) => m.startsWith("T ticket ")) || "").slice(9);
const roomOf = (code) => rooms.get(code).room;
const leave = async (code, ws, closeCode) => {
  ws.gone = true;
  await roomOf(code).webSocketClose(ws, closeCode);
};

// --- the front door's refusals (counted in the Worker, carried on its next request to a room) ---
await front("/room/bad!?role=guest");
await front("/room/ABCD?role=nobody");
await front("/room/ABCD?role=guest", { ws: false });

// --- a listed lockstep room (0.4.x): host, guest, a seat, a watcher; a second host and guest refused ---
const aHost = await open("AAAA", "role=host&list=QX9M&name=Bob%20B&game=G8ME01&v=0.4.1&max=4");
const aGuest = await open("AAAA", "role=guest");
const aSeat = await open("AAAA", "role=seat");
const aWatch = await open("AAAA", "role=watch");
check(typeof (await open("AAAA", "role=host")) === "string", "a second host was let in");
check(typeof (await open("AAAA", "role=guest")) === "string", "a second guest was let in");

// --- a 0.5.1 sync room, private: host (framed, caps), guest, two peers, a third refused ---
const bHost = await open("BBBB", "role=host&framed=1&caps=1");
const bGuest = await open("BBBB", "role=guest");
const bPeer0 = await open("BBBB", "role=peer");
const bPeer1 = await open("BBBB", "role=peer");
check(typeof (await open("BBBB", "role=peer")) === "string", "a third peer was let in");
for (const ws of [aHost, aGuest, aSeat, aWatch, bHost, bGuest, bPeer0, bPeer1]) {
  if (typeof ws === "string") {
    problems.push("a connection was refused: " + ws);
  }
}
if (problems.length > 0) {
  Date.now = realNow;
  console.log("HEALTH: " + problems.join("\nHEALTH: "));
  process.exit(1);
}

// the guest drops (1006) and comes back with its ticket; then its ticket again while that one's still there
minutes(2);
const bTicket = ticketOf(bGuest);
await leave("BBBB", bGuest, 1006);
const bGuest2 = await open("BBBB", "role=guest&ticket=" + bTicket);
const bGuest3 = await open("BBBB", "role=guest&ticket=" + bTicket);
check(typeof bGuest2 === "object" && typeof bGuest3 === "object", "a reconnect was refused");
check(bGuest2.closed && bGuest2.closed.code === 4000, "the replaced connection wasn't closed 4000");
await leave("BBBB", bGuest2, 1006); // (the replaced one's close comes later: counted once, as replaced)

// the host removes peer 0; its ticket back is refused; peer 1 floods (1001 in a second) and is closed
const p0Ticket = ticketOf(bPeer0);
await roomOf("BBBB").webSocketMessage(bHost, "drop peer 0");
await leave("BBBB", bPeer0, 4001);
check(typeof (await open("BBBB", "role=peer&ticket=" + p0Ticket)) === "string", "a removed peer's ticket was let in");
for (let i = 0; i < 1005 && !bPeer1.closed; i++) {
  await roomOf("BBBB").webSocketMessage(bPeer1, new Uint8Array([1, 0, 0, 0, 7]).buffer);
}
check(bPeer1.closed && bPeer1.closed.code === 1008, "the flooding peer wasn't closed");
await roomOf("BBBB").webSocketMessage(bPeer1, new Uint8Array([1, 0, 0, 0, 7]).buffer); // (one more on its way)
await leave("BBBB", bPeer1, 1008);

// --- a room with nobody hosting: refused twice in a minute (counts sent on their own once), and a stale
// ticket (expired); a minute on, once more (sent then) ---
const countsBefore = lobbyAsked.count;
const unitsBefore = (await lobbyStorage.get("d:" + new Date().toISOString().slice(0, 10))) || 0;
await open("CCCC", "role=guest");
await open("CCCC", "role=guest&ticket=" + "ab".repeat(16));
check(lobbyAsked.count === countsBefore + 1, `nobody-hosting refusals: ${lobbyAsked.count - countsBefore} sends, not 1`);
minutes(1.5);
await open("CCCC", "role=watch");
check(lobbyAsked.count === countsBefore + 2, `a minute on: ${lobbyAsked.count - countsBefore} sends, not 2`);
const unitsAfter = (await lobbyStorage.get("d:" + new Date().toISOString().slice(0, 10))) || 0;
check(unitsAfter - unitsBefore === 40, `the counts' own sends in the day's meter: ${unitsAfter - unitsBefore} units, not 40`);

// --- the day's budget: a caps host told busy past 80%, turned away past 95% ---
await lobbyStorage.put("d:" + new Date().toISOString().slice(0, 10), 850 * 20);
const dHost = await open("DDDD", "role=host&framed=1&caps=1");
check(typeof dHost === "object" && dHost.received.includes("T busy"), "a caps host at 85% wasn't told busy");
await lobbyStorage.put("d:" + new Date().toISOString().slice(0, 10), 960 * 20);
check(String(await open("EEEE", "role=host&framed=1&caps=1")).startsWith("429"), "a caps host at 96% wasn't turned away");
await lobbyStorage.put("d:" + new Date().toISOString().slice(0, 10), unitsAfter);

// --- an error in a room's handler (a connection with no attachment) and in the Worker (no ROOMS) ---
const orphan = new MockWS();
let threw = false;
try {
  await roomOf("AAAA").webSocketMessage(orphan, new Uint8Array([1, 2, 3]).buffer);
} catch {
  threw = true;
}
check(threw, "the room's error was swallowed");
const rooms0 = env.ROOMS;
env.ROOMS = undefined;
threw = false;
try {
  await front("/room/FFFF?role=host");
} catch {
  threw = true;
}
check(threw, "the Worker's error was swallowed");
env.ROOMS = rooms0;

// --- the lockstep room's goings: the guest after 3 minutes (clean), the seat drops, the watcher's page goes,
// the host after 20 minutes; the sync room's host after 4 hours ---
minutes(3 - 3.5);
await leave("AAAA", aGuest, 1000);
minutes(17);
await leave("AAAA", aSeat, 1006);
await leave("AAAA", aWatch, 1001);
await leave("AAAA", aHost, 1005);
minutes(220);
await leave("BBBB", bGuest3, 1000);
await leave("BBBB", bHost, 1000);
await leave("DDDD", dHost, 4999);

// --- /health: rate-limited like the room list (120 a minute an address), then read ---
let slowAt = 0;
for (let i = 1; i <= 125 && slowAt === 0; i++) {
  const res = await front("/health", { ws: false, ip: "192.0.2.77" });
  if (res.status === 403) {
    slowAt = i;
  }
}
check(slowAt === 121, `/health slowed at request ${slowAt}, not 121`);
const textRes = await front("/health", { ws: false });
const jsonRes = await front("/health.json", { ws: false });
check(textRes.status === 200 && jsonRes.status === 200, "/health didn't answer 200");
check((await front("/health", { ws: false, ip: null })).status === 200, "/health twice");
const text = String(textRes.body);
const report = JSON.parse(String(jsonRes.body));
Date.now = realNow;

const day = report.days[0].counts;
const want = {
  rooms: { opened: 3, listed: 1, private: 2, sync: 2, lockstep: 1, split: 2 },
  join: { host: 3, guest: 2, seat: 1, peer: 2, watch: 1 },
  refused: { no_host: 3, host_taken: 1, full: 2, watch_full: 0, today: 1, removed: 1, blocked: 0, slow_room: 0,
             bad_code: 1, bad_role: 1, not_websocket: 1 },
  resume: { asked: 4, ok: 2, took_over: 1, expired: 1, refused: 1 },
  end: { clean: 4, away: 1, dropped: 2, replaced: 1, removed: 1, blocked: 0, flood: 1, too_long: 0, bad_split: 0, other: 1 },
  guard: { slow_http: 1, flood: 1, too_long: 0, bad_split: 0, drop: 1, drop_block: 0, busy_told: 1 },
  error: { worker: 1, room_open: 0, room_message: 1, room_close: 0, lobby: 0 },
  // player connections (watchers aside): the replaced one <1 min; the dropped guest, the removed peer, the
  // flooder (2 min) and the lockstep guest (3) 1-5; the seat and host (20) 15-60; the rest past 3 h
  session: { "0_1m": 1, "1_5m": 4, "5_15m": 0, "15_60m": 2, "1_3h": 0, "3h_plus": 3 },
  life: { "0_1m": 0, "1_5m": 0, "5_15m": 0, "15_60m": 1, "1_3h": 0, "3h_plus": 2 },
};
for (const [g, names] of Object.entries(want)) {
  for (const [n, v] of Object.entries(names)) {
    check(day[g] && day[g][n] === v, `${g}.${n}: ${day[g] ? day[g][n] : "?"}, not ${v}`);
  }
}
check(report.days[0].peak.players === 8 && report.days[0].peak.rooms === 3,
      `the day's peak: ${JSON.stringify(report.days[0].peak)}, not 8 players and 3 rooms`);
check(report.now.players === 0 && report.now.rooms === 0, "now: " + JSON.stringify(report.now) + ", not empty");
check(report.days.length === 14, `${report.days.length} days, not 14`);
check(report.days[5].counts.rooms.opened === 3 && report.days[5].peak.players === 4, "5 days ago's wasn't kept");
check(!lobbyStorage.store.has("h:" + dayOf(20)), "20 days ago's wasn't let go");
check(text.startsWith("Paper Cut relay: health") && text.includes("now: 0 players in 0 rooms") &&
      text.includes("rooms: 3 opened (1 listed, 2 private; 2 sync, 1 lockstep; 2 split)") &&
      text.includes("reconnects: 4 asked: 2 resumed") && text.split("\n").filter((l) => l === "  nothing").length === 12,
      "the text report:\n" + text);
// nothing personal: no code, name, address or ticket kept or shown
const kept = JSON.stringify([...lobbyStorage.store].filter(([k]) => k.startsWith("h:")));
for (const secret of ["AAAA", "QX9M", "BBBB", "CCCC", "Bob", "203.0.113", "192.0.2", bTicket, p0Ticket]) {
  check(!kept.includes(secret) && !text.includes(secret) && !String(jsonRes.body).includes(secret),
        "something personal kept or shown: " + secret);
}
check(lobbyAsked.report > 0, "the rooms never reported");
if (process.argv.includes("--print")) {
  console.log(text);
}

console.log(`health: ${Object.values(want).reduce((n, g) => n + Object.keys(g).length, 0)} counts, the peak, 14 days, ` +
            "nothing personal, " + (problems.length === 0 ? "as they should be" : "PROBLEMS"));
if (problems.length > 0) {
  console.log("HEALTH: " + problems.join("\nHEALTH: "));
  console.log(text);
  process.exit(1);
}
