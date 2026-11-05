#ifndef PORT_BUILD_ID_H
#define PORT_BUILD_ID_H

/* Which build this is, for what has to be played by the same game code:
 * replays and online co-op compare it. A release is its version ("0.4.0");
 * any other build (TTYD_PORT_VERSION "dev") is the version and its commit
 * ("dev+b9424b37a", cmake/build_id.cmake), since two of those can play
 * differently while both saying "dev" (a replay made on one went out of step
 * on the other with no word why). Without git the commit is unknown and the
 * version is as it is. */

#ifdef __cplusplus
extern "C" {
#endif

/* this build's (port/settings/build_id.cpp; the settings' side, Aurora builds) */
const char* portBuildVersion(void);

#ifdef __cplusplus
}
#endif

#endif
