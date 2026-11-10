/* Entry point for the headless backend. */
#include "port/file_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/rel_pc.h"
#include "port/platform.h"
#include "../../archipelago/ttyd_ap.h"
#include "port/luigi.h"
#include "port/story_test.h"

int ttyd_main(void); /* src/main.c, compiled with -Dmain=ttyd_main */
void portDvdSetRoot(const char* root);              /* sdk/dvd_pc.c */
BOOL portDvdOpenIso(const char* path, char id[6]); /* sdk/dvd_pc.c */
BOOL portDvdHasFile(const char* discPath);         /* sdk/dvd_pc.c */
void portHeadlessProfileStart(void);                /* platform/profile_pc.c: $TTYD_PROFILE */
void portHeadlessSettingsLoad(void);               /* platform/headless.c: settings.txt */

static BOOL exists(const char* dir, const char* name) {
    char path[1024];
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = portFopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    }
    return f != NULL;
}

/* a file of the disc (portCheckGameDisc's marker files) */
static int hasFile(const char* name, void* root) {
    (void)root;
    return portDvdHasFile(name);
}

/* The game's disc image (.iso) as the first argument, as on Aurora, or
 * $TTYD_ISO. */
static BOOL checkIso(const char* path) {
    char id[6], why[512];
    if (!portDvdOpenIso(path, id)) {
        fprintf(stderr, "%s: not a GameCube disc image\n", path);
        return FALSE;
    }
    if (!portCheckGameDisc(id, hasFile, NULL, path, why, sizeof(why))) {
        fprintf(stderr, "%s\n", why);
        return FALSE;
    }
    return TRUE;
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
        fprintf(stderr, "no game data in %s: give the game's .iso, or set TTYD_DATA_DIR to the extracted disc "
                        "(or its files/ directory)\n", root);
        return FALSE;
    }
    portDvdSetRoot(root);
    snprintf(path, sizeof(path), "%s/../sys/boot.bin", root);
    f = portFopen(path, "rb");
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

#if defined(TTYD_HEADLESS_FMA) && (defined(__x86_64__) || defined(_M_X64))
#include <cpuid.h>
/* FMA, and the OS keeping the AVX registers (the instruction's VEX encoding) */
static int cpuHasFma(void) {
    unsigned a, b, c, d, lo, hi;
    if (!__get_cpuid(1, &a, &b, &c, &d) || !(c & (1u << 12)) || !(c & (1u << 27)) || !(c & (1u << 28))) {
        return 0;
    }
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (lo & 6) == 6;
}
#endif

int main(int argc, char* argv[]) {
    const char* iso;
    portUtf8EnvInit(); /* (the path variables as UTF-8 on Windows too, as portFopen reads them: port/file_pc.h) */
    iso = argc > 1 && argv[1][0] != '-' ? argv[1] : getenv("TTYD_ISO");
    portCrashInit();
#if defined(TTYD_HEADLESS_FMA) && (defined(__x86_64__) || defined(_M_X64))
    if (!cpuHasFma()) {
        fprintf(stderr, "this headless build needs a CPU with FMA (configure with -DTTYD_HEADLESS_FMA=OFF)\n");
        return 1;
    }
#endif
    if (iso != NULL && iso[0] != '\0' ? !checkIso(iso) : !checkDataDir()) {
        return 1;
    }
    portHeadlessSettingsLoad();
    portHeadlessProfileStart(); /* $TTYD_PROFILE (platform/profile_pc.c) */
    portStoryTestInit(); /* tests: the story sweep's hooks, when their variables are set */
    portRelInit(argv[0]);
    if (getenv("TTYD_AP_VERIFY") != NULL) {
        portApVerifyTargets(); /* the randomizers' targets against the disc's RELs (as Aurora's portApInit) */
    }
    portLuigiModelInit(); /* the Play as models with our animations, as on Aurora (luigi_model_pc.c) */
    return ttyd_main();
}
