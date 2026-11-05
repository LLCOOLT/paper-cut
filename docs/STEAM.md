# Add to Steam

General > Add to Steam puts Paper Cut in the Steam library as a non-Steam game, on Windows and on Linux (a Steam Deck's Desktop Mode included). Code: `port/settings/features/steam_shortcut.cpp`.

- **Steam must be closed.** Steam reads its list at start and writes it back at exit, so a change made while it runs would be lost; Paper Cut checks for a running Steam (on Windows `steam.exe`, on Linux a `steam` or `steamwebhelper` process) and asks you to close it. On a Steam Deck: Desktop Mode, then Steam > Exit.
- **What is written,** for every Steam account that has signed in on this computer (`<Steam>/userdata/<account>`):
  - `config/shortcuts.vdf`: Paper Cut's entry (the program you start, `Paper Cut.exe` on Windows, the AppImage or `papercut` on Linux), added or brought up to date if it is there already. A dated copy of the file as it was, `shortcuts.vdf.papercut-<date>.bak`, is kept beside it first; a file that can't be read is left alone.
  - `config/grid/`: the artwork, made at that moment from Paper Cut's own logo (no picture from the game): the cover (600x900), the wide capsule (920x430), the hero banner (1920x620), the logo and the icon.
  - The controller set-up, in `<Steam>/steamapps/common/Steam Controller Configs/<account>/config/configset_<controller>.vdf`: Steam Input's own "gamepad with mouse trackpad" template on the Steam Deck (the right trackpad moves the mouse, for the settings page and the launcher) and the gamepad templates for the DualSense, DualShock 4, Xbox and Switch Pro controllers. If one of those files already has an entry for Paper Cut (your own set-up), it is left as it is.
- **Undoing it:** remove Paper Cut from the library in Steam, or put the `.bak` copy back while Steam is closed.
