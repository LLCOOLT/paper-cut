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
//       a 0.5.1 host's: &caps=1 (one message for all its players, SPLIT; told "busy" when the day is mostly used)
//   GET /rooms?game=<ID>&v=<VERSION>  the room list (text, below)
//   GET /budget                       the relay's day so far, as the rooms count it (Lobby.prototype.day)
//   GET /health, /health.json         how online play went, a block a UTC day for 14 days: counts only (HEALTH_GROUPS)
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
//
// Guards (0.5.1), so one script can't squat rooms or spend the free day for everyone:
// - the Worker counts each address's room connections (ROOM_OPENS_A_MINUTE) and other requests
//   (REQUESTS_A_MINUTE) before any Durable Object is asked (each would be billed): past it, 403
//   "slow: ..." (not "busy", the day nearly used: below). With a rate limit binding (ROOM_LIMIT, HTTP_LIMIT in wrangler.toml) it's that,
//   else a best-effort count in this isolate's memory. Requests without CF-Connecting-IP (tests)
//   aren't counted.
// - a connection that sends more than FLOOD_MESSAGES messages (or FLOOD_BYTES) a second for
//   FLOOD_SECONDS seconds in a row is closed (1008 "Too fast"); the games send far fewer. It is counted with the day's meter (Room.count).
// - the host may take a game out of its place (text, ignored by older relays): "drop guest" or
//   "drop peer <N>" (a player it removed): that connection closed (4001 "Removed") and its ticket
//   refused for the room's life (403 "removed: ..."); with " block" after it (a game that held the
//   place without ever agreeing the key: a squatter), closed 4002 "No key" and its address refused
//   for BLOCK_MS (403 "blocked: ..."), its ticket not. Either way the host is told "gone" /
//   "peergone <N>" as for a leave.
// - the host's first connection makes the room's Durable Object near it (a location hint from
//   Cloudflare's idea of where the host is); a 0.5.0+ host (&framed=1) is told "where <COLO> <HINT>"
//   (its edge and the hint asked for: for the owner's checks, only logged).

const CODE = /^[0-9A-Z]{4,8}$/;
const TICKET = /^[0-9a-f]{32}$/;
const MAX_MESSAGE = 1 << 20;        // the host's setup (settings and memory card) is the largest
const ROOM_LIFETIME_MS = 6 * 3600e3; // a room's listing and count let go this long after its last report
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
// A 0.5.1 host asks (&caps=1) for one message for all its players: it's told "caps split", then may send
// SPLIT four times followed by parts [u8 place: 0xFF the guest, 0..MAX_PEERS-1 a peer][u32 length LE]
// [bytes], each part sent on exactly as that host's own message to that player would be (the guest's as it
// is, a peer's as a TO_PEER one's body). A room's day is charged once for the lot instead of once a player.
// No stream message starts with SPLIT (as a length it's over MAX_MESSAGE). A part out of bounds, or for no
// place, closes the host (1007) and nothing of that message goes.
const SPLIT = 0xfd;
// The relay's day (Workers Free: 100,000 Durable Object requests, an incoming WebSocket message a twentieth
// of one, a connection one): each Room counts what comes in (in twentieths: "units") on its connections'
// attachments and adds it to the Lobby's count for the day (UTC) every METER_EVERY messages on a connection
// and as it closes. GET /budget answers "budget <requests> <limit>" (rooms and their connections, and the
// Lobby's own counting; paper notes, boards and bingo are left out: they're few). A new 0.5.1 host
// (&caps=1) is told "busy" past BUSY_AT of the limit, and turned away past FULL_AT (429, as Cloudflare's own
// limit says it), so rooms playing finish; older games never ask and are never refused by this.
const METER_EVERY = 2000;
const BUSY_AT = 0.8;
const FULL_AT = 0.95;
const DAY_REQUESTS = 100000; // (env.RELAY_DAY_REQUESTS: another limit)
const ROLES = ["host", "guest", "seat", "watch", "peer"];
const SECRET = /^[0-9A-Z]{4}$/;
const GAME = /^[0-9A-Z]{6}$/;
const VERSION = /^[0-9A-Za-z.+-]{1,24}$/;
const LIST_TTL_MS = 20 * 60e3; // a listing the host hasn't refreshed (it does every 10 minutes)
const MAX_LISTED = 40;         // shown in the list at most
const MAX_NAME = 24;
const MAX_URL = 2048;          // a request's address, query and all
const MAX_POST = 256;          // a text request's body (notes, checks)
// the guards (above)
const ROOM_OPENS_A_MINUTE = 30;   // an address's room connections (a game reconnecting: a few)
const REQUESTS_A_MINUTE = 120;    // an address's other requests (notes, boards, bingo, the list)
const LIMIT_KEYS = 5000;          // addresses counted in an isolate's memory at most
const FLOOD_MESSAGES = 100;       // a second, from one connection (a game: about 20; a host: this for each player)
const FLOOD_BYTES = 4 << 20;
const FLOOD_SECONDS = 3;
const FLOOD_AT_ONCE = 1000;       // in one second: closed at once
const BLOCK_MS = 5 * 60e3;        // a blocked squatter's address refused this long
const MAX_REFUSED = 32;           // tickets and addresses a host's room refuses, kept at most
const DROP = /^drop (guest|peer ([0-9]))( block)?$/;
const WHERE = /^[A-Z]{3} [a-z]{2,4}$/;

