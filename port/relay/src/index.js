// Paper Cut's online relay: one room per code, two players in it, each one's
// bytes passed to the other unchanged (port/src/common/online_net_pc.c's
// stream), and up to MAX_WATCHERS watching. A Cloudflare Worker and a Durable
// Object per room, on the free plan (docs/ONLINE_RELAY.md):
// - the WebSocket hibernation API: a room waiting for its guest costs nothing;
// - only incoming messages count (20 to 1), and the games batch their input;
// - the games try a direct connection first, from the addresses passed here,
//   and leave the relay when it works;
// - a watcher costs the host's one message every 30 frames, however many
//   watch: the relay copies it to each of them (messages out are free).
//
//   GET /room/<CODE>?role=host|guest|watch[&ticket=<T>]  (a WebSocket upgrade)
//       a host's, to list its room: &list=<SECRET>&name=<NAME>&game=<ID>&v=<VERSION>
//   GET /rooms?game=<ID>&v=<VERSION>  the room list (text, below)
//   GET|POST /notes?area=<AAA>         paper notes (the Notes class, at the end)
//   GET|POST /board, GET /replay, POST /check ?board=&game=&v=
//                                      leaderboards (the Board class, at the end)
//   GET /bingo/<ROOM>?...  a lockout bingo room (the Bingo class, at the end)
//   GET /daily/<YYYYMMDD>[?...]  a day's results board (the Daily class, at the end)
//
// The room list (the Start menu's Online tab, "List my room"): a host can ask
// for its room to be listed, open to anyone; it gives the relay its code's
// secret half for that (the listing is how others join it). A listed room is
// shown while its host is there and nobody has joined; the host says "list"
// (text) every 10 minutes, and a listing not refreshed for LIST_TTL_MS is
// dropped. Every room also tells the Lobby (one Durable Object) how many are
// in it, for the list's "players online": no code or address, for a room
// that isn't listed. The list is fetched when a player opens it or asks
// again, never polled:
//
//   rooms <PLAYERS ONLINE>
//   <CODE> <IN THE ROOM> <NAME, percent-encoded>     (each listed room, newest first)
//
// Text messages from the relay, never forwarded: "you <address>" (the
// player's public address, as Cloudflare sees it), "peer" (the other player
// is in the room), "gone" (they left), "ticket <T>" (this connection's place:
// given back after a drop, the relay lets the new connection take it, closing
// the old one it may still think is there), "watch <N>" (to the host: how
// many watch). Binary messages are the games' own: the host's that start with
// four 0xFF bytes go, without them, to the watchers; its others, to the guest;
// the guest's, to the host; a watcher's, nowhere.

const CODE = /^[0-9A-Z]{4,8}$/;
const TICKET = /^[0-9a-f]{32}$/;
const MAX_MESSAGE = 1 << 20;        // the host's setup (settings and memory card) is the largest
const ROOM_LIFETIME_MS = 6 * 3600e3; // a session longer than this starts again from the lobby
const MAX_WATCHERS = 4;
const ROLES = ["host", "guest", "watch"];
const SECRET = /^[0-9A-Z]{4}$/;
const GAME = /^[0-9A-Z]{6}$/;
const VERSION = /^[0-9A-Za-z.+-]{1,24}$/;
const LIST_TTL_MS = 20 * 60e3; // a listing the host hasn't refreshed (it does every 10 minutes)
const MAX_LISTED = 40;         // shown in the list at most
const MAX_NAME = 24;

