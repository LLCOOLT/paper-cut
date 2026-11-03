#ifndef PORT_VERSIONS_H
#define PORT_VERSIONS_H

/* The game's versions and which disc is which (port/src/common/discid_pc.c).
 * Each PC executable is built for one of them (PORT_GAME_ID); the launcher
 * names every disc it's shown and starts the executable of its version. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortVersionInfo {
    const char* version;   /* "G8ME01": TTYD_VERSION, configure.py's name */
    const char* name;      /* "the US release", for sentences */
    const char* label;     /* "US", for lists */
    const char* shortName; /* "us": the executable is ttyd-<shortName> */
    char id[7];            /* the ID on its disc */
    int e3, monthry;       /* its disc has e3.tpl / monthry.tpl */
    int ported;            /* the port builds it */
} PortVersionInfo;

/* This executable's version. */
const PortVersionInfo* portVersionSelf(void);

/* The version of the disc with ID id (6 bytes; NULL: not a GameCube disc),
 * whose files has() looks up by path in its file system; NULL if unknown. */
const PortVersionInfo* portVersionIdentify(const char* id, int (*has)(const char* path, void* ctx), void* ctx);

/* TRUE if that disc is this executable's version; else FALSE with the
 * reason in why. source names the disc in the message. */
int portCheckGameDisc(const char* id, int (*has)(const char* path, void* ctx), void* ctx, const char* source,
                      char* why, size_t size);

#ifdef __cplusplus
}
#endif

#endif