// --- the relay's health (0.5.1): how online play goes, per UTC day, counts only ---
//
// GET /health (text) and /health.json: for each of the last HEALTH_DAYS days, how many rooms opened (and of
// which kind), joins by role, refusals by reason, reconnects and how they went, how connections ended, how long
// player connections and rooms lasted, the day's peak players and rooms online, guards fired and errors caught.
// Plain counts kept by the Lobby ("h:<YYYY-MM-DD>"): never a room code, name, address, ticket or anything per
// player. Each Room keeps its counts in memory and hands them to the Lobby with the report it sends anyway
// (Room.report: a join, a leave, a listing's refresh); counts with nothing to ride on (a refusal in a room with
// nobody in it, a guard firing mid-game) go on their own at most once a minute a room (POST /count, a request
// counted in the day's meter). The Worker's own (refusals at the front door, its errors) ride on its next
// request to a Room or the Lobby (the X-Papercut-Counts header, set only by the Worker).
const HEALTH_DAYS = 14;
const HEALTH_FLUSH_MS = 60e3;
const COUNTS_HEADER = "X-Papercut-Counts";
const SPANS = ["0_1m", "1_5m", "5_15m", "15_60m", "1_3h", "3h_plus"];
const SPAN_NAMES = ["<1 min", "1-5 min", "5-15 min", "15-60 min", "1-3 h", ">3 h"];
const HEALTH_GROUPS = {
  rooms: ["opened", "listed", "private", "sync", "lockstep", "split"],
  join: ["host", "guest", "seat", "peer", "watch"],
  refused: ["no_host", "host_taken", "full", "watch_full", "today", "removed", "blocked", "slow_room", "bad_code",
            "bad_role", "not_websocket"],
  resume: ["asked", "ok", "took_over", "expired", "refused"],
  end: ["clean", "away", "dropped", "replaced", "removed", "blocked", "flood", "too_long", "bad_split", "other"],
  session: SPANS,
  life: SPANS,
  guard: ["slow_http", "flood", "too_long", "bad_split", "drop", "drop_block", "busy_told"],
  error: ["worker", "room_open", "room_message", "room_close", "lobby"],
};
const HEALTH_KEYS = new Set(Object.entries(HEALTH_GROUPS).flatMap(([g, names]) => names.map((n) => g + "." + n)));

// a length of time's bucket (SPANS)
function span(ms) {
  const min = ms / 60e3;
  return SPANS[min < 1 ? 0 : min < 5 ? 1 : min < 15 ? 2 : min < 60 ? 3 : min < 180 ? 4 : 5];
}

// counts (from a Room, the Worker) added into into: known keys and sane numbers only
function addCounts(into, counts) {
  if (!counts || typeof counts !== "object") {
    return into;
  }
  for (const [k, v] of Object.entries(counts)) {
    const n = Number(v);
    if (HEALTH_KEYS.has(k) && Number.isInteger(n) && n > 0 && n <= 1e6) {
      into[k] = (into[k] || 0) + n;
    }
  }
  return into;
}

// the counts a request carries from the Worker (COUNTS_HEADER), or null
function carriedCounts(request) {
  const text = request.headers && typeof request.headers.get === "function" ? request.headers.get(COUNTS_HEADER) : null;
  if (!text || text.length > 4096) {
    return null;
  }
  try {
    const c = addCounts({}, JSON.parse(text));
    return Object.keys(c).length > 0 ? c : null;
  } catch {
    return null;
  }
}

// --- shared by every class ---
//
// Every refusal is a status other than 200 and "<WHY>: <words>" on one line
// (the games show their own text from the status or WHY). The room's refusals
// keep their words: the games look for "hosting" and "full" in them.

const TEXT = { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" };

function say(text, status = 200) {
  return new Response(text.endsWith("\n") ? text : text + "\n", { status, headers: TEXT });
}

// a room's refusal, its reason kept on it for the health counts (refused.<why>)
function refusal(why, text, status) {
  const res = say(text, status);
  res.why = why;
  return res;
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

// A device's paper-notes code (0.5.1: random, made once by the game and kept in its user folder) is kept only as
// this hash, with HASH_SALT, as its notes' author: never the code itself. (16 bytes, and its own scope, so it
// never equals an address's hash.)
async function noteCodeHash(code, env) {
  const text = (env.HASH_SALT || "papercut") + ":note-code:" + code;
  return hex(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text))).slice(0, 16));
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

// An address's requests of a kind ("room" or "http") counted in this isolate's memory (a minute's
// window): best effort (Cloudflare runs many isolates), used when no rate limit binding is set.
const sCounted = new Map();

function countedOver(key, max) {
  const now = Date.now();
  let c = sCounted.get(key);
  if (!c || now - c.at > 60e3) {
    if (!c && sCounted.size >= LIMIT_KEYS) {
      for (const [k, old] of sCounted) {
        if (now - old.at > 60e3) {
          sCounted.delete(k);
        }
      }
      if (sCounted.size >= LIMIT_KEYS) {
        sCounted.delete(sCounted.keys().next().value); // (the oldest)
      }
    }
    c = { at: now, n: 0 };
    sCounted.set(key, c);
  }
  c.n++;
  return c.n > max;
}

// true when this request's address has made too many of this kind lately
async function overLimit(request, env, kind) {
  const ip = request.headers && typeof request.headers.get === "function" ? request.headers.get("CF-Connecting-IP") : null;
  if (!ip) {
    return false;
  }
  const binding = kind === "room" ? env.ROOM_LIMIT : env.HTTP_LIMIT;
  if (binding && typeof binding.limit === "function") {
    try {
      const { success } = await binding.limit({ key: ip });
      return !success;
    } catch {
      // (the binding's trouble: the memory's count instead)
    }
  }
  return countedOver(kind + ":" + ip, kind === "room" ? ROOM_OPENS_A_MINUTE : REQUESTS_A_MINUTE);
}

// The Worker's own health counts (its refusals and errors), in this isolate's memory until its next request to
// a Room or the Lobby takes them along (best effort: an isolate let go with some loses them)
let sFront = {};

function frontCount(key) {
  sFront[key] = (sFront[key] || 0) + 1;
}

// the request to send on to a Room or the Lobby: the Worker's counts on it, never a client's own header
function carry(request, headers = null) {
  const counts = Object.keys(sFront).length > 0 ? sFront : null;
  const theirs = request.headers && typeof request.headers.get === "function" &&
                 request.headers.get(COUNTS_HEADER) !== null;
  if (!counts && !theirs && !headers) {
    return request;
  }
  const h = headers || new Headers(request.headers);
  h.delete(COUNTS_HEADER);
  if (counts) {
    h.set(COUNTS_HEADER, JSON.stringify(counts));
    sFront = {};
  }
  return new Request(request, { headers: h });
}

