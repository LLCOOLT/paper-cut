#ifndef PORT_UPDATE_CHECK_H
#define PORT_UPDATE_CHECK_H

/* The update check (port/settings/features/update_check.cpp): at start, if
 * the setting is on, the repository's latest release from GitHub, on a
 * thread of its own; any failure leaves no notice and says nothing. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "Paper Cut X is out" and the release's link, when a newer release was
 * found (Dear ImGui, inside the current window); nothing otherwise. */
void portUpdateDrawNotice(void);

/* The player asked (an online refusal: the friend's Paper Cut is newer): the
 * same check now, on a thread of its own, whatever the setting says (never
 * in test runs: $TTYD_FAST without $TTYD_UPDATE_API answers "none found").
 * Its state: 0 not asked, 1 asking, 2 none found, 3 found (its version in
 * version, "1.2.3"). Downloading and installing stay the General tab's New
 * version, as ever. */
void portUpdateCheckNow(void);
int portUpdateCheckState(char* version, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
