/* Partner skins' files (port/include/port/costume.h, docs/COSTUMES.md): at
 * startup, before the mods apply, each folder in the user folder's skins/
 * that holds a character (a pack made by tools/port/character_tool.py pack:
 * overlay/a/<model> and overlay/a/<texture>-; or an export folder: build/a/
 * or disc/) is made into a/pc_skin_<n> and a/pc_skin_<n>-, its model's own
 * names changed to those, as disc files the game loads like any other
 * (portModsSetHostFile). Local files only: nothing is fetched. The game
 * checks a model file as it loads it (port/src/common/anim_pc.c): one that
 * doesn't make sense is refused, with its name in the log. */
#include "port/costume.h"
#include "port/mods.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_FIELD 64     /* the model's own name (0x4) and its textures' (0x44) */
#define MODEL_MIN 0x1B0   /* the model file's header */
#define SKINS_FOUND 32

typedef struct Found {
    char dir[512];
    char folder[128];
} Found;

typedef struct List {
    Found* items;
    int count;
    const char* base; /* the folder listed, ending in '/' */
} List;

static SDL_EnumerationResult SDLCALL listFolder(void* user, const char* dir, const char* name) {
    List* l = (List*)user;
    char path[512];
    SDL_PathInfo info;
    if (name[0] == '.' || l->count >= SKINS_FOUND) {
        return SDL_ENUM_CONTINUE;
    }
    (void)dir;
    snprintf(path, sizeof(path), "%s%s", l->base, name);
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        snprintf(l->items[l->count].dir, sizeof(l->items[0].dir), "%s", path);
        snprintf(l->items[l->count].folder, sizeof(l->items[0].folder), "%s", name);
        l->count++;
    }
    return SDL_ENUM_CONTINUE;
}

static int byFolder(const void* a, const void* b) {
    return SDL_strcasecmp(((const Found*)a)->folder, ((const Found*)b)->folder);
}

/* the model in dir: a file named as no texture file is (no '-' at its end,
 * no extension), long enough for the header, whose name field is a name */
typedef struct Pick {
    char path[640];
    int found;
    const char* base; /* the folder listed, ending in '/' */
} Pick;

static SDL_EnumerationResult SDLCALL pickModel(void* user, const char* dir, const char* name) {
    Pick* p = (Pick*)user;
    const size_t len = strlen(name);
    if (p->found || len == 0 || name[len - 1] == '-' || strchr(name, '.') != NULL) {
        return SDL_ENUM_CONTINUE;
    }
    (void)dir;
    snprintf(p->path, sizeof(p->path), "%s%s", p->base, name);
    p->found = 1;
    return SDL_ENUM_SUCCESS;
}

/* "name" in mod.json or manifest.json's "model": the skin's name, in plain
 * ASCII (the game's font shows Latin-1; anything else becomes '?') */
static void jsonName(const char* dir, const char* file, const char* key, char* out, size_t size) {
    char path[600];
    char pattern[32];
    size_t n = 0;
    char* text;
    const char* p;
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    text = (char*)SDL_LoadFile(path, NULL);
    if (text == NULL) {
        return;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(text, pattern);
    if (p != NULL && (p = strchr(p + strlen(pattern), '"')) != NULL) {
        for (p++; *p != '\0' && *p != '"' && n + 1 < size; p++) {
            out[n++] = (*p >= 0x20 && *p < 0x7F) ? *p : '?';
        }
        out[n] = '\0';
    }
    SDL_free(text);
}

static int addSkin(const Found* f, int n) {
    static const char* const kDirs[] = {"overlay/a", "build/a", "disc"};
    char dir[600], texPath[700], model[24], name[48];
    unsigned char* data = NULL;
    unsigned char* tex = NULL;
    size_t size = 0, texSize = 0;
    size_t i;
    char texName[NAME_FIELD + 1];

    for (i = 0; i < sizeof(kDirs) / sizeof(kDirs[0]) && data == NULL; i++) {
        Pick pick = {{0}, 0, dir};
        snprintf(dir, sizeof(dir), "%s/%s/", f->dir, kDirs[i]);
        SDL_EnumerateDirectory(dir, pickModel, &pick);
        if (pick.found) {
            data = (unsigned char*)SDL_LoadFile(pick.path, &size);
            if (data != NULL && (size < MODEL_MIN || data[4 + NAME_FIELD - 1] != '\0' ||
                                 data[0x44 + NAME_FIELD - 1] != '\0' || data[0x44] == '\0')) {
                SDL_free(data);
                data = NULL;
            }
        }
    }
    if (data == NULL) {
        SDL_Log("skins: %s has no character in overlay/a, build/a or disc", f->folder);
        return 0;
    }
    memcpy(texName, data + 0x44, NAME_FIELD);
    texName[NAME_FIELD] = '\0';
    snprintf(texPath, sizeof(texPath), "%s%s-", dir, texName);
    tex = (unsigned char*)SDL_LoadFile(texPath, &texSize);
    if (tex == NULL) {
        SDL_Log("skins: %s: its textures %s- are missing", f->folder, texName);
        SDL_free(data);
        return 0;
    }
    /* the model's names: ours, so its textures are ours too */
    snprintf(model, sizeof(model), "pc_skin_%d", n);
    memset(data + 4, 0, NAME_FIELD);
    memset(data + 0x44, 0, NAME_FIELD);
    memcpy(data + 4, model, strlen(model));
    memcpy(data + 0x44, model, strlen(model));
    snprintf(dir, sizeof(dir), "/a/%s", model);
    portModsSetHostFile(dir, data, size);
    snprintf(dir, sizeof(dir), "/a/%s-", model);
    portModsSetHostFile(dir, tex, texSize);
    snprintf(name, sizeof(name), "%s", f->folder);
    jsonName(f->dir, "mod.json", "name", name, sizeof(name));
    portSkinAdd(name, model);
    SDL_Log("skins: %s as %s", name, model);
    SDL_free(data);
    SDL_free(tex);
    return 1;
}

void portSkinsInit(const char* userPath) {
    char dir[512];
    Found found[SKINS_FOUND];
    List l = {found, 0, dir};
    int i, n = 0;
    if (userPath == NULL || SDL_getenv("TTYD_NO_SKINS") != NULL) {
        return;
    }
    snprintf(dir, sizeof(dir), "%s%sskins/", userPath,
             userPath[0] != '\0' && userPath[strlen(userPath) - 1] != '/' && userPath[strlen(userPath) - 1] != '\\'
                 ? "/"
                 : "");
    SDL_CreateDirectory(dir); /* (there to be found) */
    SDL_EnumerateDirectory(dir, listFolder, &l);
    qsort(found, (size_t)l.count, sizeof(Found), byFolder);
    for (i = 0; i < l.count; i++) {
        n += addSkin(&found[i], n);
    }
}
