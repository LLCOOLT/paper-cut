# Updates from inside the game

At start Paper Cut asks GitHub for the latest release of `LLCOOLT/paper-cut` (System tab, Check for updates at start). When a newer one is out, the System tab shows it with a link to the release page, and on Windows, macOS and the Linux zip also **Download and install**. Code: `port/settings/features/update_check.cpp` (the check, the download, the checksum) and `port/settings/features/update_install.cpp` (unpacking, the switch-over).

## What is downloaded

Only Paper Cut's own build for this platform, `papercut-<version>-<windows-x64|macos-arm64|linux-x64>.zip` (the release notes' Downloads table), and only from the repository's own release files (`https://github.com/LLCOOLT/paper-cut/releases/download/...`). Never anything made from the game: no textures, mods or game text.

## Checking it

The zip's SHA-256 must match the one the release lists: the sums in its notes, or its `SHA256SUMS.txt` / `SHA256SUMS-<host>.txt` files (`tools/port/release.py` writes both on every release). A release with no sum for the zip, or a zip that doesn't match, is not installed; its SHA-256 is shown.

## Installing it

1. The zip is unpacked, its top folder dropped, into `<install>/update/new/` (on macOS `.papercut-update/` beside the app), and `update/ready` gets the version. Restart to update, or the next start, takes it from there.
2. At the next start, before anything else (`portUpdateApplyStaged`, first in `aurora_main`), each entry of `new/` moves the install's own of that name to `update/old/` and takes its place. On Windows a running program can't be overwritten, but it can be moved aside, so this works on the running `.exe` and its DLLs. On macOS only the app bundle is replaced, at its own path. A move that fails puts back the ones done: the install stays one version.
3. Then the program starts again from the new files, with the same arguments, and the old one ends. `update/old/` is removed at the start after.

The user folder (saves, settings, mods: `%APPDATA%\ttyd`, `~/Library/Application Support/ttyd`, `~/.local/share/ttyd`, or `$TTYD_USER_DIR`) is separate from the install and never touched; the switch-over skips any path that is or holds it. When the install's folder can't be written (Program Files, a read-only volume), there's no button: the release page is the way.

## Where there's no button

- **Phones:** their stores (or sideloading) update them; the check doesn't run there.
- **Flatpak** (`/.flatpak-info` exists): a note that Flathub updates it.
- **AppImage** (`$APPIMAGE` is set): a note to use AppImageUpdate or download the new one.

## Tests

`TTYD_UPDATE_API=http://127.0.0.1:<port>/<file>.json` reads a release in GitHub's shape from this computer (only `127.0.0.1` or `localhost`, and then the zip and sums from there too; checked even with `TTYD_FAST`); a `dev` build sees any version as newer. `TTYD_UPDATE_INSTALL=1` presses Download and install once the release is found.
