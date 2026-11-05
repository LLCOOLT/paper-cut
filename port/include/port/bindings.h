/* Bindings (port/settings/features/bindings.cpp): the keys and controller
 * buttons for the game's buttons and for Paper Cut's own actions, on one page
 * (Controls > Bindings), as other PC games have it.
 *
 *  - The game's buttons (A, B, X, Y, Z, L, R, Start, the D-pad, both sticks'
 *    directions): a key and a second key on the keyboard (player 1), and the
 *    chosen player's controller button or stick direction and a second one
 *    (any button, stick or trigger direction: a trigger as A). Aurora keeps
 *    the first key and the controller's (PADKeyButtonBinding,
 *    PADButtonMapping, PADAxisMapping, saved by PADSerializeMappings per
 *    controller model); the second keys and second buttons are Paper Cut's
 *    settings (key2_*, pad2_<row>_p<player>).
 *  - Paper Cut's actions (below): a key and a second key, each with Ctrl,
 *    Shift, Alt or Cmd/Win held if wanted (Ctrl + F5: the most modifiers
 *    held wins), and two controller bindings, each a button or two held
 *    together (a combo, such as Back + R), in settings.txt (bind_<action>_key,
 *    _key2, _pad, _pad2). Any controller does them.
 *  - Profiles: the whole set under a name (<user>/bindings/<name>.txt), the
 *    controller's part loaded onto the chosen player; each player's active
 *    profile takes their changes, and one can be loaded whenever a kind of
 *    controller connects (<user>/bindings/defaults.ini).
 *
 * The rules: a key or controller button the player gave to one of the game's
 * buttons doesn't also do an action (the game's wins; a combo needs one of
 * its two buttons free); a button that starts a combo still does its own
 * action alone, when it is let go without the combo; the game doesn't see a
 * combo's buttons while both are held. An action that is off online (rewind,
 * save states...) stays off there: its feature decides. */
#ifndef PORT_BINDINGS_H
#define PORT_BINDINGS_H

