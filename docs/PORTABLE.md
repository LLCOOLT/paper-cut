# Portable mode

With a file named `portable.txt` beside the program, Paper Cut keeps everything it writes beside the program too: the settings, memory cards, save library, backups, mods, photos, replays (`User`) and the graphics caches (`Cache`). Nothing goes to the system's folder for app data, so the whole folder can live on a USB stick or be copied to another computer. Code: `port/src/aurora/portable_aurora.c`, `port/settings/features/portable.cpp`.

"Beside the program" is the folder holding `Paper Cut.exe` on Windows, the AppImage (or the binary) on Linux, and the `.app` on macOS. Phones and tablets have no such place; there the section isn't shown.

- The file's contents don't matter; an empty `portable.txt` works.
- `$TTYD_USER_DIR` and `$TTYD_CACHE_DIR` still win when they are set.
- Saves > Portable mode shows where the settings and saves are. **Make this copy portable** copies the current user folder into `User` beside the program and writes `portable.txt`; it applies from the next start (Restart now). **Leave portable mode** removes `portable.txt`; `User` stays where it is, and the next start uses the system's folder again.
- Updates leave `User` alone, as they do any user folder.
- The program's folder must be writable (not `C:\Program Files`).
