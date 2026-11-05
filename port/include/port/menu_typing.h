#ifndef PORT_MENU_TYPING_H
#define PORT_MENU_TYPING_H

/* Typing on the keyboard into the Start menu's on-screen keyboards
 * (port/settings/features/menu_typing.cpp), desktop only. */
#ifdef __cplusplus
extern "C" {
#endif

/* Each frame an on-screen keyboard is open: the keyboard types into it (its
 * keys are not the game's buttons meanwhile); a page that stops asking lets go. */
void portMenuTypingFrame(void);
/* The keyboard types into a page now. */
int portMenuTyping(void);
/* What was typed since the last call, into out (NUL-ended): letters, and '\b'
 * for Backspace, '\n' for Enter, 0x1B for Esc. Its length. */
int portMenuTyped(char* out, int size);
/* Each SDL event, first: nonzero when typing took it. */
int portMenuTypingEvent(const void* sdlEvent);

#ifdef __cplusplus
}
#endif

#endif
