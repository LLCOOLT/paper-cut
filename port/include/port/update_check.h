#ifndef PORT_UPDATE_CHECK_H
#define PORT_UPDATE_CHECK_H

/* The update check (port/settings/features/update_check.cpp): at start, if
 * the setting is on, the repository's latest release from GitHub, on a
 * thread of its own; any failure leaves no notice and says nothing. */
#ifdef __cplusplus
extern "C" {
#endif

/* "Paper Cut X is out" and the release's link, when a newer release was
 * found (Dear ImGui, inside the current window); nothing otherwise. */
void portUpdateDrawNotice(void);

#ifdef __cplusplus
}
#endif

#endif