// a listing's name: printable, on one line, MAX_NAME characters at most
function cleanName(name) {
  return [...(name || "")].filter((c) => c >= " " && c !== "\x7f").slice(0, MAX_NAME).join("").trim();
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const m = url.pathname.match(/^\/room\/([^/]+)$/);
    if (url.pathname === "/" || url.pathname === "/health") {
      return new Response("Paper Cut relay\n", { headers: { "content-type": "text/plain" } });
    }
    if (url.pathname === "/rooms") {
      return env.LOBBY.get(env.LOBBY.idFromName("lobby")).fetch(request);
    }
    const bingo = url.pathname.match(/^\/bingo\/([0-9A-Z]{4,8})$/);
    if (bingo) {
      return env.BINGO.get(env.BINGO.idFromName(bingo[1])).fetch(request);
    }
    if (url.pathname === "/notes") {
      const area = url.searchParams.get("area") || "";
      if (!AREA.test(area)) {
        return new Response("area: a map's first three letters\n", { status: 400 });
      }
      return env.NOTES.get(env.NOTES.idFromName("notes:" + area)).fetch(request);
    }
    if (url.pathname === "/board" || url.pathname === "/replay" || url.pathname === "/check") {
      const board = url.searchParams.get("board") || "";
      const game = url.searchParams.get("game") || "";
      const v = url.searchParams.get("v") || "";
      if (!BOARD.test(board) || !GAME.test(game) || !VERSION.test(v)) {
        return new Response("board, game and v, please\n", { status: 400 });
      }
      return env.BOARDS.get(env.BOARDS.idFromName("board:" + board + ":" + game + ":" + v)).fetch(request);
    }
    const daily = url.pathname.match(/^\/daily\/(\d{8})$/);
    if (daily) {
      return env.DAILY.get(env.DAILY.idFromName(daily[1])).fetch(request);
    }
    if (!m || !CODE.test(m[1])) {
      return new Response("Not a room\n", { status: 404 });
    }
    const role = url.searchParams.get("role");
    if (!ROLES.includes(role)) {
      return new Response("role: host, guest or watch\n", { status: 400 });
    }
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("A WebSocket upgrade, please\n", { status: 426 });
    }
    const room = env.ROOMS.get(env.ROOMS.idFromName(m[1]));
    return room.fetch(request);
  },
};

function newTicket() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function isWatchMessage(message) {
  if (message.byteLength < 4) {
    return false;
  }
  const b = new Uint8Array(message, 0, 4);
  return b[0] === 0xff && b[1] === 0xff && b[2] === 0xff && b[3] === 0xff;
}

export class Room {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  // the room's connections in a role, without those replaced by a reconnect
  here(role) {
    return this.ctx.getWebSockets(role).filter((ws) => {
      const a = ws.deserializeAttachment();
      return a && !a.replaced;
    });
  }

  // the Lobby told how many are here, and the listing while the host waits alone
  async report() {
    const hosts = this.here("host");
    const players = hosts.length + this.here("guest").length + this.here("watch").length;
    const a = hosts.length > 0 ? hosts[0].deserializeAttachment() : null;
    const list = a && a.list && this.here("guest").length === 0 ? a.list : null;
    try {
      await this.env.LOBBY.get(this.env.LOBBY.idFromName("lobby")).fetch("https://lobby/report", {
        method: "POST",
        body: JSON.stringify({ id: this.ctx.id.toString(), players, list }),
      });
    } catch {
      // the list is a nicety: the room works without it
    }
  }

  // the host told how many watch
  tellWatchers(host) {
    const n = this.here("watch").length;
    for (const h of host ? [host] : this.here("host")) {
      try {
        h.send("watch " + n);
      } catch {
        // closing
      }
    }
  }

