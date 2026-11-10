#pragma once
/* Confirm and back in Paper Cut's own menus (port/settings/features/menu_buttons.cpp).
 *
 * Paper Cut's menus (the launcher, the settings page, the Start menu's Paper
 * Cut tabs and pages, Modes, the Journal's Paper Cut pages) confirm and back
 * out the way the controller in hand does elsewhere:
 *
 *   Xbox, PlayStation, Steam Deck, any other    the bottom button confirms, the right one backs
 *   Switch Pro, Joy-Con, NSO controllers        A (right) confirms, B (bottom) backs: their labels
 *   GameCube (adapter, NSO GameCube)            A confirms, B backs
 *
 * and the third (clear, reset) is the face button left over: the left one,
 * or a GameCube's X. "Swap confirm and back" (Controls > Bindings) swaps the
 * two for every controller; "Menu confirm" and "Menu back" there put either on
 * another face button. The game's own menus follow the same rule through
 * the menu layer (below): every menu, the game's and Paper Cut's, alike.
 *
 * The page's Dear ImGui: confirm and back are its keyboard's Space and Escape
 * (the controller's own face buttons kept from its navigation), so each
 * controller's buttons are its own; every controller connected moves it.
 * The Start menu's pages: their A, B, X and Y are what the player's controller's
 * confirm, back and third buttons are now (portMenuButtons), and their help's
 * {A}, {B}, {X} and {Y} show those buttons (portMenuText). The keyboard's keys
 * stay the game's. */
#ifdef __cplusplus
extern "C" {
#endif

enum { PORT_MENU_NONE = 0, PORT_MENU_CONFIRM = 1, PORT_MENU_BACK = 2, PORT_MENU_THIRD = 3 };

/* a controller button event's (SDL_EVENT_GAMEPAD_BUTTON_DOWN/UP) part in the menus: PORT_MENU_*;
 * PORT_MENU_NONE for any other event */
int portMenuEventRole(const void* sdlEvent);
/* the controller button (SDL_GamepadButton) that is role on the controller in hand (the last one
 * pressed, else player 1's, else the first); id 0: that one, else the controller with that id */
int portMenuRoleButton(int role, unsigned id);
/* the controller in hand's SDL_JoystickID (0: none) */
unsigned portMenuPadId(void);
/* the last press was a controller's (a key or the mouse: 0); before any, a controller is connected */
int portMenuPadInUse(void);
/* the "Swap confirm and back" setting (0 or 1) */
int portMenuSwapped(void);
void portMenuSetSwapped(int on);
/* the player's own confirm or back button (Controls > Bindings, "Menu confirm" and "Menu back"): -1 as
 * the controller has it, else a face button by place (0 the bottom one, 1 the right, 2 the left; the top
 * one switches the Bindings page's view); setting one to the other's button swaps the two, so they are
 * never the same button; the third is the face button left over. Swap confirm and back still swaps them */
int portMenuChosenPlace(int role);
void portMenuSetPlace(int role, int place);
/* confirm and back as the controller has them again (Reset all) */
void portMenuResetButtons(void);

/* the Start menu's Paper Cut pages: a player's (port 0-3) buttonTrg or buttonRep (PAD_BUTTON_*) with
 * its controller's A, B, X and Y as that controller's confirm, back and third buttons (the keyboard's
 * keys as they are) */
unsigned portMenuButtons(int port, unsigned buttons);
/* their help (the game's encoding, or UTF-8): {A}, {B}, {X} and {Y} as the buttons that do them there
 * now, for player 1; text itself when nothing changes, else a copy kept for the run */
const char* portMenuText(const char* text);

/* portMenuButtons without marking a menu up (replays' stop check, traces) */
unsigned portMenuButtonsAsOwn(int port, unsigned buttons);

/* The one back rule. On every controller the button labelled B (a Nintendo controller's B, a PlayStation
 * controller's circle; Menu back and Swap confirm and back move it) backs out of every menu, and A
 * confirms. The game's menus read the game's buttons, so while a menu is up its controller's face buttons
 * are given to the game as the controller's own confirm, back and third (the game's A, B and X), before
 * the input is recorded or sent (a replay or an online battle plays them as they were): the "menu layer".
 * In the field and in battle's actions the bindings are as they are (the hammer stays where it is).
 *
 * A menu marks itself up each frame it reads its buttons (portMenuMark: the Start menu and its pages, the
 * game's lists, shops, choices, the file select, name entry, the battle menus, Paper Cut's pages, the
 * players' cards); the layer is on for a port from the frame after a mark until two frames after the last.
 * A talk's message marks it too (portMenuMarkText: B pages through it, as in the menus), but leaves the
 * button pictures as the field's (a lesson's "press B" there is the hammer's button).
 * A face button held while the layer turns on or off gives the game nothing until it is let go (no press
 * the player didn't make). Keys, the touch controls and the mouse's and Esc's B are the game's buttons as
 * they are. The game's button pictures follow a menu's layer (button_prompts.cpp), and Esc and a right
 * click back out wherever it is on (portMenuBackOpen). */
void portMenuMark(unsigned ports); /* ports: a mask, bit p port p */
void portMenuMarkText(unsigned ports);
int portMenuLayerOn(int port);
int portMenuLayerPictures(int port); /* on for a menu, not a message alone: the pictures follow it */
void portMenuLayerFrame(void); /* DEMOPadRead, once a frame before portMenuPadLayer */
struct PADStatus;
void portMenuPadLayer(struct PADStatus* pad); /* DEMOPadRead (Aurora): the layer applied */
/* the layer's map for a port now: to[i] the game's face button (0-3: A B X Y) the controller's button that
 * is the game's i out of the menus gives in them; 0 when the layer is off or changes nothing */
int portMenuLayerMap(int port, int to[4]);

/* Dear ImGui (the launcher and the settings page): each frame before their windows, while they
 * have the controls */
void portMenuUiFrame(void);
/* each SDL event (portSettingsEvent): confirm and back into Dear ImGui */
void portMenuUiEvent(const void* sdlEvent);
/* this frame: role's button pressed on the controller in hand (Dear ImGui's keys: the third's, as clear) */
int portMenuUiPressed(int role);

#ifdef __cplusplus
}
#endif
