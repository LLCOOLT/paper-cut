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
 * bindings page can be another, from a Controls form row. */
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

#ifdef __cplusplus
}
#endif

#endif