  async fetch(request) {
    const params = new URL(request.url).searchParams;
    const role = params.get("role");
    const ticket = params.get("ticket");
    const reconnect = role !== "watch" && ticket !== null && TICKET.test(ticket);
    let here = this.here(role);
    // a reconnect: its ticket gives it the place of the connection it had
    if (reconnect) {
      for (const old of here) {
        const a = old.deserializeAttachment();
        if (a.ticket === ticket) {
          old.serializeAttachment({ ...a, replaced: true });
          try {
            old.close(4000, "Replaced");
          } catch {
            // already closed
          }
        }
      }
      here = this.here(role);
    }
    if (role === "host" && here.length > 0) {
      return new Response("That room has a host\n", { status: 409 });
    }
    if (role === "guest" || role === "watch") {
      if (this.here("host").length === 0) {
        return new Response("No one is hosting that room\n", { status: 404 });
      }
      if (role === "guest" && here.length > 0) {
        return new Response("That room is full\n", { status: 409 });
      }
      if (role === "watch" && here.length >= MAX_WATCHERS) {
        return new Response("As many are watching that room as can\n", { status: 409 });
      }
    }
    const pair = new WebSocketPair();
    const mine = role === "watch" ? null : reconnect ? ticket : newTicket();
    // a host's listing: the whole code, its name, the game and version it plays
    let list = null;
    const secret = params.get("list");
    if (role === "host" && secret !== null && SECRET.test(secret) && GAME.test(params.get("game") || "") &&
        VERSION.test(params.get("v") || "")) {
      const half = new URL(request.url).pathname.split("/").pop();
      list = { code: half + secret, name: cleanName(params.get("name")), game: params.get("game"),
               v: params.get("v") };
    }
    this.ctx.acceptWebSocket(pair[1], [role]);
    pair[1].serializeAttachment({ role, since: Date.now(), ticket: mine, list });
    if (role === "watch") {
      this.tellWatchers(null);
      await this.report();
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    const address = request.headers.get("CF-Connecting-IP");
    if (address) {
      pair[1].send("you " + address);
    }
    pair[1].send("ticket " + mine);
    if (role === "host" && this.here("watch").length > 0) {
      this.tellWatchers(pair[1]);
    }
    const other = this.here(role === "host" ? "guest" : "host");
    if (other.length > 0) {
      pair[1].send("peer");
      other[0].send("peer");
    }
    await this.report();
    return new Response(null, { status: 101, webSocket: pair[0] });
  }

  async webSocketMessage(ws, message) {
    if (typeof message === "string") {
      // a listed host's refresh ("list"); the games send nothing else as text
      const a = ws.deserializeAttachment();
      if (message === "list" && a && a.role === "host" && !a.replaced && a.list) {
        await this.report();
      }
      return;
    }
    const { role, since, replaced } = ws.deserializeAttachment();
    if (replaced || role === "watch") {
      return; // a watcher's go nowhere (it sends none)
    }
    if (message.byteLength > MAX_MESSAGE || Date.now() - since > ROOM_LIFETIME_MS) {
      ws.close(1009, "Too long");
      return;
    }
    if (role === "host" && isWatchMessage(message)) {
      const body = message.slice(4);
      for (const w of this.here("watch")) {
        try {
          w.send(body);
        } catch {
          // closing
        }
      }
      return;
    }
    const other = this.here(role === "host" ? "guest" : "host");
    if (other.length > 0) {
      other[0].send(message);
    }
  }

  async webSocketClose(ws, code) {
    const a = ws.deserializeAttachment();
    const { role, replaced } = a;
    try {
      ws.serializeAttachment({ ...a, replaced: true }); // (no longer counted while it closes)
    } catch {
      // already closed
    }
    try {
      ws.close(code === 1005 ? 1000 : code, "Bye");
    } catch {
      // already closed
    }
    if (replaced) {
      return; // its place is the new connection's: nobody left
    }
    await this.report();
    if (role === "watch") {
      this.tellWatchers(null);
      return;
    }
    const others = this.here(role === "host" ? "guest" : "host");
    const told = role === "host" ? [...others, ...this.here("watch")] : others;
    for (const other of told) {
      try {
        other.send("gone");
      } catch {
        // closing
      }
    }
  }

  async webSocketError(ws) {
    await this.webSocketClose(ws, 1011);
  }
}

// The room list's one Durable Object: each room's count (by the room's id) and
// listing, in its storage (it may be evicted between requests). Rooms that
// stopped telling it are dropped as the list is read: a listing after
// LIST_TTL_MS without a word, a count after ROOM_LIFETIME_MS.
export class Lobby {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const now = Date.now();
    if (request.method === "POST" && url.pathname === "/report") {
      let r;
      try {
        r = await request.json();
      } catch {
        return new Response("Bad report\n", { status: 400 });
      }
      if (!r || typeof r.id !== "string" || r.id.length > 80) {
        return new Response("Bad report\n", { status: 400 });
      }
      if (!(r.players > 0)) {
        await this.ctx.storage.delete("r:" + r.id);
      } else {
        const old = (await this.ctx.storage.get("r:" + r.id)) || {};
        const list = r.list || null;
        await this.ctx.storage.put("r:" + r.id, {
          players: Math.min(Number(r.players) || 0, 2 + MAX_WATCHERS),
          list,
          listedAt: list ? (old.list && old.list.code === list.code ? old.listedAt : now) : 0,
          at: now,
        });
      }
      return new Response("ok\n");
    }
    if (url.pathname !== "/rooms") {
      return new Response("Not here\n", { status: 404 });
    }
    const game = url.searchParams.get("game") || "";
    const v = url.searchParams.get("v") || "";
    const rooms = await this.ctx.storage.list({ prefix: "r:" });
    const stale = [];
    const listed = [];
    let online = 0;
    for (const [key, r] of rooms) {
      if (now - r.at > ROOM_LIFETIME_MS) {
        stale.push(key);
        continue;
      }
      online += r.players;
      if (r.list && now - r.at <= LIST_TTL_MS && r.list.game === game && r.list.v === v) {
        listed.push(r);
      }
    }
    if (stale.length > 0) {
      await this.ctx.storage.delete(stale.slice(0, 128));
    }
    listed.sort((a, b) => b.listedAt - a.listedAt);
    let text = "rooms " + online + "\n";
    for (const r of listed.slice(0, MAX_LISTED)) {
      text += r.list.code + " " + r.players + " " + encodeURIComponent(r.list.name || "") + "\n";
    }
    return new Response(text, {
      headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" },
    });
  }
}