#ifdef __cplusplus
extern "C" {
#endif

enum PortBindAction {
    PORT_BIND_SETTINGS,      /* F1, Back: the settings page */
    PORT_BIND_FAST_FORWARD,  /* Tab, the right stick pressed in: held (or toggles, as the setting says) */
    PORT_BIND_FF_TOGGLE,     /* none: fast-forward on and off */
    PORT_BIND_REWIND,        /* Backspace (rewind_key), the Rewind button (rewind_pad): held */
    PORT_BIND_PHOTO,         /* P, both sticks pressed in */
    PORT_BIND_SCREENSHOT,    /* F12 */
    PORT_BIND_SAVE_STATE,    /* F5 */
    PORT_BIND_LOAD_STATE,    /* F8 */
    PORT_BIND_FRAME_HOLD,    /* F6: frame advance on and off */
    PORT_BIND_FRAME_STEP,    /* F7: one frame (held: one per key repeat) */
    PORT_BIND_MESSAGE_LOG,   /* L */
    PORT_BIND_DISPLAY_MODE,  /* F11 (and Alt+Enter, fixed) */
    PORT_BIND_VOLUME_DOWN,   /* F9 */
    PORT_BIND_VOLUME_UP,     /* F10 */
    PORT_BIND_MUTE,          /* F3 */
    PORT_BIND_QUICK_SWAP,    /* Q, the left stick pressed in: held (polled) */
    PORT_BIND_WALK_BOOST,    /* Left Shift, the right stick pressed in (walk_boost_key/_pad): held (polled) */
    PORT_BIND_PING,          /* G, Share/Capture: co-op pings, held to aim (polled per player, port/include/port/ping.h) */
    PORT_BIND_LUA_CONSOLE,   /* F4: Lua mods' console (port/settings/features/lua_console.cpp) */
    PORT_BIND_COUNT
};

/* A feature's action: called with down 1 on its press (repeat: a held key's
 * repeat) and 0 on its release; returns nonzero when it did something (the
 * key or button is then nobody else's). Set at start (a static initializer). */
typedef int (*PortBindHandler)(int down, int repeat);
void portBindingsSetHandler(int action, PortBindHandler handler);

/* Every SDL event, first (port/settings/settings_ui.cpp's portSettingsEvent):
 * a key or button being chosen on the page, then the actions. Nonzero: used. */
int portBindingsEvent(const void* sdlEvent);
/* A key or controller button is being chosen on the page (B and Escape don't
 * close it meanwhile). */
int portBindingsCapturing(void);
/* An action's key or controller binding held now (the polled ones: quick swap,
 * walk boost). */
int portBindingsHeld(int action);
/* The same for one player: port 0's keys and controller, or port 1-3's
 * controller (the keyboard is player 1's). */
int portBindingsHeldOn(int action, int port);
/* An action's first key's name ("F12"), or "" without one; its controller
 * binding's ("Back + R"), as printed on player 1's controller, or "". */
const char* portBindingsKeyName(int action);
const char* portBindingsPadName(int action);
/* What player 1's binding is, for the text's button icons (port/include/port/
 * button_markup.h): an action's (action >= 0) or a game row's (action -1;
 * row 0-19: A B X Y Z L R Start, the D-pad's up down left right, the stick's,
 * the C-stick's), its key (pad 0) or controller binding (pad 1). Returns 0
 * none; 1 a key (a the scancode, b its modifiers: 1 Ctrl, 2 Shift, 4 Alt, 8
 * Cmd/Win); 2 a button (a, SDL_GamepadButton); 3 two buttons together (a, b);
 * 4 a stick or trigger direction (a the SDL_GamepadAxis, b its sign). */
int portBindingsWhat(int action, int row, int pad, int* a, int* b);

/* The Start menu's bindings page (port/src/aurora/pause_bindings_aurora.c),
 * player 1's, in the game's windows and font. Rows: the game's buttons and
 * stick directions (portBindingsGameRows of them), then Paper Cut's actions;
 * four columns: 0 the keyboard's key, 1 the second key, 2 the controller's
 * button, 3 the second button.
 *   portBindingsRowName      a row's name (English, for portTrGame)
 *   portBindingsCellText     what a cell is now, in the game's encoding (for
 *                            its font, translated): a key's name, a button's
 *                            ("Cross", "View + RB"), "(none)", or "Press a
 *                            key..." / "Press a button..." while asked
 *   portBindingsAsk          ask for the cell's key or button (5 seconds;
 *                            portBindingsCancel or Escape leave it)
 *   portBindingsBusy         asking, or waiting for everything to be let go
 *                            after: the menu takes no input meanwhile
 *   portBindingsConflict     the cell's binding is also another row's: that
 *                            row's name (English), or NULL
 *   portBindingsResetRow / portBindingsResetAll   the defaults (all: player 1's) */
int portBindingsGameRows(void);
int portBindingsRowCount(void);
const char* portBindingsRowName(int row);
const char* portBindingsCellText(int row, int col);
void portBindingsAsk(int row, int col);
void portBindingsCancel(void);
int portBindingsBusy(void);
const char* portBindingsConflict(int row, int col);
void portBindingsResetRow(int row);
void portBindingsResetAll(void);
/* The question (or the wait after it) is the second screen's Start menu's
 * (port/companion_menu.h): the game plays on under it, so the controllers and
 * the keyboard play nothing until it is done (input_pc.c). */
int portBindingsAskedFromSecondScreen(void);

/* Profiles (names as the Start menu's letters can show them; indexes in the
 * list's order, by name, until it changes):
 *   portBindingsProfileCount / Name     the list (read from the folder once)
 *   portBindingsProfileActive(player)   the player's profile, or -1
 *   portBindingsProfileLoad(i, player)  put in: the keyboard's and actions' part
 *                                       for everyone, the controller's onto the
 *                                       player's; it becomes the player's
 *   portBindingsProfileSaveAs(name, player)  the bindings now (the player's
 *                                       controller) under name (one of that name
 *                                       written over); its index, or -1
 *   portBindingsProfileRename(i, name)  its new index, or -1
 *   portBindingsProfileDelete(i)        nonzero when deleted
 *   portBindingsProfileIsDefault(i, player)  1 it is loaded whenever a
 *                                       controller of the kind the player has
 *                                       connects, 0 not, -1 no controller
 *   portBindingsProfileSetDefault(i, player, on)
 *   portBindingsProfilesRefresh         the folder read again */
int portBindingsProfileCount(void);
const char* portBindingsProfileName(int i);
int portBindingsProfileActive(int player);
int portBindingsProfileLoad(int i, int player);
int portBindingsProfileSaveAs(const char* name, int player);
int portBindingsProfileRename(int i, const char* name);
int portBindingsProfileDelete(int i);
int portBindingsProfileIsDefault(int i, int player);
void portBindingsProfileSetDefault(int i, int player, int on);
void portBindingsProfilesRefresh(void);

#ifdef __cplusplus
}
#endif

#endif
