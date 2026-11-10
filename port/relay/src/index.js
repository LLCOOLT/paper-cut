// Paper Cut's online relay: one room per code, two players in it, each one's
// bytes passed to the other unchanged (port/src/common/online_net_pc.c's
// stream), up to MAX_SEATS more players (players 3 and 4: "seats") and up to
// MAX_WATCHERS watching. A Cloudflare Worker and a Durable Object per room, on
// the free plan (docs/ONLINE_RELAY.md):
// - the WebSocket hibernation API: a room waiting for its guest costs nothing;
// - only incoming messages count (20 to 1), and the games batch their input;
// - the games try a direct connection first, from the addresses passed here,
//   and leave the relay when it works;
// - a watcher costs the host's one message every 30 frames, however many
//   watch: the relay copies it to each of them (messages out are free).
//
//   GET /room/<CODE>?role=host|guest|seat|watch[&ticket=<T>]  (a WebSocket upgrade)
//       a host's, to list its room: &list=<SECRET>&name=<NAME>&game=<ID>&v=<VERSION>[&max=<2-4>]
//   GET /rooms?game=<ID>&v=<VERSION>  the room list (text, below)
//   GET|POST /notes?area=<AAA>         paper notes (the Notes class, at the end)
//   GET|POST /board, GET /replay, POST /check ?board=&game=&v=
//                                      leaderboards (the Board class, at the end)
//   GET /bingo/<ROOM>?...  a lockout bingo room (the Bingo class, at the end)
//   GET /daily/<YYYYMMDD>[?...]  a day's results board (the Daily class, at the end)
//   GET /raid/<WEEK>[?...]       a week's raid boss and its clears (the Raid class, at the end)
//
// The room list (the Start menu's Online tab, "List my room"): a host can ask
// for its room to be listed, open to anyone; it gives the relay its code's
// secret half for that (the listing is how others join it), and how many may
// play (max, 2 when it doesn't say). A listed room is shown while its host is
// there and its places aren't all taken (host, guest and seats); the host says "list"
// (text) every 10 minutes, and a listing not refreshed for LIST_TTL_MS is
// dropped. Every room also tells the Lobby (one Durable Object) how many are
// in it, for the list's "players online": no code or address, for a room
// that isn't listed. The list is fetched when a player opens it or asks
// again, never polled:
//
//   rooms <PLAYERS ONLINE>
//   <CODE> <PLAYING>/<MAX> <NAME, percent-encoded>     (each listed room, newest first: "1/4")
//
// Text messages from the relay, never forwarded: "you <address>" (the
// player's public address, as Cloudflare sees it), "peer" (the other player
// is in the room), "gone" (they left), "ticket <T>" (this connection's place:
// given back after a drop, the relay lets the new connection take it, closing
// the old one it may still think is there), "watch <N>" (to the host: how
// many watch), "seatgone <N>" (to the host: the seat in place N left),
// "peer <N>" / "peergone <N>" (to the host: the peer in place N came, left;
// a peer is told "peer" when the host's there, "gone" when it leaves). Binary
// messages are the games' own: the host's that start with four 0xFF bytes go,
// without them, to the watchers and the seats; its others, to the guest; the
// guest's, to the host; a seat's, to the host wrapped as one of the stream's
// messages (its length, the type 0xF0, the seat's place, the seat's message);
// a watcher's, nowhere.

