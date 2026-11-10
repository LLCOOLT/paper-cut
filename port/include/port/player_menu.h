/* The player menu (port/src/common/player_menu_pc.c, docs/PLAYERS.md): players 2-4's Start, and the
 * players' bars.
 *
 * The bars: a row of four along the picture's bottom edge, 1P to 4P, each a quarter of the width (inside
 * the safe area, at every aspect ratio and window size), in seat order; a player's in their colour with
 * their number, face and character, a free seat's a quiet one saying how to join ("Open" online), so the
 * row never shifts as players come and go. Up in a calm field whenever more than one plays (or online
 * co-op, or a controller is there to join). A player's card rises from their bar; their notes (joined,
 * "1P has the menu", "Paused by", a controller away) are said in it. While player 1's Start menu is up on
 * this screen the row goes to the top edge, and only for a note (its help box is along the bottom).
 * Player 1 has a card too: a tap or click on 1P's bar, or Z held and Start (Start alone stays the game's
 * Start menu); its Character strip is Mario's partners (a pick switches, as the partner wheel does).
 *
 * Player 1's Start keeps the game's full Start menu, which pauses everyone (online: the host's
 * pause is everyone's). Players 2-4's Start opens a small menu of their own instead, a panel at
 * their side of the picture; nothing pauses, the others play on, and their character stands still
 * while it is open (their controller's input is the menu's, the game gets nothing pressed; a tap on their bar too):
 *   Character        a strip of the partners: left and right pick one at once (not in Brothers mode: Luigi)
 *   Who's who        ask for another seat (a request online; on one computer at the calm moment),
 *                    and answer another player's request
 *   Emotes           a wheel of the game's bubbles over their character ("!", "?", "...", dizzy, grumpy,
 *                    happy); in the field, Z held and the stick pointed shows one without the menu
 *   Your controls    their controller's buttons (online, on a touch screen: the touch layout)
 *   Your screen      online: options of their own picture only (widescreen, HUD and text size,
 *                    less flashing or shaking, captions)
 *   Leave session    online: leave the room (A twice)
 * No items: the bag is player 1's. It never opens player 1's menu, never pauses or takes another
 * player's input, and closes by itself on a room change, a scene (a moment without Mario's
 * controls), a battle or player 1's Start menu. Online, a co-op guest's own Start (it is player 2-4
 * there) opens it in its own game, never its game's full Start menu.
 *
 * Emotes ride in the player's controller input (analogA, as pings ride in theirs), so they are the
 * same in a replay and reach the host's game with the guest's controller. */
#ifndef PORT_PLAYER_MENU_H
#define PORT_PLAYER_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

struct PADStatus;
/* src/sdk/DEMOPad.c, each pad read, before pings and online's exchange: a menu's input taken (the
   game gets nothing pressed from that controller while it is open), players 2-4's Start never the
   game's, an emote or a partner change put into the player's input */
void portPlayerMenuPad(struct PADStatus* pads);
/* the same, after online's exchange and replays: each player's emote taken off their input (shown
   over their character); the game's input is the same with or without them */
void portPlayerMenuTakePad(struct PADStatus* pads);
/* each frame (second_player_pc.c's portSecondPlayerRoute): the open menus drawn */
void portPlayerMenuFrame(void);
/* second_player_pc.c: player (2-4) joined on this computer (or, the host's, online): their note, a moment,
   at their side ("2P joined: {START} for your menu") */
void portPlayerMenuJoined(int player);
/* online (statesync_pc.c's player message): this game's own player's card, so every game draws it at
   that player's place as this one does (open, page, cursors, the quick wheel, a swap asked, emotes) */
#define PORT_PLAYER_MENU_CARD 8
void portPlayerMenuCard(unsigned char* out);
/* that controller port's (0-3) menu is open; any open (or a quick emote wheel up) */
int portPlayerMenuOpen(int port);
int portPlayerMenuAny(void);
/* anything in the cards' row at the bottom (any game's card, a join note, "1P has the menu") */
int portPlayerMenuBottomUp(void);
/* the players' bars are up this frame (a row of four, 1P to 4P, along the bottom: a seat each, a free one
   saying how to join; at the top edge while player 1's Start menu is up, for a note only) */
