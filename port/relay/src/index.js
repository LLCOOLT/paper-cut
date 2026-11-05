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
