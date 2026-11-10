# Updates from inside the game

At start Paper Cut asks GitHub for the latest release of `LLCOOLT/paper-cut` (General tab, Check for updates at start). This happens on computers and on phones (from 0.4.1). When a newer one is out, the General tab (and the launcher's About tab) shows it with a link to the release page; on Windows, macOS and the Linux zip also **Download and install**, on Android a Download and install that ends in Android's own install dialog, and on iOS buttons that add Paper Cut's AltStore or SideStore source ([Phones](#phones)). Code: `port/settings/features/update_check.cpp` (the check, the download, the checksum) and `port/settings/features/update_install.cpp` (unpacking, the switch-over).

## What is downloaded

Only Paper Cut's own build for this platform, `papercut-<version>-<windows-x64|macos-arm64|linux-x64>.zip` (the release notes' Downloads table), and only from the repository's own release files (`https://github.com/LLCOOLT/paper-cut/releases/download/...`). Never anything made from the game: no textures, mods or game text.

## Checking it

The zip's SHA-256 must match the one the release lists: the sums in its notes, or its `SHA256SUMS.txt` (`tools/port/release.py` makes both again from the files on the release each time a runner adds to it; any `SHA256SUMS*.txt` is read, as older releases had `SHA256SUMS-<host>.txt`). A release with no sum for the zip, or a zip that doesn't match, is not installed; its SHA-256 is shown.

What the check covers: the sum comes from the same GitHub release as the zip, so it catches a damaged or cut-short download and an asset swapped on the way, but not a release that was itself made or changed by someone else (a stolen account or token). The releases are not signed with a key of their own yet. Downloads only ever come from `LLCOOLT/paper-cut`'s own releases over HTTPS, and **Install updates automatically** is off by default for that reason: with it off, nothing is installed until the player presses Download and install. On Android the APK must also carry the same signing key as the installed app (below).

## Installing it

1. The zip is unpacked, its top folder dropped, into `<install>/update/new/` (on macOS `.papercut-update/` beside the app), and `update/ready` gets the version. Restart to update, or the next start, takes it from there.
2. At the next start, before anything else (`portUpdateApplyStaged`, first in `aurora_main`), each entry of `new/` moves the install's own of that name to `update/old/` and takes its place. On Windows a running program can't be overwritten, but it can be moved aside, so this works on the running `.exe` and its DLLs. On macOS only the app bundle is replaced, at its own path. A move that fails puts back the ones done: the install stays one version.
3. Then the program starts again from the new files, with the same arguments, and the old one ends. `update/old/` is removed at the start after.

The user folder (saves, settings, mods: `%APPDATA%\ttyd`, `~/Library/Application Support/ttyd`, `~/.local/share/ttyd`, or `$TTYD_USER_DIR`) is separate from the install and never touched; the switch-over skips any path that is or holds it. When the install's folder can't be written (Program Files, a read-only volume), there's no button: the release page is the way.

## Installing updates automatically

**Install updates automatically** (General, and the Start menu's Settings tab; off by default) does the same without the button: when the start's check finds a newer release, its zip is downloaded quietly in the background, checked against the release's SHA-256 in the same way, and staged as above. It goes in place at the next start, never mid-game. While online co-op or a replay runs, nothing is downloaded: a download under way stops, and starts again once it's over. A download that fails, or doesn't match its checksum, isn't tried again until the next start; the General tab says why. The setting is only there where Download and install is. At the start the update is installed, the What's new window says so (`port/src/aurora/whats_new_aurora.c`).

## What's new

The first start of a newer version (however it was installed) shows its **What's new** window once, in the game's own windows over the title screen, the file select or the field, half a second after the screen settles: one screen of this version's highlights, and where the full list is. A or B (or a click or tap) closes it. It isn't shown on the very first start, during online co-op or a replay, or in a dev build. The highlights and then the full list of new features and fixes are on the General tab (**What's new**) and in the Start menu's Settings tab (General > **What's new**), scrolled with up and down (a page with left and right, the mouse wheel, or a click above or below the middle). The list is made from the version's release notes when the game is built ([RELEASING.md](RELEASING.md); `tools/port/whats_new.py`, `port/settings/features/whats_new.cpp`, `port/src/aurora/whats_new_aurora.c`). `$TTYD_WHATS_NEW=0` never shows the window, `=1` shows it at this start.

## Phones

Each phone app plays one game version (`PORT_GAME_ID`: G8MJ01 jp, G8ME01 us, G8MP01 eu), and its update is that version's app from the release. The check is the same as on computers: quiet on errors, nothing for a `dev` build.

**Android.** Download and install fetches `papercut-<version>-<us|jp|eu>-android-arm64.apk` from the release into the app's cache folder (with the same progress bar), and checks its SHA-256 against the release's sums exactly as on computers (`SHA256SUMS.txt` has the APKs). A checked APK shows **Install**, which hands it to Android's own installer (a `PackageInstaller` session that always requires the player's action, `TtydActivity.installApk`). The player taps Install in Android's dialog; nothing is ever installed silently, and the game closes while Android replaces it. The first time, Android needs the app allowed to install apps: Paper Cut opens Android's **Install unknown apps** screen for itself (the manifest asks for `REQUEST_INSTALL_PACKAGES`), and the player comes back and presses Install again. **Install updates automatically** on Android only downloads and checks the APK in the background; installing still waits for Install and Android's dialog.

Android installs an update over the earlier app only while both are signed with the same key. Releases keep one key for that (the release machine's debug key, `port/android/app/build.gradle`); an APK built and signed by someone else, or after the key changed, can't update it: Android refuses, the game says so, and the old app has to be uninstalled first (which deletes its saves: copy them off first).

**iOS and iPadOS.** An app can't install itself there. Every release carries an AltStore / SideStore source, `altstore-source.json` (written by `tools/port/release.py` on the Mac run, from the IPAs and the version; `release.py VERSION --altstore-source --out DIR` writes it alone from a folder of IPAs). It lists the three apps (one per game version, bundle IDs `io.github.ttyd-port.jp`/`.us`/`.eu`) with the release's IPA, size, date and its notes' Highlights. Its stable address is always the newest release's:

`https://github.com/LLCOOLT/paper-cut/releases/latest/download/altstore-source.json`

Add it once in AltStore (Sources, +) or SideStore, and they offer each new version as an update. The game's notice has **Add to AltStore** and **Add to SideStore** buttons that do it (`altstore://source?url=...`, `sidestore://source?url=...`), beside the release page's link. Sideloadly has no sources: download the new IPA from the release page.

## Where there's no button

- **Flatpak** (`/.flatpak-info` exists): a note that Flathub updates it.
- **AppImage** (`$APPIMAGE` is set): a note to use AppImageUpdate or download the new one. The AppImage, `Paper_Cut-<version>-x86_64.AppImage` (named as the AppImage catalog lists it), carries its update information: AppImageUpdate (or a software centre that reads it) fetches the newest release's AppImage through the `Paper_Cut-<version>-x86_64.AppImage.zsync` beside it. AppImages before 0.5.0 (`papercut-<version>-linux-x64.AppImage`) carry none: their notice is the same, and the new one is downloaded by hand once.

## Tests

`TTYD_UPDATE_API=http://127.0.0.1:<port>/<file>.json` reads a release in GitHub's shape from this computer (only `127.0.0.1` or `localhost`, and then the zip and sums from there too; checked even with `TTYD_FAST`); a `dev` build sees any version as newer. `TTYD_UPDATE_INSTALL=1` presses Download and install once the release is found; `TTYD_SET_update_auto=1` turns on Install updates automatically for the run instead.