const CODE = /^[0-9A-Z]{4,8}$/;
const TICKET = /^[0-9a-f]{32}$/;
const MAX_MESSAGE = 1 << 20;        // the host's setup (settings and memory card) is the largest
const ROOM_LIFETIME_MS = 6 * 3600e3; // a session longer than this starts again from the lobby
const MAX_WATCHERS = 4;
const MAX_SEATS = 2;           // players 3 and 4
const FROM_SEAT = 0xf0;        // the stream's message type a seat's message comes to the host in
// "Sync what matters" online (each player their own game: players 3 and 4 as "peers"): a peer's
// messages come to the host wrapped as one of the stream's (its length, FROM_PEER, the peer's
// place, the message); the host's that start with TO_PEER and a place go, without those two
// bytes, to that peer alone. Peers take their place again after a drop (tickets), as the guest.
const MAX_PEERS = 2;
const FROM_PEER = 0xf1;
const TO_PEER = 0xfe;
// A 0.5.0 host asks (&framed=1) for the guest's messages wrapped too, as one of the stream's messages
// of type FROM_GUEST (its length, 0xF2, the message): every message to that host is then framed by the
// relay, so no player's bytes can pass for another player's frames. The host is told "framed". A
// framed message (a peer's, or a framed host's guest's) is at most MAX_FRAMED bytes (the host's
// receive buffer, 1 MB, holds it with the relay's wrapping): a longer one closes its sender (1009).
const FROM_GUEST = 0xf2;
const MAX_FRAMED = (1 << 20) - 64;
const ROLES = ["host", "guest", "seat", "watch", "peer"];
const SECRET = /^[0-9A-Z]{4}$/;
const GAME = /^[0-9A-Z]{6}$/;
const VERSION = /^[0-9A-Za-z.+-]{1,24}$/;
const LIST_TTL_MS = 20 * 60e3; // a listing the host hasn't refreshed (it does every 10 minutes)
const MAX_LISTED = 40;         // shown in the list at most
const MAX_NAME = 24;
const MAX_URL = 2048;          // a request's address, query and all
const MAX_POST = 256;          // a text request's body (notes, checks)

// --- shared by every class ---
//
// Every refusal is a status other than 200 and "<WHY>: <words>" on one line
// (the games show their own text from the status or WHY). The room's refusals
// keep their words: the games look for "hosting" and "full" in them.

const TEXT = { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" };

function say(text, status = 200) {
  return new Response(text.endsWith("\n") ? text : text + "\n", { status, headers: TEXT });
}

// a name as shown: printable, on one line, max characters at most
function cleanName(name, max = MAX_NAME) {
  return [...(name || "")].filter((c) => c >= " " && c !== "\x7f").slice(0, max).join("").trim();
}

// a whole number in lo..hi from text with nothing else in it, or null
function int(text, lo, hi) {
  if (!/^-?[0-9]{1,10}$/.test(text || "")) {
    return null;
  }
  const n = Number(text);
  return n >= lo && n <= hi ? n : null;
}

function hex(bytes) {
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function newTicket() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return hex(bytes);
}

// Who sent something is kept only as a hash of the address with HASH_SALT (a
// secret, optional: `npx wrangler secret put HASH_SALT`) and a scope (a day's
// or week's board, so the hashes there can't be matched with another's).
async function addressHash(request, env, scope = "") {
  const ip = request.headers.get("CF-Connecting-IP") || "?";
  const text = (env.HASH_SALT || "papercut") + ":" + (scope ? scope + ":" : "") + ip;
  return hex(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text))).slice(0, 8));
}

// a text body of at most max bytes, or null when it's larger
async function readText(request, max = MAX_POST) {
  if (Number(request.headers.get("content-length") || "0") > max) {
    return null;
  }
  const text = await request.text();
  return text.length > max ? null : text;
}

// A Durable Object's class: its state and the Worker's environment.
class Stored {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }
}

// The browser build's page is another site than this one: its fetches may read the
// answers (CORS), and its POSTs ask first (a preflight). WebSockets aren't held to CORS.
const CORS = {
  "access-control-allow-origin": "*",
  "access-control-allow-methods": "GET, POST",
  "access-control-allow-headers": "content-type",
  "access-control-max-age": "86400",
};

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS });
    }
    const answer = await route(request, env);
    if (answer.status === 101 || answer.webSocket) {
      return answer;
    }
    const out = new Response(answer.body, answer);
    out.headers.set("access-control-allow-origin", "*");
    return out;
  },
};