// Where a room's Durable Object should be made: near its host (Cloudflare's guess of where the
// request came from). Only a Durable Object's first get() places it; later hints change nothing.
function locationHint(request) {
  const cf = request.cf;
  if (!cf || typeof cf.continent !== "string") {
    return null;
  }
  const lon = Number(cf.longitude);
  const lat = Number(cf.latitude);
  switch (cf.continent) {
    case "EU":
      return lon > 20 ? "eeur" : "weur";
    case "NA":
      return lon < -100 ? "wnam" : "enam";
    case "SA":
      return "sam";
    case "OC":
      return "oc";
    case "AF":
      return "afr";
    case "AS":
      return lon < 63 && lat < 42 ? "me" : "apac";
    default:
      return null;
  }
}

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS });
    }
    let answer;
    try {
      answer = await route(request, env);
    } catch (e) {
      frontCount("error.worker"); // (told the Lobby with the next request: the health report)
      throw e;
    }
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
    if (url.pathname === "/") {
      return say("Paper Cut relay");
    }
    if (request.method !== "GET" && request.method !== "POST") {
      return say("method: GET or POST", 405);
    }
    const toRoom = url.pathname.startsWith("/room/");
    if (await overLimit(request, env, toRoom ? "room" : "http")) {
      // (before any Durable Object is asked. Not 429, which the games read as the day used up ("today"), nor
      // "busy", the day mostly used (the budget meter): this address alone is slowed)
      frontCount(toRoom ? "refused.slow_room" : "guard.slow_http");
      return say("slow: too many tries from your address: wait a minute", 403);
    }
    if (url.pathname === "/health" || url.pathname === "/health.json") {
      // the relay's health (HEALTH_GROUPS): counts only, nothing personal, so anyone may read it
      if (request.method !== "GET") {
        return say("method: GET", 405);
      }
      return env.LOBBY.get(env.LOBBY.idFromName("lobby")).fetch(carry(request));
    }
    if (url.pathname === "/rooms" || url.pathname === "/budget") {
      return env.LOBBY.get(env.LOBBY.idFromName("lobby")).fetch(carry(request));
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
      if (toRoom) {
        frontCount("refused.bad_code");
      }
      return say("Not a room", 404);
    }
    if (!ROLES.includes(url.searchParams.get("role"))) {
      frontCount("refused.bad_role");
      return say("role: host, guest, seat, watch or peer", 400);
    }
    if (request.headers.get("Upgrade") !== "websocket") {
      frontCount("refused.not_websocket");
      return say("A WebSocket upgrade, please", 426);
    }
    const hint = url.searchParams.get("role") === "host" ? locationHint(request) : null;
    if (hint === null) {
      return env.ROOMS.get(env.ROOMS.idFromName(m[1])).fetch(carry(request));
    }
    // the host's: the room made near it, and the room told where (its edge and the hint), for its log
    const headers = new Headers(request.headers);
    headers.set("X-Papercut-Where", String(request.cf.colo || "???").slice(0, 3) + " " + hint);
    return env.ROOMS.get(env.ROOMS.idFromName(m[1]), { locationHint: hint }).fetch(carry(request, headers));
}

function isWatchMessage(message) {
  if (message.byteLength < 4) {
    return false;
  }
  const b = new Uint8Array(message, 0, 4);
  return b[0] === 0xff && b[1] === 0xff && b[2] === 0xff && b[3] === 0xff;
}

// the relay's day, as the Lobby counts it ("YYYY-MM-DD", UTC)
function today() {
  return new Date().toISOString().slice(0, 10);
}

function dayLimit(env) {
  const n = Number(env && env.RELAY_DAY_REQUESTS);
  return n > 0 ? n : DAY_REQUESTS;
}

// The flood cap's rate, kept with the day's count on the same attachment (Room.count): this second's
// messages and bytes, and how many seconds in a row were over the line. A copy of r, moved on by one
// message of that many bytes; .fast when the connection is to be closed. (max: messages a second.)
function rateStep(r, bytes, max) {
  const second = Math.floor(Date.now() / 1000);
  const out = r && typeof r.s === "number" ? { ...r } : { s: second, n: 0, b: 0, streak: 0, last: -2 };
  if (out.fast) {
    return out;
  }
  if (second !== out.s) {
    // the second just gone: over the line or not (a streak of seconds over it in a row)
    if (out.n > max || out.b > FLOOD_BYTES) {
      out.streak = out.last === out.s - 1 ? out.streak + 1 : 1;
      out.last = out.s;
    } else {
      out.streak = 0;
    }
    out.s = second;
    out.n = 0;
    out.b = 0;
  }
  out.n++;
  out.b += bytes;
  if (out.n > FLOOD_AT_ONCE || out.streak >= FLOOD_SECONDS) {
    out.fast = true;
  }
  return out;
}

export class Room extends Stored {
  // one more message in on this connection, of that many bytes: the day's count (its attachment's n, in
  // twentieths of a request) and the flood cap's rate (its attachment's r: one counting for both).
  // [the units to give the Lobby now (every METER_EVERY messages, else 0), too fast: to be closed]
  count(ws, bytes) {
    let a;
    try {
      a = ws.deserializeAttachment();
    } catch {
      return [0, false];
    }
    if (!a) {
      return [0, false];
    }
    const n = (a.n || 0) + 1;
    // (a host sends one message for each player it has in a batch, without the split: allowed that many more)
    const r = rateStep(a.r, bytes, a.role === "host" ? FLOOD_MESSAGES * (1 + MAX_PEERS) : FLOOD_MESSAGES);
    try {
      ws.serializeAttachment({ ...a, n: n >= METER_EVERY ? 0 : n, r });
    } catch {
      return [0, false];
    }
    return [n >= METER_EVERY ? n : 0, !!r.fast];
  }

  // the Lobby told what came in (units: twentieths of a request)
  async meter(units) {
    if (!(units > 0)) {
      return;
    }
    try {
      await this.env.LOBBY.get(this.env.LOBBY.idFromName("lobby")).fetch("https://lobby/meter", {
        method: "POST",
        body: JSON.stringify({ day: today(), units }),
      });
    } catch {
      // the count is a guide: the room works without it
    }
  }

  // the day's requests so far and the limit, as the Lobby counts them ([0, limit] when it can't say)
  async budget() {
    try {
      const res = await this.env.LOBBY.get(this.env.LOBBY.idFromName("lobby")).fetch("https://lobby/budget");
      const m = String(await res.text()).match(/^budget (\d+) (\d+)/);
      if (m) {
        return [Number(m[1]), Number(m[2])];
      }
    } catch {
      // as below
    }
    return [0, dayLimit(this.env)];
  }

