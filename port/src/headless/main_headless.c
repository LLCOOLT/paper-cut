/* Entry point for the headless backend. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/rel_pc.h"
#include "port/platform.h"

int ttyd_main(void); /* src/main.c, compiled with -Dmain=ttyd_main */
void portDvdSetRoot(const char* root); /* sdk/dvd_pc.c */

static BOOL exists(const char* dir, const char* name) {
    char path[1024];
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    }
    return f != NULL;
}

/* a file of the extracted disc (portCheckGameDisc's marker files) */
static int hasFile(const char* name, void* root) {
    return exists((const char*)root, name);
}

/* $TTYD_DATA_DIR: the extracted disc's files/ directory, or the directory
 * above it (with sys/). The disc's sys/boot.bin starts with the game ID. */
static BOOL checkDataDir(void) {
    const char* env = getenv("TTYD_DATA_DIR");
    char root[1024], path[1024];
    char id[6], why[512];
    BOOL ok;
    FILE* f;
    snprintf(root, sizeof(root), "%s", env != NULL && env[0] != '\0' ? env : "data");
    if (!exists(root, "mariost.tpl") && exists(root, "files/mariost.tpl")) {
        snprintf(root + strlen(root), sizeof(root) - strlen(root), "/files");
    }
    if (!exists(root, "mariost.tpl")) {
        fprintf(stderr, "no game data in %s: set TTYD_DATA_DIR to the extracted disc "
                        "(or its files/ directory)\n", root);
        return FALSE;
    }
    portDvdSetRoot(root);
    snprintf(path, sizeof(path), "%s/../sys/boot.bin", root);
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "note: %s not found; disc version not checked\n", path);
        return TRUE;
    }
    ok = fread(id, 1, sizeof(id), f) == sizeof(id);
    fclose(f);
    if (!portCheckGameDisc(ok ? id : NULL, hasFile, root, path, why, sizeof(why))) {
        fprintf(stderr, "%s\n", why);
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char* argv[]) {
    portCrashInit();
    if (!checkDataDir()) {
        return 1;
    }
    portRelInit(argv[0]);
    return ttyd_main();
}