async function route(request, env) {
    const url = new URL(request.url);
    if (request.url.length > MAX_URL) {
      return say("long: that request is too long", 414);
    }
    if (url.pathname === "/" || url.pathname === "/health") {
      return say("Paper Cut relay");
    }
    if (request.method !== "GET" && request.method !== "POST") {
      return say("method: GET or POST", 405);
    }
    if (url.pathname === "/rooms") {
      return env.LOBBY.get(env.LOBBY.idFromName("lobby")).fetch(request);
    }
    const bingo = url.pathname.match(/^\/bingo\/([^/]+)$/);
    if (bingo && CODE.test(bingo[1])) {
      return env.BINGO.get(env.BINGO.idFromName(bingo[1])).fetch(request);
    }
    if (url.pathname === "/notes") {
      if (!AREA.test(url.searchParams.get("area") || "")) {
        return say("bad: area, a map's first three letters", 400);
      }
      return env.NOTES.get(env.NOTES.idFromName("notes:" + url.searchParams.get("area"))).fetch(request);
    }
    if (url.pathname === "/board" || url.pathname === "/replay" || url.pathname === "/check") {
      const board = url.searchParams.get("board") || "";
      const game = url.searchParams.get("game") || "";
      const v = url.searchParams.get("v") || "";
      if (!BOARD.test(board) || !GAME.test(game) || !VERSION.test(v)) {
        return say("bad: board, game and v, please", 400);
      }
      return env.BOARDS.get(env.BOARDS.idFromName("board:" + board + ":" + game + ":" + v)).fetch(request);
    }
    const raid = url.pathname.match(/^\/raid\/(\d{1,6})$/);
    if (raid) {
      return env.RAID.get(env.RAID.idFromName("raid:" + Number(raid[1]))).fetch(request);
    }
    const daily = url.pathname.match(/^\/daily\/(\d{8})$/);
    if (daily) {
      if (dayNumber(daily[1]) === null) {
        return say("bad: not a date", 400);
      }
      return env.DAILY.get(env.DAILY.idFromName(daily[1])).fetch(request);
    }
    const m = url.pathname.match(/^\/room\/([^/]+)$/);
    if (!m || !CODE.test(m[1])) {
      return say("Not a room", 404);
    }
    if (!ROLES.includes(url.searchParams.get("role"))) {
      return say("role: host, guest, seat, watch or peer", 400);
    }
    if (request.headers.get("Upgrade") !== "websocket") {
      return say("A WebSocket upgrade, please", 426);
    }
    return env.ROOMS.get(env.ROOMS.idFromName(m[1])).fetch(request);
}

function isWatchMessage(message) {
  if (message.byteLength < 4) {
    return false;
  }
  const b = new Uint8Array(message, 0, 4);
  return b[0] === 0xff && b[1] === 0xff && b[2] === 0xff && b[3] === 0xff;
}

export class Room extends Stored {
  // the room's connections in a role, without those replaced by a reconnect
  here(role) {
    return this.ctx.getWebSockets(role).filter((ws) => {
      const a = ws.deserializeAttachment();
      return a && !a.replaced;
    });
  }

