/* Each PC executable is built for one game version (TTYD_VERSION): the disc it
 * runs from must be that version's, since the versions' data and structs
 * differ. The launcher also names every disc it's shown and starts the
 * executable of its version. The disc ID doesn't tell all seven versions
 * apart: the June 2004 US demo's is G8MJ01 as the Japanese release's and store
 * demo's, and the September demos' are their releases'. Two files do: the
 * June US demo has e3.tpl (its attract screen), the Japanese store demo and
 * the September demos monthry.tpl. The port builds the three releases. */
#include <stdio.h>
#include <string.h>
#include "port/platform.h"

static const PortVersionInfo kVersions[] = {
    {"G8MJ01", "the Japanese release", "Japan", "jp", "G8MJ01", FALSE, FALSE, TRUE},
    {"G8ME01", "the US release", "US", "us", "G8ME01", FALSE, FALSE, TRUE},
    {"G8MP01", "the European release", "Europe", "eu", "G8MP01", FALSE, FALSE, TRUE},
    {"G8ME01_DEMO", "the June 2004 US demo", "US demo (June 2004)", "usdemo", "G8MJ01", TRUE, FALSE, FALSE},
    {"G8MJ01_DEMO", "the Japanese store demo", "Japanese store demo", "jpdemo", "G8MJ01", FALSE, TRUE, FALSE},
    {"G8ME01_DEMO2", "the September 2004 US demo", "US demo (Sept 2004)", "usdemo2", "G8ME01", FALSE, TRUE, FALSE},
    {"G8MP01_DEMO", "the September 2004 European demo", "European demo (Sept 2004)", "eudemo", "G8MP01", FALSE, TRUE,
     FALSE},
};
#define VERSION_COUNT (sizeof(kVersions) / sizeof(kVersions[0]))

const PortVersionInfo* portVersionSelf(void) {
    size_t i;
    for (i = 0; i < VERSION_COUNT; i++) {
        if (strcmp(kVersions[i].version, PORT_GAME_ID) == 0) {
            return &kVersions[i];
        }
    }
    return &kVersions[0];
}

const PortVersionInfo* portVersionIdentify(const char* id, int (*has)(const char* path, void* ctx), void* ctx) {
    BOOL e3, monthry;
    size_t i;
    if (id == NULL) {
        return NULL;
    }
    e3 = has != NULL && has("e3.tpl", ctx);
    monthry = has != NULL && has("monthry.tpl", ctx);
    for (i = 0; i < VERSION_COUNT; i++) {
        if (memcmp(kVersions[i].id, id, 6) == 0 && kVersions[i].e3 == e3 && kVersions[i].monthry == monthry) {
            return &kVersions[i];
        }
    }
    return NULL;
}

int portCheckGameDisc(const char* id, int (*has)(const char* path, void* ctx), void* ctx, const char* source,
                       char* why, size_t size) {
    const PortVersionInfo* self = portVersionSelf();
    const PortVersionInfo* found = portVersionIdentify(id, has, ctx);
    if (found == self) {
        return TRUE;
    }
    if (found != NULL) {
        snprintf(why, size, "%s is %s (%s), but this build is for %s (Paper Mario: The Thousand-Year Door, %s).",
                 source, found->name, found->version, self->name, self->version);
    } else if (id != NULL) {
        snprintf(why, size, "%s is %.6s, but this build is for %s (Paper Mario: The Thousand-Year Door, %s).", source,
                 id, self->name, self->version);
    } else {
        snprintf(why, size, "%s is not a GameCube disc, but this build is for %s (Paper Mario: The Thousand-Year "
                 "Door, %s).", source, self->name, self->version);
    }
    return FALSE;
}
