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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const m = url.pathname.match(/^\/room\/([^/]+)$/);
    if (url.pathname === "/" || url.pathname === "/health") {
      return new Response("Paper Cut relay\n", { headers: { "content-type": "text/plain" } });
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
    this.ctx.acceptWebSocket(pair[1], [role]);
    pair[1].serializeAttachment({ role, since: Date.now(), ticket: mine });
    if (role === "watch") {
      this.tellWatchers(null);
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
    return new Response(null, { status: 101, webSocket: pair[0] });
  }

  async webSocketMessage(ws, message) {
    if (typeof message === "string") {
      return; // the games send only binary
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
