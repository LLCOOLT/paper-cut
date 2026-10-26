/* An example code mod (docs/MODDING.md, "Code mods"): it uses each kind of
 * service once. Built by CMakeLists.txt (ttyd_add_mod) into
 * <build>/mods/hello-native.<dll|so|dylib>; a mod ships it as
 * lib/<platform>/ in its folder or zip. */
#include "ttyd_mod/ttyd_mod.h"
#include <stdio.h>

static const TtydModApi* api;
static int frames;
static int shops;

static void onFrame(void* user) {
    (void)user;
    frames++;
}

/* Over the picture, in the game's 640x480. */
static void onDraw(void* user) {
    char text[64];
    PouchData* pouch;
    (void)user;
    if (!api->optionInt("counter", 1)) {
        return;
    }
    pouch = pouchGetPtr(); /* NULL until the game has set itself up */
    if (pouch == NULL) {
        return;
    }
    snprintf(text, sizeof(text), "Coins %d   frame %d", pouch->coins, frames);
    api->drawRect(440.0f, 440.0f, 190.0f, 30.0f, 0x00000099u);
    api->drawText(450.0f, 445.0f, 18.0f, 0xFFE070FFu, text);
}

/* After the game works out a hit's damage: Mario's side hits harder. */
static void damagePost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                       BattleWeapon* weapon, u32* flags, u32 unused) {
    (void)target, (void)part, (void)weapon, (void)flags, (void)unused;
    if (attacker != NULL && attacker->mAlliance == 0) { /* Mario and his partner (battle.c: entryunit_mario) */
        const s32 before = *result;
        *result *= api->optionInt("damage", 2);
        api->log("hit: %d damage, %d with the mod", before, *result);
    }
}

static void onShop(void* user, const char* map, ShopItem* items, int count) {
    int i;
    (void)user;
    if (api->optionInt("shops", 1) == 1) {
        for (i = 0; i < count; i++) {
            items[i].price = (items[i].price + 1) / 2;
        }
    }
    shops++;
    api->log("shop on %s: %d goods, the first %d coins", map, count, items[0].price);
}

static void onArea(void* user, const char* area, int entered) {
    (void)user;
    api->log("%s area %s", entered ? "entered" : "left", area);
}

TTYD_MOD_ENTRY(a) {
    api = a;
    api->log("hello from %s (API %d)", api->gameId, api->version);
    api->onFrame(onFrame, NULL);
    api->onDraw(onDraw, NULL);
    api->onShop(onShop, NULL);
    api->onArea(onArea, NULL);
    api->hookPost(TTYD_HOOK_BattleCalculateDamage, (void*)damagePost);
    api->setMessage("in_kinoko", "Mushroom (set by code)");
    return 0;
}