// --- paper notes and leaderboards (0.4.0) ---
//
// Both are small stores in Durable Objects' storage, sized for the free plan:
// a Durable Object per area's notes and per leaderboard, each read when a
// player opens the page or enters the area (never polled). Who sent what is
// kept only as a hash of the address with HASH_SALT (a secret, optional:
// `npx wrangler secret put HASH_SALT`), for the daily limits and one vote each.

const AREA = /^[a-z]{3}$/;
const NOTE_MAP = /^[a-z]{3}_[0-9a-z_]{1,13}$/;
const NOTE_ID = /^[0-9a-f]{12}$/;
const NOTES_PER_MAP = 30;   // kept for a map: past it, the lowest-rated older one goes
const NOTES_SHOWN = 8;      // a map's best, sent to the games
const NOTES_MINE_A_MAP = 3; // one address's on a map
const NOTE_ADDS_A_DAY = 10; // an address's, in an area
const NOTE_RATES_A_DAY = 60;
const NOTE_DROP_AT = -3;    // rated down to this: gone
const NOTE_COORD = 20000;

const BOARD = /^(bossrush-mid|bossrush-late|pit100)(-50)?$/; // -50: the EU release at 50 Hz
const BOARD_ENTRIES = 10;
const MAX_REPLAY = 1 << 20; // a compressed replay (zlib)
const CHUNK = 96 * 1024;    // a storage value's piece (the key-value API keeps values under 128 KiB)
const UPLOADS_A_DAY = 5;
const CHECKS_A_DAY = 50;

