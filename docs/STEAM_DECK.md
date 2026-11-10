# Steam Deck and PlayStation controllers

## The Steam Deck's profile

Paper Cut knows it runs on a Deck from Steam's `SteamDeck=1`, or in Desktop Mode from the board's name (Valve's Jupiter, LCD, or Galileo, OLED); `$TTYD_STEAM_DECK=0/1` overrides it. Code: `portSteamDeck` in `port/src/aurora/settings_aurora.c`.

- **Layout**: on the first start (no `settings.txt` yet), full screen, the 16:10 picture with the HUD at its edges, and the message windows a little larger for the 7-inch screen. The settings page changes them like any other.
- **Glyphs**: the Deck's own buttons (dark grey, light letters, L1/R1 and L2/R2) in the game's prompts and on the settings page (`port/settings/button_glyphs.cpp`).
- **Controls**: General > Add to Steam sets Steam Input's gamepad template with the right trackpad as a mouse, for the settings page and the launcher (docs/STEAM.md).
- **Save before sleep** (Saves; on by default, on any Linux): before the system sleeps (the power button, the lid of a laptop), where you are is written as quick resume's save, and the settings with it, so a battery that runs flat in its sleep loses nothing; Continue brings you back. Paper Cut hears of the sleep from logind (its PrepareForSleep signal, through `gdbus monitor`) and holds the sleep back until the save is written with a delay lock (`systemd-inhibit --mode=delay`; logind waits at most its InhibitDelayMaxSec, 5 seconds by default). The lock is taken again on waking, and goes when Paper Cut quits. Code: `port/settings/features/sleep_save.cpp`.

## DualSense and DualShock 4

- **Controller light** (Controls) has **The partner's colour, red in Peril**: Goombella's pink, Koops's blue, Bobbery's navy, Yoshi's green, Flurrie's violet, Vivian's purple, Ms. Mowz's rose, and red when Mario is in Danger (5 HP or less, a slow pulse) or Peril (1 HP, a fast one). Other players' controllers keep their player's colour. Code: `port/settings/features/controller_light.cpp`.
- **Touchpad scrolls menus** (Controls, on by default): with a menu up, a swipe on the touchpad tilts the stick against the finger's move for that frame, so the map pans and the Journal and the game's lists scroll under the finger, as on a phone. A swipe across a tenth of the pad in a frame is the stick all the way. Nothing happens in play. Code: `portMenuTouchpad` in `port/src/common/menu_tap_pc.c`.