  // the Lobby told how many are here, and the listing while places are free
  async report() {
    const hosts = this.here("host");
    const playing = hosts.length + this.here("guest").length + this.here("seat").length + this.here("peer").length;
    const players = playing + this.here("watch").length;
    const a = hosts.length > 0 ? hosts[0].deserializeAttachment() : null;
    const max = a && a.list ? a.list.max || 2 : 2;
    const list = a && a.list && playing < max ? { ...a.list, playing } : null;
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
    const reconnect = role !== "watch" && role !== "seat" && ticket !== null && TICKET.test(ticket);
    let here = this.here(role);
    let oldSlot = null; // (a peer's place, taken back)
    // a reconnect: its ticket gives it the place of the connection it had
    if (reconnect) {
      for (const old of here) {
        const a = old.deserializeAttachment();
        if (a.ticket === ticket) {
          oldSlot = a.slot;
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
      return say("That room has a host", 409);
    }
    if (role === "guest" || role === "watch" || role === "seat" || role === "peer") {
      if (this.here("host").length === 0) {
        return say("No one is hosting that room", 404);
      }
      if (role === "peer" && here.length >= MAX_PEERS) {
        return say("That room has as many players as it can", 409);
      }
      if (role === "guest" && here.length > 0) {
        return say("That room is full", 409);
      }
      if (role === "watch" && here.length >= MAX_WATCHERS) {
        return say("As many are watching that room as can", 409);
      }
      if (role === "seat" && here.length >= MAX_SEATS) {
        return say("That room has as many players as it can", 409);
      }
    }
    // a seat's place in the room (0 or 1): the host knows its messages by it
    let slot = null;
    if (role === "seat") {
      const taken = here.map((ws) => ws.deserializeAttachment().slot);
      slot = taken.includes(0) ? 1 : 0;
    }
    if (role === "peer") {
      // a reconnect takes back its place (the old connection's, closed above); a new one the first free
      if (oldSlot !== null && oldSlot !== undefined) {
        slot = oldSlot;
      } else {
        const taken = here.map((ws) => ws.deserializeAttachment().slot);
        slot = taken.includes(0) ? 1 : 0;
      }
    }
    const pair = new WebSocketPair();
    const mine = role === "watch" || role === "seat" ? null : reconnect ? ticket : newTicket();
    // a host's listing: the whole code, its name, the game and version it plays
    let list = null;
    const secret = params.get("list");
    if (role === "host" && secret !== null && SECRET.test(secret) && GAME.test(params.get("game") || "") &&
        VERSION.test(params.get("v") || "")) {
      const half = new URL(request.url).pathname.split("/").pop();
      const max = Math.min(Math.max(Number(params.get("max")) || 2, 2), 2 + MAX_SEATS);
      list = { code: half + secret, name: cleanName(params.get("name")), game: params.get("game"),
               v: params.get("v"), max };
    }
    this.ctx.acceptWebSocket(pair[1], [role]);
    const framed = role === "host" && params.get("framed") === "1";
    pair[1].serializeAttachment(framed ? { role, since: Date.now(), ticket: mine, list, slot, framed }
                                       : { role, since: Date.now(), ticket: mine, list, slot });
    if (role === "watch" || role === "seat") {
      this.tellWatchers(null);
      await this.report();
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    const address = request.headers.get("CF-Connecting-IP");
    if (address) {
      pair[1].send("you " + address);
    }
    pair[1].send("ticket " + mine);
    if (framed) {
      pair[1].send("framed");
    }
    if (role === "host" && this.here("watch").length > 0) {
      this.tellWatchers(pair[1]);
    }
    // peers (players 3 and 4): the host told "peer <place>" for each, each peer "peer" (the host's here)
    if (role === "peer") {
      const hosts = this.here("host");
      if (hosts.length > 0) {
        pair[1].send("peer");
        hosts[0].send("peer " + slot);
      }
      await this.report();
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    if (role === "host") {
      for (const p of this.here("peer")) {
        try {
          p.send("peer");
          pair[1].send("peer " + p.deserializeAttachment().slot);
        } catch {
          // closing
        }
      }
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
    const { role, since, replaced, slot } = ws.deserializeAttachment();
    if (replaced || role === "watch") {
      return; // a watcher's go nowhere (it sends none)
    }
    if (message.byteLength > MAX_MESSAGE || Date.now() - since > ROOM_LIFETIME_MS) {
      ws.close(1009, "Too long");
      return;
    }
    if (role === "peer" && message.byteLength > MAX_FRAMED) {
      ws.close(1009, "Too long");
      return;
    }
    if (role === "seat" || role === "peer") {
      // to the host, as one of the stream's messages: its length, FROM_SEAT (FROM_PEER), the place,
      // the message
      const hosts = this.here("host");
      if (hosts.length > 0) {
        const wrapped = new Uint8Array(6 + message.byteLength);
        const total = 2 + message.byteLength;
        wrapped[0] = total & 0xff;
        wrapped[1] = (total >>> 8) & 0xff;
        wrapped[2] = (total >>> 16) & 0xff;
        wrapped[3] = (total >>> 24) & 0xff;
        wrapped[4] = role === "peer" ? FROM_PEER : FROM_SEAT;
        wrapped[5] = slot;
        wrapped.set(new Uint8Array(message), 6);
        hosts[0].send(wrapped.buffer);
      }
      return;
    }
    if (role === "host" && message.byteLength >= 4) {
      // to one peer: TO_PEER three times, its place, then the message (no stream message starts so: as
      // a length it would be over MAX_MESSAGE)
      const head = new Uint8Array(message, 0, 4);
      if (head[0] === TO_PEER && head[1] === TO_PEER && head[2] === TO_PEER && head[3] < MAX_PEERS) {
        for (const p of this.here("peer")) {
          if (p.deserializeAttachment().slot === head[3]) {
            try {
              p.send(message.slice(4));
            } catch {
              // closing
            }
          }
        }
        return;
      }
    }
    if (role === "host" && isWatchMessage(message)) {
      const body = message.slice(4);
      for (const w of [...this.here("watch"), ...this.here("seat")]) {
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
      const to = role === "guest" ? other[0].deserializeAttachment() : null;
      if (to && to.framed) {
        // a framed host's guest: its message wrapped (its length, FROM_GUEST, the message)
        if (message.byteLength > MAX_FRAMED) {
          ws.close(1009, "Too long");
          return;
        }
        const wrapped = new Uint8Array(5 + message.byteLength);
        const total = 1 + message.byteLength;
        wrapped[0] = total & 0xff;
        wrapped[1] = (total >>> 8) & 0xff;
        wrapped[2] = (total >>> 16) & 0xff;
        wrapped[3] = (total >>> 24) & 0xff;
        wrapped[4] = FROM_GUEST;
        wrapped.set(new Uint8Array(message), 5);
        other[0].send(wrapped.buffer);
        return;
      }
      other[0].send(message);
    }
  }

  async webSocketClose(ws, code) {
    const a = ws.deserializeAttachment();
    const { role, replaced, slot } = a;
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
    if (role === "seat" || role === "peer") {
      for (const h of this.here("host")) {
        try {
          h.send((role === "peer" ? "peergone " : "seatgone ") + slot);
        } catch {
          // closing
        }
      }
      return;
    }
    const others = this.here(role === "host" ? "guest" : "host");
    const told = role === "host" ? [...others, ...this.here("seat"), ...this.here("watch"), ...this.here("peer")]
                                 : others;
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
export class Lobby extends Stored {
  async fetch(request) {
    const url = new URL(request.url);
    const now = Date.now();
    if (request.method === "POST" && url.pathname === "/report") {
      let r;
      try {
        r = await request.json();
      } catch {
        return say("bad: not a report", 400);
      }
      if (!r || typeof r.id !== "string" || r.id.length > 80) {
        return say("bad: not a report", 400);
      }
      if (!(r.players > 0)) {
        await this.ctx.storage.delete("r:" + r.id);
      } else {
        const old = (await this.ctx.storage.get("r:" + r.id)) || {};
        const list = r.list || null;
        await this.ctx.storage.put("r:" + r.id, {
          players: Math.min(Number(r.players) || 0, 2 + MAX_SEATS + MAX_WATCHERS),
          list,
          listedAt: list ? (old.list && old.list.code === list.code ? old.listedAt : now) : 0,
          at: now,
        });
      }
      return say("ok");
    }
    if (url.pathname !== "/rooms") {
      return say("Not here", 404);
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
      const playing = r.list.playing || 1;
      text += r.list.code + " " + playing + "/" + (r.list.max || 2) + " " + encodeURIComponent(r.list.name || "") + "\n";
    }
    return say(text);
  }
}

// --- paper notes and leaderboards (0.4.0) ---
//
// Both are small stores in Durable Objects' storage, sized for the free plan:
// a Durable Object per area's notes and per leaderboard, each read when a
// player opens the page or enters the area (never polled). Who sent what is
// kept only as an address's hash (addressHash), for the daily limits and one
// vote each.

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
const QUOTA_KEYS = 4000;    // addresses counted in a day, so the quota stays one small value

// one more of an address's daily allowance of kind; false when it's spent (resets at 00:00 UTC)
async function spend(storage, kind, hash, max) {
  const day = new Date().toISOString().slice(0, 10);
  let q = (await storage.get("quota")) || { day, n: {} };
  if (q.day !== day) {
    q = { day, n: {} };
  }
  const key = kind + ":" + hash;
  if ((q.n[key] || 0) >= max || (!(key in q.n) && Object.keys(q.n).length >= QUOTA_KEYS)) {
    return false;
  }
  q.n[key] = (q.n[key] || 0) + 1;
  await storage.put("quota", q);
  return true;
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
export class Notes extends Stored {
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
    const text = await readText(request);
    if (text === null) {
      return say("bad: not a note", 400);
    }
    const body = text.trim().split(/\s+/);
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
export class Board extends Stored {
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
    if (request.method === "POST") {
      return this.post(request, url, storage);
    }
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
      return new Response(out, { headers: { ...TEXT, "content-type": "application/octet-stream" } });
    }
    return say("Not here", 404);
  }

  // An upload or a check. Everything that isn't storage (the body, the address's hash) is awaited
  // before the entries are read: a Durable Object lets another request in only while it awaits
  // something else, so between this read of "e" and its write nothing else runs (an upload that
  // awaited its body after reading "e" wrote back a copy without a run another had put there meanwhile).
  async post(request, url, storage) {
    if (url.pathname !== "/check" && url.pathname !== "/board") {
      return say("Not here", 404);
    }
    const hash = await addressHash(request, this.env);
    if (url.pathname === "/check") {
      const text = await readText(request);
      const entries = (await storage.get("e")) || [];
      const e = entries.find((x) => x.id === url.searchParams.get("id"));
      if (!e) {
        return say("gone: that run isn't on the board any more", 404);
      }
      const frames = int((text || "").trim(), 1, 0x7fffffff);
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
    const entries = (await storage.get("e")) || []; // (from here on, only storage is awaited)
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

export class Bingo extends Stored {
  async fetch(request) {
    const params = new URL(request.url).searchParams;
    const name = cleanName(params.get("name"));
    const seed = (params.get("seed") || "").toUpperCase();
    if (name === "") {
      return say("bad: name, please", 400);
    }
    const now = Date.now();
    let room = (await this.ctx.storage.get("room")) || null;
    if (room && now - room.made > BINGO_LIFETIME_MS) {
      room = null;
    }
    if (!room) {
      if (!BINGO_SEED.test(seed)) {
        return say("bad: seed, please", 400);
      }
      room = { made: now, seed, players: [], claims: {} };
    }
    let you = room.players.indexOf(name);
    if (you < 0) {
      if (room.players.length >= BINGO_PLAYERS) {
        return say("That room is full", 409);
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
    return say(text);
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

// the day's number since 1970-01-01 (UTC), or null when it isn't a date
function dayNumber(yyyymmdd) {
  const y = Number(yyyymmdd.slice(0, 4)), m = Number(yyyymmdd.slice(4, 6)), d = Number(yyyymmdd.slice(6, 8));
  const at = new Date(Date.UTC(y, m - 1, d));
  if (at.getUTCFullYear() !== y || at.getUTCMonth() !== m - 1 || at.getUTCDate() !== d) {
    return null;
  }
  return Math.floor(at.getTime() / 86400e3);
}

// The daily and raid boards: one stored value, each name's best entry and the
// names each address (as a hash) sent under, the whole of it dropped a while
// after its first entry. Sized to stay one small value: past NAMED_BOARD_NAMES
// names, a new one goes on only if it beats the last, which leaves.
const NAMED_BOARD_NAMES = 600;
const NAMED_BOARD_ADDRESSES = 1000;

class NamedBoard extends Stored {
  async board() {
    return (await this.ctx.storage.get("board")) || { best: {}, addresses: {} };
  }

  // the names, best first (compare: below 0 when its first entry is better)
  ranked(board, compare) {
    return Object.entries(board.best).sort((a, b) => compare(a[1], b[1]));
  }

  // name's entry sent: kept when it's the name's first or better than its
  // last. A refusal (a Response), or null when it's on the board. who: the
  // sender's addressHash with opts.scope, worked out before the board was read
  // (only storage is awaited between that read and the write here, so no
  // other request runs in between and writes back an older copy).
  async enter(who, board, opts, name, entry) {
    const { perAddress, keepMs, compare } = opts;
    const old = board.best[name];
    let dropped = null;
    if (!old && Object.keys(board.best).length >= NAMED_BOARD_NAMES) {
      const last = this.ranked(board, compare).pop();
      if (compare(entry, last[1]) >= 0) {
        return say("slower: the board is full of better ones", 409);
      }
      dropped = last[0];
    }
    const names = board.addresses[who] || [];
    if (!names.includes(name)) {
      if (names.length >= perAddress) {
        return say("names: enough names from here for this board", 429);
      }
      if (names.length === 0 && Object.keys(board.addresses).length >= NAMED_BOARD_ADDRESSES) {
        return say("full: this board takes no more senders", 429);
      }
      names.push(name);
      board.addresses[who] = names;
    }
    if (!old || compare(entry, old) < 0) {
      board.best[name] = entry;
      if (dropped !== null) {
        delete board.best[dropped];
      }
    }
    if ((await this.ctx.storage.getAlarm()) === null) {
      await this.ctx.storage.setAlarm(Date.now() + keepMs);
    }
    await this.ctx.storage.put("board", board);
    return null;
  }

  async alarm() {
    await this.ctx.storage.deleteAll();
  }
}

const DAILY = {
  perAddress: DAILY_NAMES_PER_ADDRESS,
  keepMs: DAILY_KEEP_MS,
  compare: (a, b) => a.ms - b.ms || a.at - b.at,
};

export class Daily extends NamedBoard {
  async fetch(request) {
    const url = new URL(request.url);
    const day = url.pathname.split("/").pop();
    const params = url.searchParams;
    const who = params.has("ms") ? await addressHash(request, this.env, "daily:" + day) : null;
    const board = await this.board();
    let rankOf = null;
    if (params.has("ms")) {
      const name = cleanName(params.get("name"));
      const ms = int(params.get("ms"), DAILY_MIN_MS, DAILY_MAX_MS);
      const game = GAME.test(params.get("game") || "") ? params.get("game") : "?";
      const age = Math.floor(Date.now() / 86400e3) - dayNumber(day);
      if (name === "" || ms === null || age < 0 || age > 2) {
        return say("bad: not a time for this board", 400);
      }
      const refused = await this.enter(who, board, DAILY, name,
                                       { ms, game, at: Date.now() });
      if (refused) {
        return refused;
      }
      rankOf = name;
    }
    const sorted = this.ranked(board, DAILY.compare);
    let text = "daily " + day + " " + sorted.length;
    if (rankOf !== null) {
      text += " " + (sorted.findIndex(([n]) => n === rankOf) + 1);
    }
    text += "\n";
    sorted.slice(0, DAILY_SHOWN).forEach(([n, t], i) => {
      text += i + 1 + " " + t.ms + " " + encodeURIComponent(n) + " " + t.game + "\n";
    });
    return say(text);
  }
}

// The raid boss's weekly boards (port/settings/features/raid.cpp, docs/RAID.md): one Durable Object
// per week (weeks since Monday 1969-12-29, UTC), the best clear per party name.
//
//   GET /raid/<WEEK>                                                    the week's boss and board
//   GET /raid/<WEEK>?name=<NAME>&boss=<B>&turns=<T>&frames=<F>&players=<P>&game=<ID>&v=<VERSION>
//                                                                       a clear sent
//
//   raid <WEEK> <BOSS> <CLEARS> [<THIS NAME'S RANK>]
//   <RANK> <TURNS> <FRAMES> <PLAYERS> <NAME, percent-encoded> <GAME>   (the best RAID_SHOWN)
//
// The week's boss is the rotation's (raidBossOf: the game's portRaidOfWeek, port/src/aurora/raid_aurora.c,
// which needs no network: the same list in the same order), so the game can tell a computer whose date
// is off. A clear is taken only for this week or the last (a fight that ended after Monday came), for
// that week's boss, with 2 to 4 players, at least RAID_MIN_TURNS turns and RAID_MIN_FRAMES frames; a
// name keeps its best (fewest turns, then frames). One address may send under RAID_NAMES_PER_ADDRESS
// names a week (kept as a hash salted with the week, never the address). A week's board is dropped
// RAID_KEEP_MS after its first clear.
const RAID_BOSSES = 6; // kBosses in raid_aurora.c: Bonetail, Hooktail, Magnus 2.0, Gloomtail, Macho Grubba, Smorg
const RAID_SHOWN = 50;
const RAID_MIN_TURNS = 4;
const RAID_MAX_TURNS = 999;
const RAID_MIN_FRAMES = 60 * 60;
const RAID_MAX_FRAMES = 60 * 60 * 60 * 6;
const RAID_NAMES_PER_ADDRESS = 4;
const RAID_KEEP_MS = 120 * 24 * 3600e3;
const RAID_MAX_NAME = 80; // a party's names, "A & B & C & D"

function raidWeekNow() {
  return Math.floor((Math.floor(Date.now() / 86400e3) + 3) / 7);
}

function raidBossOf(week) {
  return week % RAID_BOSSES;
}

const RAID = {
  perAddress: RAID_NAMES_PER_ADDRESS,
  keepMs: RAID_KEEP_MS,
  compare: (a, b) => a.turns - b.turns || a.frames - b.frames || a.at - b.at,
};

export class Raid extends NamedBoard {
  async fetch(request) {
    const url = new URL(request.url);
    const week = Number.parseInt(url.pathname.split("/").pop(), 10);
    const params = url.searchParams;
    const boss = raidBossOf(week);
    const who = params.has("turns") ? await addressHash(request, this.env, "raid:" + week) : null;
    const board = await this.board();
    let rankOf = null;
    if (params.has("turns")) {
      const name = cleanName(params.get("name"), RAID_MAX_NAME);
      const turns = int(params.get("turns"), RAID_MIN_TURNS, RAID_MAX_TURNS);
      const frames = int(params.get("frames"), RAID_MIN_FRAMES, RAID_MAX_FRAMES);
      const players = int(params.get("players"), 2, 4);
      const age = raidWeekNow() - week;
      const game = GAME.test(params.get("game") || "") ? params.get("game") : "?";
      if (name === "" || int(params.get("boss"), 0, RAID_BOSSES - 1) !== boss || turns === null ||
          frames === null || players === null || age < 0 || age > 1) {
        return say("bad: not a clear for this board", 400);
      }
      const refused = await this.enter(who, board, RAID, name,
                                       { turns, frames, players, game, at: Date.now() });
      if (refused) {
        return refused;
      }
      rankOf = name;
    }
    const sorted = this.ranked(board, RAID.compare);
    let text = "raid " + week + " " + boss + " " + sorted.length;
    if (rankOf !== null) {
      text += " " + (sorted.findIndex(([n]) => n === rankOf) + 1);
    }
    text += "\n";
    sorted.slice(0, RAID_SHOWN).forEach(([n, c], i) => {
      text += i + 1 + " " + c.turns + " " + c.frames + " " + c.players + " " + encodeURIComponent(n) + " " + c.game + "\n";
    });
    return say(text);
  }
}
