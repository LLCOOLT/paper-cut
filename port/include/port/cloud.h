#ifndef PORT_CLOUD_H
#define PORT_CLOUD_H

/* Save sync through iCloud Drive (port/apple/cloud_apple.m; Apple builds,
 * PORT_CLOUD_SYNC): the memory card's .gci files kept the same as a folder in
 * iCloud Drive, each the way of its newer copy. Plain C. */
#ifdef __cplusplus
extern "C" {
#endif

/* There is a cloud folder (the Mac: iCloud Drive is on; iOS: one was picked). */
int portCloudAvailable(void);
/* Its name, for the settings page. */
const char* portCloudFolderName(void);
/* Syncs <userPath>/<region>/Card A: pull (from the cloud, before the card is
 * mounted) or push (to it). Returns the number of files copied. */
int portCloudSync(const char* userPath, const char* region, int pull);
/* Waits for the queued pushes (before the app is suspended or quits). */
void portCloudFlush(void);
/* iOS: shows the folder picker (returns 0 if it can't); forgets the folder. */
int portCloudPickFolder(void);
void portCloudForget(void);
/* The last thing done, for the settings page. */
const char* portCloudStatus(void);

#ifdef __cplusplus
}
#endif

#endif
