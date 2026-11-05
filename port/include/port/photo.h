#ifndef PORT_PHOTO_H
#define PORT_PHOTO_H

/* The player's pictures (photo mode's and the screenshots; port/src/aurora/shot_aurora.c) and the Journal's
 * Photos, where they are looked through (port/src/aurora/photo_gallery_aurora.c). Aurora builds only (the game's
 * code calls these under TTYD_BACKEND_AURORA).
 *
 * The Journal's Photos: an entry after the game's own (not in an online game or a replay: the entries are the
 * same on both sides), its button painted as theirs. A book of the pictures, newest first, eight a spread: the
 * stick picks one, L and R turn the page, A shows it large (left and right, L and R: the one before or after),
 * B goes back. The pictures are read on a thread of their own, small ones kept while the game runs. */

#ifdef __cplusplus
extern "C" {
#endif

/* where new pictures are saved: the Pictures folder's Paper Cut (on phones, or with $TTYD_USER_DIR set, the user
 * folder's Photos); and the earlier releases' Screenshots folder, also shown */
const char* portPhotoFolder(void);
const char* portPhotoOldFolder(void);
/* a picture was saved: the Photos read the folders again when next opened */
void portPhotoGalleryChanged(void);

struct WindowWork;
/* win_log.c: the entry is there (not online, not in a replay) */
int portPhotoJournalEntry(void);
/* its button at (x, y), as the Journal draws its own (the cursor's lifted over its shadow, the others grey) */
void portPhotoJournalButton(struct WindowWork* wp, float x, float y, int selected);
/* the entry opened; once a frame while it is (0 stay, -1 back to the entries, -2 the menu closed by Start);
 * its pages drawn at the Journal's (x, y); closed */
void portPhotoJournalOpen(struct WindowWork* wp);
int portPhotoJournalMain(struct WindowWork* wp);
void portPhotoJournalDisp(struct WindowWork* wp, float x, float y);
void portPhotoJournalClose(void);
/* msgSearch: the entry's help ("pc_menu_photo..."), or NULL */
const char* portPhotoJournalMsg(const char* tag);

#ifdef __cplusplus
}
#endif

#endif
