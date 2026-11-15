#pragma once
/* An on-screen keyboard for Paper Cut's own text fields with a controller (port/settings/features/pad_keyboard.cpp).
 *
 * The launcher's, the settings page's and their windows' text fields (room codes, names, save-state labels,
 * search, mod and challenge names, the Lua console...) are Dear ImGui's, which takes letters only from a
 * keyboard. With a controller in hand (the last press a controller's: portMenuPadInUse), a field being typed in
 * puts a keyboard under it: the D-pad or the left stick moves over the keys, confirm (A, a Nintendo controller's
 * A) types one, back deletes one, the top face button types a space, the third face button is Shift, the left
 * stick pressed in switches letters and symbols, the shoulders move the text cursor, and Start or Back is done
 * (Enter: a field that acts on Enter, a room code's, does). Meanwhile the controllers are the keyboard's alone:
 * the page doesn't move, Paper Cut's actions don't fire, the game gets nothing. A key on the keyboard (or a
 * click) gives the field back to the keyboard and the keyboard goes. Phones and tablets without a controller
 * have the system's own keyboard (Dear ImGui asks SDL for it); the Start menu's pages have keyboards of their own
 * (port/include/port/menu_typing.h).
 *
 * $TTYD_PAD_KEYBOARD_TEST=1 (tests): logs "[pad-keyboard] shown", "[pad-keyboard] typed <c>", "... done" and
 * "... hidden". */
#ifdef __cplusplus
extern "C" {
#endif

/* Every SDL event, early (port/settings/settings_ui.cpp's portSettingsEvent, after the menus' own confirm and
 * back): a controller's buttons while the keyboard is up are its. Nonzero: used. */
int portPadKeyboardEvent(const void* sdlEvent);
/* Once a frame, after every Dear ImGui window of it (the game's frame, the launcher's): the keyboard drawn when
 * a field is being typed in with a controller in hand. */
void portPadKeyboardDraw(void);
/* The keyboard is up (the game gets no controller input meanwhile: bindings.cpp's portKeyboardApplyPad). */
int portPadKeyboardShown(void);

#ifdef __cplusplus
}
#endif