  // the room's connections in a role, without those replaced by a reconnect
  here(role) {
    return this.ctx.getWebSockets(role).filter((ws) => {
      const a = ws.deserializeAttachment();
      return a && !a.replaced;
    });
  }

  // the Lobby told how many are here, and the listing while places are free (and the room's health counts)
  async report() {
    const hosts = this.here("host");
    const playing = hosts.length + this.here("guest").length + this.here("seat").length + this.here("peer").length;
    const players = playing + this.here("watch").length;
    const a = hosts.length > 0 ? hosts[0].deserializeAttachment() : null;
    const max = a && a.list ? a.list.max || 2 : 2;
    const list = a && a.list && playing < max ? { ...a.list, playing } : null;
    const h = this.takeCounts();
    const body = { id: this.ctx.id.toString(), players, list };
    if (h) {
      body.h = h;
    }
    try {
      await this.env.LOBBY.get(this.env.LOBBY.idFromName("lobby")).fetch("https://lobby/report", {
        method: "POST",
        body: JSON.stringify(body),
      });
    } catch {
      // the list is a nicety: the room works without it
      this.giveBack(h);
    }
  }

  // --- the room's health counts (HEALTH_GROUPS): in memory until the Lobby takes them ---

  tally(key, n = 1) {
    this.counts = this.counts || {};
    this.counts[key] = (this.counts[key] || 0) + n;
  }

  // the counts so far, handed over (null when there are none)
  takeCounts() {
    const c = this.counts && Object.keys(this.counts).length > 0 ? this.counts : null;
    this.counts = {};
    if (c) {
      this.countsSentAt = Date.now();
    }
    return c;
  }

  giveBack(c) {
    if (c) {
      this.counts = addCounts(this.counts || {}, c);
    }
  }

  // counts with no report to ride on: on their own (one request, in the day's meter), at most once a minute
  async flushCounts() {
    if (!this.counts || Object.keys(this.counts).length === 0 ||
        Date.now() - (this.countsSentAt || 0) < HEALTH_FLUSH_MS) {
      return;
    }
    const h = this.takeCounts();
    try {
      await this.env.LOBBY.get(this.env.LOBBY.idFromName("lobby")).fetch("https://lobby/count", {
        method: "POST",
        body: JSON.stringify({ h }),
      });
    } catch {
      this.giveBack(h);
    }
  }

  // a connection's end counted (once: the relay closing one counts it then, its close may never come back);
  // its attachment, marked
  ended(a, how) {
    if (!a || a.ended) {
      return a;
    }
    this.tally("end." + how);
    if (a.role !== "watch" && typeof a.since === "number") {
      this.tally("session." + span(Date.now() - a.since));
    }
    return { ...a, ended: true };
  }

  // the relay closes a connection for a guard (flood, too_long, bad_split): counted, then closed
  cut(ws, code, reason, how) {
    try {
      const a = ws.deserializeAttachment();
      if (a && !a.ended) {
        this.tally("guard." + how); // (once: messages already on their way find it ended)
        ws.serializeAttachment(this.ended(a, how));
      }
    } catch {
      // already closed
    }
    try {
      ws.close(code, reason);
    } catch {
      // already closed
    }
  }

  // the room's connections in every role, replaced or not
  everyone() {
    return ROLES.flatMap((r) => this.ctx.getWebSockets(r));
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
    try {
      this.giveBack(carriedCounts(request)); // (the Worker's, to go with this room's)
      const params = new URL(request.url).searchParams;
      const reconnect = params.get("role") !== "watch" && params.get("role") !== "seat" &&
                        params.get("ticket") !== null && TICKET.test(params.get("ticket"));
      if (reconnect) {
        this.tally("resume.asked");
      }
      const res = await this.open(request, params, reconnect);
      if (res.status !== 101) {
        // a refusal: by its reason (refusal()), and a reconnect's as expired or refused
        const why = res.why || "full";
        this.tally("refused." + why);
        if (reconnect) {
          this.tally(why === "removed" || why === "blocked" ? "resume.refused" : "resume.expired");
        }
        await this.flushCounts();
      }
      return res;
    } catch (e) {
      this.tally("error.room_open");
      await this.flushCounts();
      throw e;
    }
  }