const TEXT = { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" };

function say(text, status = 200) {
  return new Response(text + "\n", { status, headers: TEXT });
}

async function addressHash(request, env) {
  const ip = request.headers.get("CF-Connecting-IP") || "?";
  const data = new TextEncoder().encode((env.HASH_SALT || "papercut") + ":" + ip);
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", data));
  return [...digest.slice(0, 8)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// one more of an address's daily allowance of kind; false when it's spent (resets at 00:00 UTC)
async function spend(storage, kind, hash, max) {
  const day = new Date().toISOString().slice(0, 10);
  let q = (await storage.get("quota")) || { day, n: {} };
  if (q.day !== day) {
    q = { day, n: {} };
  }
  const key = kind + ":" + hash;
  if ((q.n[key] || 0) >= max) {
    return false;
  }
  q.n[key] = (q.n[key] || 0) + 1;
  await storage.put("quota", q);
  return true;
}

function int(text, lo, hi) {
  if (!/^-?[0-9]{1,10}$/.test(text || "")) {
    return null;
  }
  const n = Number(text);
  return n >= lo && n <= hi ? n : null;
}

// Paper notes: an area's (a map's first three letters) in one Durable Object.
// A note is a set phrase and a word, both numbers (the games have the text, in
// every language), and a place on a map; never free text.
//
//   GET /notes?area=<AAA>   "notes <N>", then a line a note, each map's best first:
//                           <ID> <MAP> <X> <Y> <Z> <PHRASE> <WORD> <SCORE>
//   POST /notes?area=<AAA>  "add <MAP> <X> <Y> <Z> <PHRASE> <WORD>" -> "ok <ID>"
//                           "rate <MAP> <ID> <1|-1>"                 -> "ok <SCORE>"
// A refusal is a status other than 200 and "<WHY>: <words>" (mine, today, own,
// rated, gone).
export class Notes {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const area = url.searchParams.get("area");
    const storage = this.ctx.storage;
    if (request.method === "GET") {
      const maps = await storage.list({ prefix: "m:" });
      const lines = [];
      for (const [key, notes] of maps) {
        const best = [...notes].sort((a, b) => b.s - a.s || b.at - a.at).slice(0, NOTES_SHOWN);
        for (const n of best) {
          lines.push([n.id, key.slice(2), n.x, n.y, n.z, n.t, n.w, n.s].join(" "));
        }
      }
      return say("notes " + lines.length + (lines.length > 0 ? "\n" + lines.join("\n") : ""));
    }
    if (request.method !== "POST") {
      return say("GET or POST", 405);
    }
    const body = (await request.text()).slice(0, 200).trim().split(/\s+/);
    const hash = await addressHash(request, this.env);
    const map = body[1] || "";
    if (!NOTE_MAP.test(map) || !map.startsWith(area + "_")) {
      return say("bad: not a map of this area", 400);
    }
    const notes = (await storage.get("m:" + map)) || [];
    if (body[0] === "add" && body.length === 7) {
      const [x, y, z] = [body[2], body[3], body[4]].map((t) => int(t, -NOTE_COORD, NOTE_COORD));
      const t = int(body[5], 0, 255);
      const w = int(body[6], 0, 1023);
      if (x === null || y === null || z === null || t === null || w === null) {
        return say("bad: not a note", 400);
      }
      if (notes.filter((n) => n.by === hash).length >= NOTES_MINE_A_MAP) {
        return say("mine: you have as many notes here as can be", 409);
      }
      if (!(await spend(storage, "add", hash, NOTE_ADDS_A_DAY))) {
        return say("today: no more notes in this area today", 429);
      }
      const id = newTicket().slice(0, 12);
      notes.push({ id, x, y, z, t, w, s: 0, at: Date.now(), by: hash });
      while (notes.length > NOTES_PER_MAP) {
        // the lowest-rated (the oldest of those), never the one just left
        let worst = 0;
        for (let i = 1; i < notes.length - 1; i++) {
          if (notes[i].s < notes[worst].s || (notes[i].s === notes[worst].s && notes[i].at < notes[worst].at)) {
            worst = i;
          }
        }
        await storage.delete("v:" + notes[worst].id);
        notes.splice(worst, 1);
      }
      await storage.put("m:" + map, notes);
      return say("ok " + id);
    }
    if (body[0] === "rate" && body.length === 4) {
      const d = body[3] === "1" || body[3] === "+1" ? 1 : body[3] === "-1" ? -1 : 0;
      if (!NOTE_ID.test(body[2]) || d === 0) {
        return say("bad: not a rating", 400);
      }
      const i = notes.findIndex((n) => n.id === body[2]);
      if (i < 0) {
        return say("gone: that note isn't here any more", 404);
      }
      if (notes[i].by === hash) {
        return say("own: you can't rate your own note", 409);
      }
      const voters = (await storage.get("v:" + notes[i].id)) || [];
      if (voters.includes(hash)) {
        return say("rated: you rated it already", 409);
      }
      if (!(await spend(storage, "rate", hash, NOTE_RATES_A_DAY))) {
        return say("today: no more ratings today", 429);
      }
      notes[i].s += d;
      const score = notes[i].s;
      if (score <= NOTE_DROP_AT) {
        await storage.delete("v:" + notes[i].id);
        notes.splice(i, 1);
      } else {
        voters.push(hash);
        await storage.put("v:" + notes[i].id, voters.slice(-500));
      }
      if (notes.length > 0) {
        await storage.put("m:" + map, notes);
      } else {
        await storage.delete("m:" + map);
      }
      return say("ok " + score);
    }
    return say("bad: add or rate", 400);
  }
}

// Leaderboards: one Durable Object per board, game (the disc's ID) and Paper
// Cut version, since a replay plays out the same only on the version that
// made it. Each entry is a run's time in frames and its replay (zlib), the
// best BOARD_ENTRIES kept, one per address. The relay can't play a replay:
// the uploading game checked its run by playing it back before sending, and
// each game that plays an entry back says whether it finished in the same
// frames (/check), counted once per address.
//
//   GET /board?board=&game=&v=   "board <N>", then a line an entry, fastest first:
//                                <ID> <FRAMES> <CHECKS> <FAILS> <DATE> <NAME, percent-encoded>
//   POST /board?...&frames=<N>&name=<NAME>   (body: the replay) -> "ok <ID> <RANK>"
//   GET /replay?...&id=<ID>      the replay
//   POST /check?...&id=<ID>      "<FRAMES>": the frames its playback here took -> "ok <CHECKS> <FAILS>"
// A refusal is a status other than 200 and "<WHY>: <words>" (slower, today,
// big, gone).
export class Board {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  async dropReplay(entry) {
    const keys = [];
    for (let i = 0; i < entry.chunks; i++) {
      keys.push("r:" + entry.id + ":" + i);
    }
    if (keys.length > 0) {
      await this.ctx.storage.delete(keys);
    }
  }

  async fetch(request) {
    const url = new URL(request.url);
    const storage = this.ctx.storage;
    const entries = (await storage.get("e")) || [];
    if (url.pathname === "/board" && request.method === "GET") {
      const lines = entries.map((e) =>
        [e.id, e.frames, e.checks.length, e.fails.length, e.date, encodeURIComponent(e.name)].join(" "));
      return say("board " + entries.length + (lines.length > 0 ? "\n" + lines.join("\n") : ""));
    }
    if (url.pathname === "/replay" && request.method === "GET") {
      const e = entries.find((x) => x.id === url.searchParams.get("id"));
      if (!e) {
        return say("gone: that run isn't on the board any more", 404);
      }
      const keys = [];
      for (let i = 0; i < e.chunks; i++) {
        keys.push("r:" + e.id + ":" + i);
      }
      const got = await storage.get(keys);
      const parts = keys.map((k) => got.get(k));
      if (parts.some((p) => !p)) {
        return say("broken: that run's replay is incomplete", 500);
      }
      const size = parts.reduce((n, p) => n + p.byteLength, 0);
      const out = new Uint8Array(size);
      let at = 0;
      for (const p of parts) {
        out.set(new Uint8Array(p), at);
        at += p.byteLength;
      }
      return new Response(out, { headers: { "content-type": "application/octet-stream", "cache-control": "no-store" } });
    }
    if (request.method !== "POST") {
      return say("Not here", 404);
    }
    const hash = await addressHash(request, this.env);
    if (url.pathname === "/check") {
      const e = entries.find((x) => x.id === url.searchParams.get("id"));
      if (!e) {
        return say("gone: that run isn't on the board any more", 404);
      }
      const frames = int((await request.text()).slice(0, 20).trim(), 1, 0x7fffffff);
      if (frames === null) {
        return say("bad: not a check", 400);
      }
      if (e.by !== hash && !e.checks.includes(hash) && !e.fails.includes(hash)) {
        if (!(await spend(storage, "check", hash, CHECKS_A_DAY))) {
          return say("today: no more checks today", 429);
        }
        (frames === e.frames ? e.checks : e.fails).push(hash);
        e.checks = e.checks.slice(-100);
        e.fails = e.fails.slice(-100);
        await storage.put("e", entries);
      }
      return say("ok " + e.checks.length + " " + e.fails.length);
    }
    if (url.pathname !== "/board") {
      return say("Not here", 404);
    }
    const frames = int(url.searchParams.get("frames"), 1, 0x7fffffff);
    const name = cleanName(url.searchParams.get("name")) || "?";
    if (frames === null) {
      return say("bad: frames, please", 400);
    }
    if (Number(request.headers.get("content-length") || "0") > MAX_REPLAY) {
      return say("big: that replay is larger than a board keeps", 413);
    }
    const replay = new Uint8Array(await request.arrayBuffer());
    if (replay.byteLength > MAX_REPLAY) {
      return say("big: that replay is larger than a board keeps", 413);
    }
    if (replay.byteLength < 16 || replay[0] !== 0x78) {
      return say("bad: not a replay", 400);
    }
    const mine = entries.find((e) => e.by === hash);
    if (mine && mine.frames <= frames) {
      return say("slower: your run on this board is as fast or faster", 409);
    }
    const others = entries.filter((e) => e !== mine);
    const rank = others.filter((e) => e.frames <= frames).length;
    if (rank >= BOARD_ENTRIES) {
      return say("slower: the board's runs are all faster", 409);
    }
    if (!(await spend(storage, "upload", hash, UPLOADS_A_DAY))) {
      return say("today: no more runs on this board today", 429);
    }
    const entry = {
      id: newTicket().slice(0, 12), frames, name, date: new Date().toISOString().slice(0, 10), by: hash,
      chunks: Math.ceil(replay.byteLength / CHUNK), checks: [], fails: [],
    };
    for (let i = 0; i < entry.chunks; i++) {
      await storage.put("r:" + entry.id + ":" + i, replay.slice(i * CHUNK, (i + 1) * CHUNK).buffer);
    }
    if (mine) {
      await this.dropReplay(mine);
    }
    others.splice(rank, 0, entry);
    for (const dropped of others.splice(BOARD_ENTRIES)) {
      await this.dropReplay(dropped);
    }
    await storage.put("e", others);
    return say("ok " + entry.id + " " + (rank + 1));
  }
}

// Bingo rooms (port/settings/features/bingo.cpp, docs/MODES.md): lockout
// bingo, each player on their own game. One Durable Object per room code; it
// keeps the room's card (the seed the first player brought), its players (by
// name, in the order they came) and who claimed each square first. Plain
// HTTPS GETs, asked every few seconds while a player has the room open:
//
//   GET /bingo/<ROOM>?name=<NAME>&seed=<SEED>[&claim=<CELL>,<CELL>...]
//
//   bingo <SEED>
//   you <I>
//   player <I> <NAME, percent-encoded>      (each player)
//   claim <CELL> <I>                        (each square claimed)
//
// A claim on a square already claimed changes nothing: the first stays. A room
// forgets everything BINGO_LIFETIME_MS after it was made.
const BINGO_SEED = /^[0-9A-Z_-]{1,24}$/;
const BINGO_PLAYERS = 8;
const BINGO_CELLS = 25;
const BINGO_LIFETIME_MS = 24 * 3600e3;

export class Bingo {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  async fetch(request) {
    const params = new URL(request.url).searchParams;
    const name = cleanName(params.get("name"));
    const seed = (params.get("seed") || "").toUpperCase();
    if (name === "") {
      return new Response("name, please\n", { status: 400 });
    }
    const now = Date.now();
    let room = (await this.ctx.storage.get("room")) || null;
    if (room && now - room.made > BINGO_LIFETIME_MS) {
      room = null;
    }
    if (!room) {
      if (!BINGO_SEED.test(seed)) {
        return new Response("seed, please\n", { status: 400 });
      }
      room = { made: now, seed, players: [], claims: {} };
    }
    let you = room.players.indexOf(name);
    if (you < 0) {
      if (room.players.length >= BINGO_PLAYERS) {
        return new Response("That room is full\n", { status: 409 });
      }
      room.players.push(name);
      you = room.players.length - 1;
    }
    for (const c of (params.get("claim") || "").split(",").slice(0, BINGO_CELLS)) {
      const cell = Number.parseInt(c, 10);
      if (cell >= 0 && cell < BINGO_CELLS && !(cell in room.claims)) {
        room.claims[cell] = you;
      }
    }
    await this.ctx.storage.put("room", room);
    let text = "bingo " + room.seed + "\nyou " + you + "\n";
    room.players.forEach((p, i) => {
      text += "player " + i + " " + encodeURIComponent(p) + "\n";
    });
    for (const [cell, who] of Object.entries(room.claims)) {
      text += "claim " + cell + " " + who + "\n";
    }
    return new Response(text, {
      headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" },
    });
  }
}

// The daily seed's results boards (port/settings/features/daily.cpp,
// docs/MODES.md): one Durable Object per day (UTC), the best time per name.
//
//   GET /daily/<YYYYMMDD>                                   the board
//   GET /daily/<YYYYMMDD>?name=<NAME>&ms=<MS>&game=<ID>&v=<VERSION>   a time sent
//
//   daily <YYYYMMDD> <FINISHED> [<THIS NAME'S RANK>]
//   <RANK> <MS> <NAME, percent-encoded> <GAME>             (the best DAILY_SHOWN)
//
// A time is taken only for today or the two days before (a run started late in
// the day), between DAILY_MIN_MS and DAILY_MAX_MS; a name keeps its best. One
// address may send under DAILY_NAMES_PER_ADDRESS names a day (kept as a hash
// salted with the day, never the address). A day's board is dropped
// DAILY_KEEP_MS after its first time.
const DAILY_SHOWN = 50;
const DAILY_MIN_MS = 5 * 60e3;
const DAILY_MAX_MS = 7 * 24 * 3600e3;
const DAILY_NAMES_PER_ADDRESS = 3;
const DAILY_KEEP_MS = 40 * 24 * 3600e3;

function dayNumber(yyyymmdd) {
  const y = Number(yyyymmdd.slice(0, 4)), m = Number(yyyymmdd.slice(4, 6)), d = Number(yyyymmdd.slice(6, 8));
  return Math.floor(Date.UTC(y, m - 1, d) / 86400e3);
}

async function addressHash(address, day) {
  const data = new TextEncoder().encode(day + "|" + (address || "?"));
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", data));
  return [...digest.slice(0, 12)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export class Daily {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const day = url.pathname.split("/").pop();
    const params = url.searchParams;
    let board = (await this.ctx.storage.get("board")) || { times: {}, addresses: {} };
    let rankOf = null;
    if (params.has("ms")) {
      const name = cleanName(params.get("name"));
      const ms = Number.parseInt(params.get("ms") || "", 10);
      const game = GAME.test(params.get("game") || "") ? params.get("game") : "?";
      const today = Math.floor(Date.now() / 86400e3);
      const age = today - dayNumber(day);
      if (name === "" || !(ms >= DAILY_MIN_MS && ms <= DAILY_MAX_MS) || age < 0 || age > 2) {
        return new Response("Not a time for this board\n", { status: 400 });
      }
      const who = await addressHash(request.headers.get("CF-Connecting-IP"), day);
      const names = board.addresses[who] || [];
      if (!names.includes(name)) {
        if (names.length >= DAILY_NAMES_PER_ADDRESS) {
          return new Response("Enough names from here today\n", { status: 429 });
        }
        names.push(name);
        board.addresses[who] = names;
      }
      const old = board.times[name];
      if (!old || ms < old.ms) {
        board.times[name] = { ms, game, at: Date.now() };
      }
      if ((await this.ctx.storage.getAlarm()) === null) {
        await this.ctx.storage.setAlarm(Date.now() + DAILY_KEEP_MS);
      }
      await this.ctx.storage.put("board", board);
      rankOf = name;
    }
    const sorted = Object.entries(board.times).sort((a, b) => a[1].ms - b[1].ms || a[1].at - b[1].at);
    let text = "daily " + day + " " + sorted.length;
    if (rankOf !== null) {
      text += " " + (sorted.findIndex(([n]) => n === rankOf) + 1);
    }
    text += "\n";
    sorted.slice(0, DAILY_SHOWN).forEach(([n, t], i) => {
      text += i + 1 + " " + t.ms + " " + encodeURIComponent(n) + " " + t.game + "\n";
    });
    return new Response(text, {
      headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" },
    });
  }

  async alarm() {
    await this.ctx.storage.deleteAll();
  }
}
