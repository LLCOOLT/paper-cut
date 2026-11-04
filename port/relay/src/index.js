// Paper Cut's online relay: one room per code, two players in it, each one's
// bytes passed to the other unchanged (port/src/common/online_net_pc.c's
// stream). A Cloudflare Worker and a Durable Object per room, on the free plan
// (docs/ONLINE_RELAY.md):
// - the WebSocket hibernation API: a room waiting for its guest costs nothing;
// - only incoming messages count (20 to 1), and the games batch their input;
// - the games try a direct connection first, from the addresses passed here,
//   and leave the relay when it works.
//
//   GET /room/<CODE>?role=host|guest  (a WebSocket upgrade)
//
// Text messages from the relay, never forwarded: "you <address>" (the
// player's public address, as Cloudflare sees it), "peer" (the other player
// is in the room), "gone" (they left). Binary messages are the games' own.

const CODE = /^[0-9A-Z]{4,8}$/;
const MAX_MESSAGE = 1 << 20;        // the host's setup (settings and memory card) is the largest
const ROOM_LIFETIME_MS = 6 * 3600e3; // a session longer than this starts again from the lobby

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
    if (role !== "host" && role !== "guest") {
      return new Response("role: host or guest\n", { status: 400 });
    }
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("A WebSocket upgrade, please\n", { status: 426 });
    }
    const room = env.ROOMS.get(env.ROOMS.idFromName(m[1]));
    return room.fetch(request);
  },
};

export class Room {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
  }

  async fetch(request) {
    const role = new URL(request.url).searchParams.get("role");
    const here = this.ctx.getWebSockets(role);
    if (role === "host" && here.length > 0) {
      return new Response("That room has a host\n", { status: 409 });
    }
    if (role === "guest") {
      if (this.ctx.getWebSockets("host").length === 0) {
        return new Response("No one is hosting that room\n", { status: 404 });
      }
      if (here.length > 0) {
        return new Response("That room is full\n", { status: 409 });
      }
    }
    const pair = new WebSocketPair();
    this.ctx.acceptWebSocket(pair[1], [role]);
    pair[1].serializeAttachment({ role, since: Date.now() });
    const address = request.headers.get("CF-Connecting-IP");
    if (address) {
      pair[1].send("you " + address);
    }
    const other = this.ctx.getWebSockets(role === "host" ? "guest" : "host");
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
    const { role, since } = ws.deserializeAttachment();
    if (message.byteLength > MAX_MESSAGE || Date.now() - since > ROOM_LIFETIME_MS) {
      ws.close(1009, "Too long");
      return;
    }
    const other = this.ctx.getWebSockets(role === "host" ? "guest" : "host");
    if (other.length > 0) {
      other[0].send(message);
    }
  }

  async webSocketClose(ws, code) {
    const { role } = ws.deserializeAttachment();
    for (const other of this.ctx.getWebSockets(role === "host" ? "guest" : "host")) {
      other.send("gone");
    }
    try {
      ws.close(code === 1005 ? 1000 : code, "Bye");
    } catch {
      // already closed
    }
  }

  async webSocketError(ws) {
    await this.webSocketClose(ws, 1011);
  }
}