int portPlayerMenuBarsUp(void);
/* player 1's Start menu (its pages) is up on this screen: nothing of the port's goes along the bottom then
   (its help box is there) */
int portPlayerMenuStartMenuUp(void);

/* second_player_pc.c for it: player 1..3's (controller port) character: a partner (1-7,
   PARTY_MEMBER_GOOMBELLA...), another follower (its member id), -1 Luigi (Brothers mode), 0 none */
int portCoopCharacterOf(int port);
/* and the Paper Mario 64 partner (port/pm64.h, 1-8) that character is drawn as, 0 none */
int portCoopPm64Of(int port);
/* a Character strip entry: a partner (1-7), or a Paper Mario 64 partner p (1-8, port/pm64.h: a copy of its TTYD
   counterpart, drawn as p) */
#define PORT_STRIP_PM64(p) (8 + (p))
#define PORT_STRIP_PM64_OF(id) ((id) > 8 ? (id) - 8 : 0)
/* the Character strip: entry id for the player of port (1-3): not offered, free (a partner Mario has, or a
   Paper Mario 64 partner that has joined with it, even while another player has it), free to borrow (lent:
   none Mario has is free), another player's, or this player's own */
enum { PORT_STRIP_NONE, PORT_STRIP_FREE, PORT_STRIP_LENT, PORT_STRIP_TAKEN, PORT_STRIP_MINE };
int portCoopPickState(int port, int id);
/* the player (1-4) whose character partner member (1-7) is now (1: the partner nobody walks), 0 none */
int portCoopPartnerPlayer(int member);
/* the join rule on one computer: a controller in port (1-3) that doesn't play is unseated; its Start, at
   any time (not mid-battle: at its end), takes the next free seat (the lowest port nobody plays; the
   controller moves there). The seat's port, -1 none. */
int portCoopUnseated(int port);
int portCoopJoinStart(int port);
/* A controller is never a player for being in a port (SDL puts each one that connects in the next free one:
   a handheld's built-in controls and the first controller paired are player 1's). Port 1-3's plays once it
   joined by Start, or once its seat was given on purpose: Choose player 2's controller, the Controllers
   page's player (portCoopSeatChosen). Let player 2 go and Not playing free the seat at once
   (portCoopSeatLetGo). */
void portCoopSeatChosen(int port);
void portCoopSeatLetGo(int port);
/* Aurora (second_player.cpp): the controller in that port (0-3) as a device, its SDL GUID and serial ("" none:
   no device there, the keyboard, touch, a test's scripted controller); a seat kept for a controller gone is
   its own again only when this matches (holdFrame) */
void portPadIdentity(int port, char* out, int size);
/* a seat swap on one computer moved everything in ports a and b (0-3, portInputSwapPorts): the seats'
   own state (joined, kept for a controller away, seen last frame) goes with their players */
void portCoopSeatsSwapped(int a, int b);
/* second_player_pc.c's field input: the strip entry (a partner 1-7 or PORT_STRIP_PM64) the player of port
   (1-3) picked in their menu this frame (carried in their input, so online the host's game takes a guest's),
   0 none; taken once */
int portPlayerMenuTakePick(int port);
/* The Partner wheel on an online co-op guest's game that plays the partner (port/settings/features/partner_wheel.cpp):
   its own field never takes a switch (the host's game has the party), so the wheel opens while the host's field is
   calm (portPlayerMenuWheelField: 1 now, 0 not now, -1 not such a game: the field's own rule) and its choice (a
   partner 1-7 or PORT_STRIP_PM64: the wheel's ids) goes out as the Character strip's pick does
   (portPlayerMenuWheelPick: 1 taken, 0 not such a game: the wheel's own tap) */
int portPlayerMenuWheelField(void);
int portPlayerMenuWheelPick(int id);

/* bindings.cpp: a game row's binding on that controller port's controller (the game's encoding),
   read-only; port 0 without a controller: its key */
const char* portBindingsPlayerText(int port, int row);

#ifdef __cplusplus
}
#endif

#endif
