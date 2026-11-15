/* The features' Start menu forms (port/include/port/pause_forms.h), as they
 * register at start. Kept here, not in pause_settings_aurora.c: that file's
 * statics are the Start menu's own block (port/companion_section.h), which
 * the second screen's copy of the menu swaps for its own, whose bss starts
 * empty: the list there was empty, and its Settings tab had no form rows. */
#include "port/pause_forms.h"

const PortPauseForm* gPortPauseForms[PORT_PAUSE_FORMS_MAX];
int gPortPauseFormCount;

void portPauseAddForm(const PortPauseForm* form) {
    if (gPortPauseFormCount < PORT_PAUSE_FORMS_MAX) {
        gPortPauseForms[gPortPauseFormCount++] = form;
    }
}
