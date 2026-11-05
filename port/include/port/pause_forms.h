#ifndef PORT_PAUSE_FORMS_H
#define PORT_PAUSE_FORMS_H

/* Start menu forms: what a feature that draws its own section on the full
 * settings page (portSettingsAddSection) shows in the Start menu's Settings
 * tab instead (port/src/aurora/pause_settings_aurora.c), in the game's
 * windows and font.
 *
 * A form adds rows to its category's page (tab: the settings page's tab, the
 * page's category), after the category's plain settings: rows() fills the
 * rows as they are now (a list of backups, the controllers connected), each
 * frame; act() is called with a row's id when it is changed: dir 0 for A
 * (after the second A, for a PORT_PAUSE_ROW_CONFIRM row), -1 or +1 for left
 * and right. act returns a message for the help box (English, translated
 * for the game's text), or NULL. Strings are English and translated as they
 * are shown, except a row's text and value (a device's name, a date), shown
 * as they are.
 *
 * A form or a row can also open a page of its own over the tab
 * (portPauseOpenPage): it takes the pad and draws on the tab's page until its
 * main returns -1 (B, back to the page it came from). The Wardrobe's colour
 * picker is one (port/src/aurora/pause_picker_aurora.c); the controls'
 * bindings page can be another, from a Controls form row.
 *
 * act's message is a constant (its translation is kept by its address): what
 * changes (a count, a path) goes in a row's text or value instead. On the
 * second screen's copy of the menu (gPortCompanionMenu, port/companion_menu.h)
 * the game's state is put back after each of its frames: a row that would
 * change the game (wear a loadout, travel) says so there instead. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_PAUSE_ROW_CONFIRM = 1, /* A twice: "Press A again to ..." in between */
    PORT_PAUSE_ROW_HEADING = 2, /* a heading over the rows after it (not selectable) */
    PORT_PAUSE_ROW_DIM = 4,     /* shown, but not available now (act still says why) */
};

typedef struct PortPauseRow {
    const char* label; /* English; NULL: text instead */
    const char* note;  /* the help box's: English, or NULL */
    char text[64];     /* the label as it is (a name, a date), when label is NULL */
    char value[64];    /* the value on the right, as it is; or valueTr */
    const char* valueTr; /* the value in English, translated (On, Off, 2x...), or NULL */
    int id;
    unsigned flags;
} PortPauseRow;

typedef struct PortPauseForm {
    const char* tab;
    const char* section; /* the full page's section it stands for there (its label), or NULL */
    int (*rows)(PortPauseRow* out, int max);
    const char* (*act)(int id, int dir);
} PortPauseForm;

/* At start (a C++ static initializer in the feature's file): form stays valid. */
void portPauseAddForm(const PortPauseForm* form);

struct WindowWork;
typedef struct PortPausePage {
    void (*open)(int arg);
    int (*main)(struct WindowWork* wp); /* each frame it has the pad: 0, or -1 to go back */
    void (*disp)(struct WindowWork* wp, float x, float y); /* over the tab's page at (x, y) */
    const char* (*help)(void); /* the help box's text now (game's encoding, already wrapped), or NULL */
} PortPausePage;

/* page shown over the Settings or Wardrobe tab, arg for its open */
void portPauseOpenPage(const PortPausePage* page, int arg);
/* the page shown, or NULL (pause_settings_aurora.c, wardrobe_aurora.c) */
const PortPausePage* portPausePage(void);
/* to the page shown: the pad (0 or -1, as its main) and the help box */
int portPausePageMain(struct WindowWork* wp);
void portPausePageDisp(struct WindowWork* wp, float x, float y);

/* The Wardrobe's colour picker (pause_picker_aurora.c), on a cosmetics slot
 * (port/include/port/cosmetics.h); the slot it shows now, for the stage. */
extern const PortPausePage gPortPausePicker;
int portPausePickerSlot(void);
/* The same picker on any one colour (the controller's light): its name
 * (English), and the colour as 0xRRGGBB, read and set. */
void portPauseColourOpen(const char* label, unsigned (*get)(void), void (*set)(unsigned rgb));

/* --- Pages any form can open (port/src/aurora/pause_pages_aurora.c) --- */

/* A text typed in the game's windows: the letters of charset (ASCII) in a
 * grid, then Space (when charset has one), Delete, Paste and Done. done(text)
 * on Done; B leaves it as it was. title is English; text the text to start
 * from, at most max letters. */
void portPauseTextOpen(const char* title, const char* text, int max, const char* charset,
                       void (*done)(const char* text));
#define PORT_PAUSE_CHARS_ADDRESS "0123456789abcdefghijklmnopqrstuvwxyz.:/-_"
#define PORT_PAUSE_CHARS_NAME "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-'!?&+"
/* A page of lines (a log, a QR code and its address, a wizard's step): up
 * and down scroll it when there are more than fit; A, X and Y go to button()
 * ('A', 'X', 'Y'), which returns -1 to close it. B closes it. */
#define PORT_PAUSE_LINE 96
typedef struct PortPauseInfo {
    const char* title; /* English */
    int (*lines)(char (*out)[PORT_PAUSE_LINE], int max); /* the lines now, in the game's encoding */
    int (*button)(int button);                           /* NULL: only B */
    const char* (*help)(void);                           /* English, or NULL */
    int (*qrSize)(void);                                 /* NULL, or a QR code's size in modules (0: none) */
    int (*qrModule)(int x, int y);                       /* its module: 1 dark */
    int bottom;                                          /* opens on its last lines (a log) */
} PortPauseInfo;
void portPauseInfoOpen(const PortPauseInfo* info);
/* game-encoded text wrapped at cols columns (pause_settings_aurora.c) */
void portPauseWrapCols(const char* text, char* out, size_t size, int cols);

#ifdef __cplusplus
}

/* a row of a form: a label (English); its text, value and note filled after when needed */
inline PortPauseRow portPauseRow(const char* label, const char* note, const char* valueTr, int id, unsigned flags = 0) {
    PortPauseRow r{};
    r.label = label;
    r.note = note;
    r.valueTr = valueTr;
    r.id = id;
    r.flags = flags;
    return r;
}
#endif

#endif