  async open(request, params, reconnect) {
    const role = params.get("role");
    const ticket = params.get("ticket");
    // a game the host removed: its ticket (or, blocked, its address) refused for the room's life
    let from = null;
    if (role === "guest" || role === "peer") {
      from = request.headers.get("CF-Connecting-IP") ? await addressHash(request, this.env, "room") : null;
      const hosts = this.here("host");
      const h = hosts.length > 0 ? hosts[0].deserializeAttachment() : null;
      if (h && reconnect && (h.refused || []).includes(ticket)) {
        return refusal("removed", "removed: the host removed you from the room", 403);
      }
      if (h && from !== null && (h.blocked || []).some((b) => b.h === from && b.until > Date.now())) {
        return refusal("blocked", "blocked: that room keeps this address out a while", 403);
      }
    }
    let here = this.here(role);
    let oldSlot = null; // (a peer's place, taken back)
    let carried = null; // (a host's refusals, kept across its reconnect)
    // a reconnect: its ticket gives it the place of the connection it had
    if (reconnect) {
      for (const old of here) {
        const a = old.deserializeAttachment();
        if (a.ticket === ticket) {
          oldSlot = a.slot;
          if (a.refused || a.blocked || a.last) {
            carried = { refused: a.refused, blocked: a.blocked, last: a.last };
          }
          old.serializeAttachment({ ...this.ended(a, "replaced"), replaced: true });
          this.tally("resume.took_over"); // (the old connection was still there)
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
      return refusal("host_taken", "That room has a host", 409);
    }
    if (role === "guest" || role === "watch" || role === "seat" || role === "peer") {
      if (this.here("host").length === 0) {
        return refusal("no_host", "No one is hosting that room", 404);
      }
      if (role === "peer" && here.length >= MAX_PEERS) {
        return refusal("full", "That room has as many players as it can", 409);
      }
      if (role === "guest" && here.length > 0) {
        return refusal("full", "That room is full", 409);
      }
      if (role === "watch" && here.length >= MAX_WATCHERS) {
        return refusal("watch_full", "As many are watching that room as can", 409);
      }
      if (role === "seat" && here.length >= MAX_SEATS) {
        return refusal("full", "That room has as many players as it can", 409);
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
    // a new 0.5.1 host (one that can be told "busy"): the relay's day asked first; nearly used, no new room
    const caps = role === "host" && params.get("caps") === "1";
    let busy = false;
    if (caps && !reconnect) {
      const [used, limit] = await this.budget();
      if (used >= limit * FULL_AT) {
        return refusal("today", "today: the relay's day is used up", 429);
      }
      busy = used >= limit * BUSY_AT;
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
    const framed = role === "host" && params.get("framed") === "1";
    // the health counts: a join (or a reconnect's place taken back), a new room and its kind (a framed host
    // plays "sync what matters", any other lockstep), and since when the room has had anyone in it (room:
    // carried on every connection, for its lifetime when the last one goes)
    let room = Date.now();
    for (const ws of this.everyone()) {
      const o = ws.deserializeAttachment();
      if (o && typeof o.room === "number" && o.room < room) {
        room = o.room;
      }
    }
    if (reconnect) {
      this.tally("resume.ok");
    } else {
      this.tally("join." + role);
      if (role === "host") {
        this.tally("rooms.opened");
        this.tally(list ? "rooms.listed" : "rooms.private");
        this.tally(framed ? "rooms.sync" : "rooms.lockstep");
        if (caps) {
          this.tally("rooms.split");
        }
      }
    }
    if (busy) {
      this.tally("guard.busy_told");
    }
    this.ctx.acceptWebSocket(pair[1], [role]);
    // (n: what came in on it, counted for the day from its connection, a request: 20 twentieths)
    const attachment = framed ? { role, since: Date.now(), ticket: mine, list, slot, framed }
                              : { role, since: Date.now(), ticket: mine, list, slot };
    attachment.room = room;
    if (caps) {
      attachment.caps = true;
    }
    if (from !== null) {
      attachment.from = from; // (a guest's or peer's address, hashed: for the host's "drop ... block")
    }
    if (role === "host" && carried) {
      Object.assign(attachment, carried);
    }
    attachment.n = 20;
    pair[1].serializeAttachment(attachment);
    if (role === "guest" || role === "peer") {
      // the host keeps each place's last ticket: a "drop" that finds the place empty (its game left first, as a
      // told removed player's does) still refuses that ticket for the room's life
      const hosts = this.here("host");
      if (hosts.length > 0) {
        const h = hosts[0].deserializeAttachment();
        const key = role === "guest" ? "guest" : "peer" + slot;
        hosts[0].serializeAttachment({ ...h, last: { ...(h.last || {}), [key]: mine } });
      }
    }
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
      const where = request.headers.get("X-Papercut-Where") || "";
      if (WHERE.test(where)) {
        pair[1].send("where " + where); // (0.5.0+ hosts only: older games are never sent a new word)
      }
    }
    if (caps) {
      pair[1].send("caps split");
      if (busy) {
        pair[1].send("busy");
      }
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

  // the host's "drop guest" / "drop peer <N>" [block]: that game out of its place, refused after
  drop(host, a, which, place, block) {
    const now = Date.now();
    this.tally(block ? "guard.drop_block" : "guard.drop");
    const gone = which === "guest" ? this.here("guest")
                                   : this.here("peer").filter((p) => p.deserializeAttachment().slot === place);
    if (gone.length === 0) {
      // (left already, as a told removed player's game does at once: its last ticket refused all the same)
      const last = (a.last || {})[which === "guest" ? "guest" : "peer" + place];
      if (!block && last && !(a.refused || []).includes(last)) {
        host.serializeAttachment({ ...a, refused: [...(a.refused || []), last].slice(-MAX_REFUSED) });
      }
      return;
    }
    const refused = [...(a.refused || [])];
    const blocked = (a.blocked || []).filter((b) => b.until > now);
    for (const ws of gone) {
      const t = ws.deserializeAttachment();
      if (!block && t.ticket && !refused.includes(t.ticket)) {
        refused.push(t.ticket);
      }
      if (block && t.from) {
        blocked.push({ h: t.from, until: now + BLOCK_MS });
      }
      // (out of the room now, as a replaced one: its close later tells no one again)
      ws.serializeAttachment({ ...this.ended(t, block ? "blocked" : "removed"), replaced: true });
      try {
        ws.close(block ? 4002 : 4001, block ? "No key" : "Removed");
      } catch {
        // already closed
      }
    }
    host.serializeAttachment({ ...a, refused: refused.slice(-MAX_REFUSED), blocked: blocked.slice(-MAX_REFUSED) });
    try {
      host.send(which === "guest" ? "gone" : "peergone " + place);
    } catch {
      // closing
    }
  }

  async webSocketMessage(ws, message) {
    try {
      await this.message(ws, message);
    } catch (e) {
      this.tally("error.room_message");
      throw e;
    } finally {
      if (this.counts && Object.keys(this.counts).length > 0) {
        await this.flushCounts(); // (a guard fired, an error: at most once a minute)
      }
    }
  }

  async message(ws, message) {
    // (every message in is part of the day, whatever it is; the same count says when one floods)
    const [units, fast] = this.count(ws, typeof message === "string" ? message.length : message.byteLength);
    if (fast) {
      // (far more than any game sends: every message is billed, so the sender's closed)
      this.cut(ws, 1008, "Too fast", "flood");
    } else {
      await this.pass(ws, message);
    }
    await this.meter(units);
  }

  // a host's SPLIT message: each part to its player, as that host's own message to it would go; 0 when
  // the message isn't one (or the host never asked); -1 a bad one (the host closed, nothing sent)
  split(ws, message) {
    const b = new Uint8Array(message);
    if (b.length < 4 || b[0] !== SPLIT || b[1] !== SPLIT || b[2] !== SPLIT || b[3] !== SPLIT) {
      return 0;
    }
    const parts = [];
    let at = 4;
    while (at < b.length) {
      if (b.length - at < 5) {
        return -1;
      }
      const place = b[at];
      const n = (b[at + 1] | (b[at + 2] << 8) | (b[at + 3] << 16) | (b[at + 4] << 24)) >>> 0;
      at += 5;
      if (n === 0 || n > b.length - at || (place !== 0xff && place >= MAX_PEERS)) {
        return -1;
      }
      parts.push([place, message.slice(at, at + n)]);
      at += n;
    }
    if (parts.length === 0) {
      return -1;
    }
    for (const [place, part] of parts) {
      const to = place === 0xff ? this.here("guest")
                                : this.here("peer").filter((p) => p.deserializeAttachment().slot === place);
      for (const p of to) {
        try {
          p.send(part);
        } catch {
          // closing
        }
      }
    }
    return 1;
  }

  async pass(ws, message) {
    if (typeof message === "string") {
      // a listed host's refresh ("list"), a host's "drop ..."; the games send nothing else as text
      const a = ws.deserializeAttachment();
      if (message === "list" && a && a.role === "host" && !a.replaced && a.list) {
        await this.report();
      }
      const d = a && a.role === "host" && !a.replaced ? message.match(DROP) : null;
      if (d && (d[1] === "guest" || Number(d[2]) < MAX_PEERS)) {
        this.drop(ws, a, d[1] === "guest" ? "guest" : "peer", d[2] === undefined ? null : Number(d[2]), !!d[3]);
        await this.report();
      }
      return;
    }
    const { role, replaced, slot } = ws.deserializeAttachment();
    if (replaced || role === "watch") {
      return; // a watcher's go nowhere (it sends none)
    }
    // (no cut after ROOM_LIFETIME_MS any more: a long evening's room went on through a reconnect anyway, and
    // the games now take a 1009 as "come back at once"; the Lobby still lets a listing go after it)
    if (message.byteLength > MAX_MESSAGE) {
      this.cut(ws, 1009, "Too long", "too_long");
      return;
    }
    if (role === "peer" && message.byteLength > MAX_FRAMED) {
      this.cut(ws, 1009, "Too long", "too_long");
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
    if (role === "host" && ws.deserializeAttachment().caps) {
      const r = this.split(ws, message);
      if (r < 0) {
        this.cut(ws, 1007, "Bad split", "bad_split");
        return;
      }
      if (r > 0) {
        return;
      }
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
          this.cut(ws, 1009, "Too long", "too_long");
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
    try {
      await this.leave(ws, code);
    } catch (e) {
      this.tally("error.room_close");
      await this.flushCounts();
      throw e;
    }
  }

  async leave(ws, code) {
    const a = ws.deserializeAttachment();
    const { role, replaced, slot } = a;
    // how it ended (unless the relay ended it and counted it then: ended): a leave (1000, or 1005 no code), the
    // other end going away (1001: a restart, a page closed), a drop (1006 no close at all, 1011 an error,
    // 1015 TLS), a reconnect taking its place (replaced), anything else
    const how = replaced ? "replaced" : code === 1000 || code === 1005 ? "clean" : code === 1001 ? "away"
              : code === 1006 || code === 1011 || code === 1015 ? "dropped" : "other";
    try {
      ws.serializeAttachment({ ...this.ended(a, how), replaced: true, n: 0 }); // (no longer counted while it closes)
    } catch {
      // already closed
    }
    await this.meter(a.n || 0); // (what came in on it since the Lobby was last told)
    try {
      ws.close(code === 1005 ? 1000 : code, "Bye");
    } catch {
      // already closed
    }
    if (replaced) {
      return; // its place is the new connection's: nobody left
    }
    if (typeof a.room === "number" && this.everyone().every((o) => (o.deserializeAttachment() || {}).replaced)) {
      this.tally("life." + span(Date.now() - a.room)); // (the room's last one gone: how long it had anyone in it)
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
    try {
      const carried = carriedCounts(request); // (the Worker's: the health counts)
      if (carried) {
        await this.addHealth(carried);
      }
      return await this.serve(request);
    } catch (e) {
      try {
        await this.addHealth({ "error.lobby": 1 });
      } catch {
        // (as it was)
      }
      throw e;
    }
  }

  async serve(request) {
    const url = new URL(request.url);
    const now = Date.now();
    if (url.pathname === "/meter" || url.pathname === "/budget") {
      return this.day(request, url);
    }
    if (request.method === "POST" && url.pathname === "/count") {
      // a room's health counts on their own (Room.flushCounts): a request, so in the day's meter
      let r;
      try {
        r = await request.json();
      } catch {
        return say("bad: not a count", 400);
      }
      await this.addHealth(r && r.h);
      await this.charge(20);
      return say("ok");
    }
    if (request.method === "GET" && (url.pathname === "/health" || url.pathname === "/health.json")) {
      return this.health(url.pathname === "/health.json");
    }
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
      const old = (await this.ctx.storage.get("r:" + r.id)) || null;
      const players = r.players > 0 ? Math.min(Number(r.players) || 0, 2 + MAX_SEATS + MAX_WATCHERS) : 0;
      if (players === 0) {
        await this.ctx.storage.delete("r:" + r.id);
      } else {
        const list = r.list || null;
        await this.ctx.storage.put("r:" + r.id, {
          players,
          list,
          listedAt: list ? (old && old.list && old.list.code === list.code ? old.listedAt : now) : 0,
          at: now,
        });
      }
      // the health counts it brought, and players and rooms online now (for the day's peak): this room's
      // change (a room gone stale was already let go of by the last count, liveFrom)
      const was = old && now - old.at <= ROOM_LIFETIME_MS ? old.players : 0;
      await this.addHealth(r.h, { players: players - was, rooms: (players > 0 ? 1 : 0) - (was > 0 ? 1 : 0) });
      return say("ok");
    }
    if (url.pathname !== "/rooms") {
      return say("Not here", 404);
    }
    const game = url.searchParams.get("game") || "";
    const v = url.searchParams.get("v") || "";
    const { online, listed: all } = await this.online(now);
    const listed = all.filter((r) => now - r.at <= LIST_TTL_MS && r.list.game === game && r.list.v === v);
    listed.sort((a, b) => b.listedAt - a.listedAt);
    let text = "rooms " + online + "\n";
    for (const r of listed.slice(0, MAX_LISTED)) {
      const playing = r.list.playing || 1;
      text += r.list.code + " " + playing + "/" + (r.list.max || 2) + " " + encodeURIComponent(r.list.name || "") + "\n";
    }
    return say(text);
  }

  // every room's count: players and rooms online, the listed rooms; rooms that stopped telling it let go of,
  // and the health's own running count set right (liveFrom)
  async online(now) {
    const rooms = await this.ctx.storage.list({ prefix: "r:" });
    const stale = [];
    const listed = [];
    let online = 0;
    let n = 0;
    for (const [key, r] of rooms) {
      if (now - r.at > ROOM_LIFETIME_MS) {
        stale.push(key);
        continue;
      }
      online += r.players;
      n++;
      if (r.list) {
        listed.push(r);
      }
    }
    if (stale.length > 0) {
      await this.ctx.storage.delete(stale.slice(0, 128));
    }
    await this.addHealth(null, null, { players: online, rooms: n });
    return { online, rooms: n, listed };
  }
}

// --- the relay's health (HEALTH_GROUPS, above): "h:<YYYY-MM-DD>" holds a UTC day's
// { c: counts, peak: { players, rooms }, live: { players, rooms } } (live: players and rooms online as the
// reports say, kept to find the day's peak, set right whenever every room's count is read). Days older than
// HEALTH_DAYS go when a new day's is made. Never a code, name, address or ticket.

// today's record, made (from the newest before it: who's online carries over) when it's the first
Lobby.prototype.healthToday = async function () {
  const day = today();
  const rec = await this.ctx.storage.get("h:" + day);
  if (rec) {
    return [day, rec];
  }
  const all = await this.ctx.storage.list({ prefix: "h:" });
  const keep = new Date(Date.now() - (HEALTH_DAYS - 1) * 86400e3).toISOString().slice(0, 10);
  const old = [];
  let last = null;
  for (const [key, r] of all) {
    if (key.slice(2) < keep) {
      old.push(key);
    } else if (key.slice(2) < day && (!last || key > last[0])) {
      last = [key, r];
    }
  }
  if (old.length > 0) {
    await this.ctx.storage.delete(old.slice(0, 128));
  }
  const live = last && last[1].live ? { ...last[1].live } : { players: 0, rooms: 0 };
  return [day, { c: {}, peak: { ...live }, live }];
};

// counts added to today's, who's online moved by a change (delta) or set (set); one write, none if nothing
Lobby.prototype.addHealth = async function (counts, delta = null, set = null) {
  const c = addCounts({}, counts);
  const moves = delta && (delta.players !== 0 || delta.rooms !== 0);
  if (Object.keys(c).length === 0 && !moves && !set) {
    return;
  }
  const [day, rec] = await this.healthToday();
  const before = JSON.stringify(rec);
  rec.c = addCounts(rec.c || {}, c);
  rec.live = rec.live || { players: 0, rooms: 0 };
  rec.peak = rec.peak || { players: 0, rooms: 0 };
  if (set) {
    rec.live = { players: set.players, rooms: set.rooms };
  } else if (moves) {
    rec.live = { players: Math.max(0, rec.live.players + delta.players), rooms: Math.max(0, rec.live.rooms + delta.rooms) };
  }
  rec.peak = { players: Math.max(rec.peak.players, rec.live.players), rooms: Math.max(rec.peak.rooms, rec.live.rooms) };
  if (JSON.stringify(rec) !== before) {
    await this.ctx.storage.put("h:" + day, rec);
  }
};

// units (twentieths of a request) added to the day's meter (Lobby.prototype.day)
Lobby.prototype.charge = async function (units) {
  const key = "d:" + today();
  const was = await this.ctx.storage.get(key);
  if (!was) {
    // (the day's first: two days ago's goes, as in day())
    await this.ctx.storage.delete("d:" + new Date(Date.now() - 2 * 86400e3).toISOString().slice(0, 10));
  }
  await this.ctx.storage.put(key, (was || 0) + units);
};

// GET /health (text) or /health.json: the last HEALTH_DAYS days, newest first, and who's online now
Lobby.prototype.health = async function (json) {
  const now = await this.online(Date.now());
  const all = await this.ctx.storage.list({ prefix: "h:" });
  const day0 = Date.parse(today() + "T00:00:00Z");
  const days = [];
  for (let i = 0; i < HEALTH_DAYS; i++) {
    const day = new Date(day0 - i * 86400e3).toISOString().slice(0, 10);
    const rec = all.get("h:" + day) || { c: {}, peak: { players: 0, rooms: 0 } };
    const counts = {};
    for (const [g, names] of Object.entries(HEALTH_GROUPS)) {
      counts[g] = {};
      for (const n of names) {
        counts[g][n] = (rec.c || {})[g + "." + n] || 0;
      }
    }
    days.push({ day, counts, peak: { players: (rec.peak || {}).players || 0, rooms: (rec.peak || {}).rooms || 0 } });
  }
  if (json) {
    const body = JSON.stringify({ relay: "Paper Cut relay", now: { players: now.online, rooms: now.rooms }, days });
    return new Response(body + "\n", { headers: { ...TEXT, "content-type": "application/json; charset=utf-8" } });
  }
  return say(healthText(now, days));
};

// the report as text: a block a day (docs/ONLINE_RELAY.md, "The relay's health")
function healthText(now, days) {
  const list = (o, names = Object.keys(o)) => names.map((n) => n.replace(/_/g, " ") + " " + o[n]).join(", ");
  const spans = (o) => SPANS.map((s, i) => SPAN_NAMES[i] + " " + o[s]).join(", ");
  const sum = (o) => Object.values(o).reduce((a, b) => a + b, 0);
  let text = "Paper Cut relay: health, per UTC day (counts only: nothing personal)\n";
  text += "now: " + now.online + " players in " + now.rooms + " rooms\n";
  days.forEach((d, i) => {
    const c = d.counts;
    text += "\n" + d.day + (i === 0 ? " (today, so far)" : "") + "\n";
    if (sum(c.join) + sum(c.refused) + sum(c.end) + sum(c.error) + sum(c.guard) === 0 && d.peak.players === 0) {
      text += "  nothing\n";
      return;
    }
    const r = c.rooms;
    text += "  rooms: " + r.opened + " opened (" + r.listed + " listed, " + r.private + " private; " + r.sync +
            " sync, " + r.lockstep + " lockstep; " + r.split + " split)\n";
    text += "  room lifetimes: " + spans(c.life) + "\n";
    text += "  peak: " + d.peak.players + " players, " + d.peak.rooms + " rooms\n";
    text += "  joins: " + list(c.join) + "\n";
    const refused = Object.keys(c.refused).filter((k) => c.refused[k] > 0);
    text += "  refused: " + sum(c.refused) + (refused.length > 0 ? " (" + list(c.refused, refused) + ")" : "") + "\n";
    text += "  reconnects: " + c.resume.asked + " asked: " + c.resume.ok + " resumed (" + c.resume.took_over +
            " while the old connection was still there), " + c.resume.expired + " expired, " + c.resume.refused +
            " refused\n";
    text += "  ends: " + list(c.end) + "\n";
    text += "  player connections: " + spans(c.session) + "\n";
    text += "  guards: " + list(c.guard) + ", slow room " + c.refused.slow_room + ", day full " + c.refused.today +
            ", removed back " + c.refused.removed + ", blocked back " + c.refused.blocked + "\n";
    text += "  errors: " + list(c.error) + "\n";
  });
  return text;
}

// The relay's day as the rooms count it (Room.meter): "d:<YYYY-MM-DD>" holds the day's units (twentieths of
// a request), this object's own requests among them; a day's count is dropped two days on.
Lobby.prototype.day = async function (request, url) {
  const day = today();
  const key = "d:" + day;
  let units = (await this.ctx.storage.get(key)) || 0;
  units += 20; // (this request: counted with a count, said with an answer)
  if (request.method === "POST" && url.pathname === "/meter") {
    let r;
    try {
      r = await request.json();
    } catch {
      return say("bad: not a count", 400);
    }
    const add = Number(r && r.units);
    if (!(add > 0) || add > 1e9) {
      return say("bad: not a count", 400);
    }
    units += add;
    if (units === add + 20) {
      // (the day's first: two days ago's goes)
      const old = new Date(Date.now() - 2 * 86400e3).toISOString().slice(0, 10);
      await this.ctx.storage.delete("d:" + old);
    }
    await this.ctx.storage.put(key, units);
  }
  return say("budget " + Math.ceil(units / 20) + " " + dayLimit(this.env) + " " + day);
};

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
const NOTES_MINE_A_MAP = 3; // one author's on a map (a device's code; a note from before codes: its address)
const NOTES_ADDRESS_A_MAP = 6; // one address's on a map, whatever its codes (a code can be made anew)
const NOTE_CODE = /^[0-9a-f]{32,64}$/; // a device's code (0.5.1): 128 bits or more, hex
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
//                           "take <MAP> <ID>"                        -> "ok taken" (0.5.1)
// A refusal is a status other than 200 and "<WHY>: <words>" (mine, today, own,
// rated, gone). Take: only by who left it, and it frees that note's place in
// the three a map; an older relay answers it "bad: add or rate" (400), which
// the game says it can't do yet.
//
// A 0.5.1 game adds its device's code (32-64 hex) as a last word to add and
// take: "add <MAP> <X> <Y> <Z> <PHRASE> <WORD> <CODE>", "take <MAP> <ID> <CODE>".
// Such a note's author is the code's hash (noteCodeHash; `by`), its address's
// hash kept beside it (`a`) for the limits: three a map by code, six a map by
// address, ten adds a day and one rating each by address, and no rating of a
// note left from your own address. It is taken back by the same code from any
// address. A note from before (no `a`: its author is its address's hash) is
// taken back from that address, as before. An older relay answers a coded add
// or take "bad: add or rate" (400): the game then leaves the note without its
// code, as before. Old games' requests are answered exactly as they were.
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
    if (body[0] === "add" && (body.length === 7 || body.length === 8)) {
      const [x, y, z] = [body[2], body[3], body[4]].map((t) => int(t, -NOTE_COORD, NOTE_COORD));
      const t = int(body[5], 0, 255);
      const w = int(body[6], 0, 1023);
      if (x === null || y === null || z === null || t === null || w === null ||
          (body.length === 8 && !NOTE_CODE.test(body[7]))) {
        return say("bad: not a note", 400);
      }
      // the author: the device's code's hash (0.5.1), else (an older game) the address's
      const by = body.length === 8 ? await noteCodeHash(body[7], this.env) : hash;
      if (notes.filter((n) => n.by === by).length >= NOTES_MINE_A_MAP ||
          notes.filter((n) => (n.a || n.by) === hash).length >= NOTES_ADDRESS_A_MAP) {
        return say("mine: you have as many notes here as can be", 409);
      }
      if (!(await spend(storage, "add", hash, NOTE_ADDS_A_DAY))) {
        return say("today: no more notes in this area today", 429);
      }
      const id = newTicket().slice(0, 12);
      notes.push(by === hash ? { id, x, y, z, t, w, s: 0, at: Date.now(), by }
                             : { id, x, y, z, t, w, s: 0, at: Date.now(), by, a: hash });
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
      if (notes[i].by === hash || notes[i].a === hash) {
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
    if (body[0] === "take" && (body.length === 3 || body.length === 4)) {
      if (!NOTE_ID.test(body[2]) || (body.length === 4 && !NOTE_CODE.test(body[3]))) {
        return say("bad: not a note to take", 400);
      }
      const i = notes.findIndex((n) => n.id === body[2]);
      if (i < 0) {
        return say("gone: that note isn't here any more", 404);
      }
      // a coded note: by the same code, from anywhere; one from before codes: from its address
      const author = notes[i].a === undefined ? hash
                   : body.length === 4 ? await noteCodeHash(body[3], this.env) : null;
      if (notes[i].by !== author) {
        return say(notes[i].a === undefined ? "own: only who left a note can take it back"
                                            : "own: only the device that left a note can take it back", 403);
      }
      await storage.delete("v:" + notes[i].id);
      notes.splice(i, 1);
      if (notes.length > 0) {
        await storage.put("m:" + map, notes);
      } else {
        await storage.delete("m:" + map);
      }
      return say("ok taken");
    }
    // (the words 0.4.x's relay said: a "take" to it gets these, and the game knows them)
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
