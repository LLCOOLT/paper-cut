#!/bin/sh
# Before every deploy of the relay worker: the 0.4.1 release's worker and this one, played the same
# lockstep rooms, room-list requests and every HTTPS request a 0.4.1 game makes (notes, leaderboards,
# daily, raid, bingo: compat.mjs). Exit 0 when they answer the same.
set -e
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git -C "$here" show v0.4.1:port/relay/src/index.js > "$tmp/old.mjs"
cp "$here/../src/index.js" "$tmp/new.mjs"
node "$here/compat.mjs" "$tmp/old.mjs" "$tmp/new.mjs" > "$tmp/out.txt" || { cat "$tmp/out.txt"; exit 1; }
grep -E "events|boards:|ROOM LIST|DIFFERS|FRAMED|framed host|NOTES" "$tmp/out.txt"
