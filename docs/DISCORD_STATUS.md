# Discord status

System > Discord status (on by default) shows what you're doing in Paper Cut on
your Discord profile while the Discord app runs on the same computer:

- **details:** the area (Petalburg, Rogueport Sewers, the Great Tree...), from
  the map's three-letter prefix (the table in
  `port/settings/features/discord.cpp`, after `src/data/mapdata.c`'s areas);
  the title screen, the ending and the menus otherwise;
- **state:** In battle, Playing online, Playing co-op (Second player on and a
  player 2 in) or Exploring;
- the time since the game started, and the large image `logo` ("Paper Cut").

The state and the other words are in the menu language; area names are English.

## How it works

No SDK and no library: the game talks to the Discord app's local IPC itself.

| Platform | Where |
| --- | --- |
| Windows | the named pipes `\\.\pipe\discord-ipc-0` .. `9` |
| Linux | unix sockets in `$XDG_RUNTIME_DIR`, `$TMPDIR`, `/tmp`, then each of those + `/snap.discord`, then + `/app/com.discordapp.Discord` (Flatpak): `discord-ipc-0` .. `9` |
| macOS | `$TMPDIR/discord-ipc-0` .. `9` |

A frame is a little-endian u32 opcode, a u32 length, then that much JSON.
Op 0 is the handshake `{"v":1,"client_id":"<id>"}`; op 1 sends
`{"cmd":"SET_ACTIVITY","args":{"pid":<pid>,"activity":{...}},"nonce":"<n>"}`.
Discord's replies are read and dropped (its READY is waited for, a ping
answered, a close noted).

A thread of its own does all of it, so the game never waits on Discord. While
Discord isn't running it tries again quietly every 15 s. The activity is sent
only when it changes, and at most once every 15 s (Discord's own rate limit).
Turning the setting off clears it. Android, iOS and the web build don't have
it at all.

The game's side (the map's name, whether a battle runs) is
`port/src/aurora/discord_aurora.c`.

## The build variable

    cmake ... -DTTYD_DISCORD_APP_ID=<application id>

Paper Cut's own application (`1567620424886657085`) by default. Set it empty
and the feature is built in but does nothing, and its setting is hidden.

## Making the Discord application

1. Sign in at https://discord.com/developers/applications and choose
   **New Application**. Its name is what Discord shows after "Playing":
   name it **Paper Cut**.
2. On **General Information**, copy the **Application ID**: that is
   `TTYD_DISCORD_APP_ID`. (No bot, OAuth2 redirect or secret is needed:
   local IPC uses the ID only.)
3. On **Rich Presence > Art Assets**, upload the logo (at least 512x512,
   PNG or JPG; Paper Cut's icon, `port/assets/icon/papercut-1024.png`) and name the asset
   **logo**. Discord can take a few minutes to start serving a new asset.

## Testing without Discord

A small stand-in that serves a pipe and logs each frame is enough: build with
any `-DTTYD_DISCORD_APP_ID` (`123`), start the stand-in, then the game
(`TTYD_STORY=60 TTYD_MAP=nok_00`), and look for `[discord] connected` in the
game's output and the handshake and SET_ACTIVITY with `"details":"Petalburg"`
in the stand-in's. `$TTYD_DISCORD_IPC` makes the game use that one pipe or
socket instead of Discord's (`//./pipe/discord-ipc-mock` on Windows), so the
stand-in can run beside a real Discord, which holds `discord-ipc-0`.
