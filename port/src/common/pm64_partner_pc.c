/* The eight Paper Mario 64 partners (port/include/port/pm64.h, docs/PM64_LINK.md):
 * Goombario, Kooper, Bombette, Parakarry, Watt, Sushie, Lakilester and Bow, from
 * the player's own linked ROM. Each takes a TTYD partner's place, its host:
 * the host's party slot, field ability and battle unit run as ever (Tattle,
 * the shell, the blast, the flutter over gaps, the hidden things found, the
 * dive into the shadows, the ride), and the host's model is drawn as the PM64
 * partner's paper sprite instead, with the map's lights on it (the game's
 * shadow stays the host's). In battle the host's moves that match a PM64 move
 * take its name, description and FP cost from the ROM's move table.
 *
 * Which is out is game state, so a game played together or replayed plays the
 * same: GSW 2011 and 2019 (each one's joining told) and 2012..2018 (per host
 * 1..7, the PM64 partner wearing it, 0 none), changed only through the partner
 * wheel's input tap (portPm64WheelChoice). */
#include "port/file_pc.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include "pmario_sound.h"
#include "system.h"
#include "port/achievements.h"
#include "port/anim_pc.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#ifdef PORT_MODS
#include "port/mod_host.h"
#include "port/postgame_extra.h"
#endif
#include "port/pm64_sfx.h"
#include "port/statesync.h"
#include "port/paper.h"
#include "port/players.h"
#include "port/settings.h"
#include "port/toggles.h"
#include "port/versions.h"
#include "battle/battle.h"
#include "battle/battle_camera.h"
#include "battle/battle_damage.h"
#include "battle/battle_database_common.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "evt/evt_audience.h"
#include "evt/evt_cmd.h"
#include "evt/evt_snd.h"
#include "manager/evtmgr_cmd.h"
#include "battle/battle_event_default.h"
#include "driver/icondrv.h"
#include "driver/effdrv.h"
#include "eff/eff_charge.h"
#include "eff/eff_kemu_test.h"
#include "eff/eff_spark.h"
#include "eff/eff_splash.h"
#include "eff/n64/eff_akari_charge_n64.h"
#include "eff/n64/eff_bomb_n64.h"
#include "eff/n64/eff_akari_sflush_n64.h"
#include "eff/n64/eff_confetti_n64.h"
#include "eff/n64/eff_flame_n64.h"
#include "eff/n64/eff_guruguru_n64.h"
#include "eff/n64/eff_lensflare_n64.h"
#include "eff/n64/eff_opuku_wcurtain_n64.h"
#include "eff/n64/eff_opuku_wgun_n64.h"
#include "eff/n64/eff_opuku_wprotect_n64.h"
#include "eff/n64/eff_pokopi_thunder_n64.h"
#include "eff/n64/eff_shock_n64.h"
#include "eff/n64/eff_thunder_n64.h"
#include "eff/n64/eff_water_damage_n64.h"
#include "eff/n64/eff_whirlwind_n64.h"
#include "mario/mario_party.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/mapdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "win/win_main.h"
#include "mario/mario.h"
#include "party.h"
#include "icon_ids.h"
#include "manager/evtmgr.h"
#include "port/battlex.h"
#include "port/save_words.h"
#include "unit/party/unit_party_vivian.h"

s32 yoshiGetStatus(void);

/* GSW 2011..2019 (port/include/port/save_words.h: until 0.5.0's first builds 2040..2047, on the hardcore mark) */
#define PM64_GSW PORT_GSW_PM64_FIRST
#define PM64_GSW_MET PM64_GSW            /* bit p: partner p's joining told (bit 0: all) */
#define PM64_GSW_WORN (PM64_GSW + 1)     /* + host - 1: the PM64 partner on host */
#define PM64_GSW_MET2 (PM64_GSW + 8)     /* bit p - 8: partner p's (Bow's) joining told */
_Static_assert(PM64_GSW_MET2 <= PORT_GSW_PM64_LAST && PM64_GSW_WORN + 6 < PM64_GSW_MET2,
               "PM64's words: port/include/port/save_words.h");
/* GSW 2024..2025: bit f of the pair (2024's 0..7, 2025's 0..1): PM64 boss fight f beaten on this file */
#define PM64_GSW_BOSSES PORT_GSW_PM64_BOSSES_FIRST
_Static_assert(PORT_GSW_PM64_BOSSES_LAST - PORT_GSW_PM64_BOSSES_FIRST + 1 >= (PM64_BOSS_FIGHTS + 7) / 8,
               "PM64 bosses' words: port/include/port/save_words.h");

/* PM64's battles run at 30 frames a second, TTYD's at 60: a PM64 script's numbers, read off the decomp, are made
 * TTYD's with these, so the moves play at PM64's real speed (and move every TTYD frame, smoothly). Frames (waits,
 * move / jump / effect times, loop counts of a per-frame loop) twice; a distance or angle per frame half; an
 * acceleration (SetFallAccel: per frame per frame) a quarter. The shared building blocks (PM64_HOME, PM64_BOUNCE,
 * sHitAllEvt's LW(11), pm64Zoom, pm64Throw, pm64Beam, pm64Fx's lifetimes) take PM64's numbers and do it themselves.
 * An effect's duration from a PM64 script is a PM64 frame count too (PM64_FRAMES); only TTYD's own effects' and
 * commands' numbers, already TTYD frames, stay as they are. */
#define PM64_FRAMES(n) ((n) * 2)
#define PM64_SPEED(v) FLOAT((v) * 0.5f)
#define PM64_ACCEL(a) FLOAT((a) * 0.25f)
#define PM64_PER_FRAME(v) ((v) * 0.5f) /* (in C: a per-frame step) */

/* --- who's who */

/* their host (TTYD PartyId), the NPC sprite archive's field sprite (the battle
 * one is 8 on), their move set's place in the message and move tables (Bow
 * is 4 there), and the ROM's name of them */
static const struct {
    int host;
    int sprite;
    int order;
    const char* english;
} kWho[PM64_PARTNER_COUNT] = {
    {0, -1, -1, ""},
    {1, 0, 0, "Goombario"},  /* Goombella: Tattle */
    {2, 1, 1, "Kooper"},     /* Koops: the shell */
    {3, 2, 2, "Bombette"},   /* Bobbery: the blast */
    {4, 3, 3, "Parakarry"},  /* Yoshi: over gaps */
    {7, 5, 5, "Watt"},       /* Ms. Mowz: hidden things */
    {6, 6, 6, "Sushie"},     /* Vivian: under the surface */
    {5, 7, 7, "Lakilester"}, /* Flurrie: Gale Force (his ride over spikes and lava: pm64_field_pc.c) */
    {6, 4, 4, "Bow"},        /* Vivian: Mario hidden (Outta Sight for her Veil) */
};
/* the ROM's names: Menus_Party_<name>, read off the ROM's message table (section 0x1D: Goombario 0x07 to Lakilester
 * 0x0E, with Goompa at 0x0B and Bow after them at 0x0F) */
static const unsigned short kNameMsg[PM64_PARTNER_COUNT] = {0, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F};

int portPm64PartnerHost(int p) {
    return p > 0 && p < PM64_PARTNER_COUNT ? kWho[p].host : 0;
}

/* the Japanese release's names, Paper Cut's own text (a JP or iQue ROM's own
 * text isn't decoded: pm64_pc.c) */
static const char* const kJapanese[PM64_PARTNER_COUNT] = {
    "", "クリオ", "カメキ", "ピンキー", "パレッタ",
    "アカリン", "プクプク", "パタクモ", "レディバウ",
};

static int japaneseGame(void) {
    const PortVersionInfo* v = portVersionSelf();
    return v != NULL && v->id != NULL && v->id[3] == 'J';
}

int portPm64GameLanguage(void) {
    /* the European release's text language (gp->language, as msgdrv.c's language_dir: 0 Japanese, 1 English,
       2 German, 3 French, 4 Spanish, 5 Italian; the US release's is 1) as a PAL ROM's (0 English, 1 German,
       2 French, 3 Spanish; Italian: English). (It was read as 0-based: a PAL ROM's German on the US release
       and in the European one's English.) */
    if (gp != NULL && gp->language >= 2 && gp->language <= 4) {
        return gp->language - 1;
    }
    return 0;
}

/* tests: TTYD_PM64_NAME_DUMP=1 logs the ROM's section 0x1D from 0x00 to 0x20 in each of its languages, once linked */
static void nameDump(void) {
    static int sDone;
    char buf[64];
    int lang, i;
    if (sDone || getenv("TTYD_PM64_NAME_DUMP") == NULL || portPm64Linked() == PM64_NONE) {
        return;
    }
    sDone = 1;
    for (lang = 0; lang < 4; lang++) {
        for (i = 0; i <= 0x20; i++) {
            if (portPm64MessageText(PM64_MSG(0x1D, i), lang, buf, sizeof(buf)) > 0) {
                printf("[pm64] msg 1D%04X lang %d: %s\n", i, lang, buf);
            }
        }
    }
    fflush(stdout);
}

/* Where the names begin in section 0x1D of language lang's messages (the US ROM's: 0x07). Not taken on trust: a
 * ROM whose section has more or fewer messages before them would name each partner after another (Bow as
 * Goompa, four before her). Found by the two names every language has as they are, Peach then Mario, the last of
 * the run (Goombario .. Bow, Goombaria, Twink, Peach, Mario: Mario 12 after Goombario); 0x07 if they aren't
 * there. */
static int nameBase(int lang) {
    static unsigned sGen[4];
    static int sBase[4];
    char a[32], b[32];
    const unsigned gen = portPm64Generation() + 1;
    int i;
    if (lang < 0 || lang > 3) {
        return 0x07;
    }
    if (sGen[lang] == gen) {
        return sBase[lang];
    }
    sGen[lang] = gen;
    sBase[lang] = 0x07;
    for (i = 12; i < 0x60; i++) {
        if (portPm64MessageText(PM64_MSG(0x1D, i), lang, a, sizeof(a)) > 0 && strcmp(a, "Mario") == 0 &&
            portPm64MessageText(PM64_MSG(0x1D, i - 1), lang, b, sizeof(b)) > 0 && strcmp(b, "Peach") == 0) {
            sBase[lang] = i - 12;
            break;
        }
    }
    if (sBase[lang] != 0x07 || i == 0x60) {
        printf("[pm64] partner names: section 1D from %02X in language %d%s\n", sBase[lang], lang,
               i == 0x60 ? " (Peach and Mario not found: as the US ROM)" : "");
        fflush(stdout);
    }
    return sBase[lang];
}

const char* portPm64PartnerName(int p) {
    static char sName[PM64_PARTNER_COUNT][32];
    static unsigned sGen[PM64_PARTNER_COUNT];
    unsigned gen;
    nameDump();
    if (p <= 0 || p >= PM64_PARTNER_COUNT) {
        return "";
    }
    if (japaneseGame()) {
        return kJapanese[p];
    }
    gen = (portPm64Generation() + 1) * 8u + (unsigned)portPm64GameLanguage();
    if (sGen[p] != gen) {
        const int at = nameBase(portPm64GameLanguage()) + kNameMsg[p] - 0x07;
        sGen[p] = gen;
        if (portPm64MessageText(PM64_MSG(0x1D, at), portPm64GameLanguage(), sName[p], sizeof(sName[p])) <= 0) {
            snprintf(sName[p], sizeof(sName[p]), "%s", kWho[p].english);
        }
    }
    return sName[p];
}

/* --- the state */

static int setting(const char* key) {
    int* v;
    static int sDeclared;
    if (!sDeclared) {
        /* (declared here too, as the Settings page does (pm64_link.cpp): headless has no page, and its tests set
           them) */
        sDeclared = 1;
        portSettingsDeclare("pm64_partners", 0, 0, 1);
        portSettingsDeclare("pm64_met_all", 0, 0, 1);
        portSettingsDeclare("pm64_join", 1, 0, 1);
    }
    v = portSettingsFind(key);
    return v != NULL ? *v : 0;
}

static int wanted(void) {
    return setting("pm64_partners") && portPm64Linked() != PM64_NONE;
}

/* Online (port/include/port/statesync.h), the hello's mods list says whether
 * this game has the partners (portPm64OnlineToken): every player needs the
 * same, or the session is refused. What it said holds while connected (a
 * change of the setting or the link waits for the next session), so every
 * game of a session agrees on the partners, their abilities and their moves. */
static int sOnlineSaid = -1;

const char* portPm64OnlineToken(void) {
    if (sOnlineSaid < 0 || !portStateSyncLinked()) {
        sOnlineSaid = wanted();
    }
    return sOnlineSaid ? "pm64" : "";
}

int portPm64PartnersActive(void) {
    if (gp == NULL) {
        return 0;
    }
    if (portStateSyncLinked() && sOnlineSaid >= 0) {
        return sOnlineSaid && portPm64Linked() != PM64_NONE;
    }
    return wanted();
}

int portPm64PartnerOn(int party) {
    int p;
    if (party < 1 || party > 7 || !portPm64PartnersActive()) {
        return 0;
    }
    if (!portStateSyncCoopHostWorn(party, &p)) {
        p = gp->gsw[PM64_GSW_WORN + party - 1]; /* (a co-op guest draws the host's party: the host's, above) */
    }
    return p > 0 && p < PM64_PARTNER_COUNT && kWho[p].host == party ? p : 0;
}

/* --- one character's own look (a co-op player's pick: second_player_pc.c) ---
 * The look above is per host: every copy of that partner out at once. A party slot (src/party.c's partyPtrTbl)
 * may have its own instead, so one copy out draws as Goombario while another stays Goombella: a PM64 partner
 * (on its host's model only), -1 the TTYD partner's own look, 0 the party's (the host's above). The field's
 * party entries only: a battle's units, the Party tab and the HUD keep the party's (its moves are the party's). */
static signed char sSlotLook[PORT_PARTY_SLOTS];

void portPm64SlotLook(int slot, int p) {
    if (slot >= 0 && slot < PORT_PARTY_SLOTS) {
        sSlotLook[slot] = (signed char)(p < 0 ? -1 : p < PM64_PARTNER_COUNT ? p : 0);
    }
}

/* pose poseId's slot look (that of the party entry drawn with it), host its model's partner; 0 none */
static int slotLook(int poseId, int host) {
    int slot;
    for (slot = 0; slot < PORT_PARTY_SLOTS; slot++) {
        const PartyEntry* party;
        const int look = sSlotLook[slot];
        if (look == 0 || (party = partyGetPtr(slot)) == NULL || party->paperAnimGroup != poseId) {
            continue;
        }
        if (look < 0) {
            return -1;
        }
        return kWho[look].host == host && portPm64PartnersActive() ? look : 0;
    }
    return 0;
}

/* pose poseId is the partner's (the partner slot's, Mario's own: its field abilities' rides and Bow's veil) */
static int partnerPose(int poseId) {
    const PartyEntry* party = partyGetPtr(marioGetPartyId());
    return party != NULL && party->paperAnimGroup == poseId;
}

/* the Japanese release's names in its font's Shift-JIS (Paper Cut's own text) */
static const unsigned char kSjis[PM64_PARTNER_COUNT][12] = {
    {0},
    {0x83, 0x4E, 0x83, 0x8A, 0x83, 0x49},
    {0x83, 0x4A, 0x83, 0x81, 0x83, 0x4C},
    {0x83, 0x73, 0x83, 0x93, 0x83, 0x4C, 0x81, 0x5B},
    {0x83, 0x70, 0x83, 0x8C, 0x83, 0x62, 0x83, 0x5E},
    {0x83, 0x41, 0x83, 0x4A, 0x83, 0x8A, 0x83, 0x93},
    {0x83, 0x76, 0x83, 0x4E, 0x83, 0x76, 0x83, 0x4E},
    {0x83, 0x70, 0x83, 0x5E, 0x83, 0x4E, 0x83, 0x82},
    {0x83, 0x8C, 0x83, 0x66, 0x83, 0x42, 0x83, 0x6F, 0x83, 0x45},
};

/* partner p's name in the game's encoding (Latin-1, or the JP release's Shift-JIS) */
const char* portPm64PartnerGameName(int p) {
    static char sOut[PM64_PARTNER_COUNT][48];
    char* out;
    const size_t size = sizeof(sOut[0]);
    const unsigned char* u;
    size_t n = 0;
    if (p <= 0 || p >= PM64_PARTNER_COUNT) {
        return "";
    }
    if (japaneseGame()) {
        return (const char*)kSjis[p];
    }
    out = sOut[p];
    for (u = (const unsigned char*)portPm64PartnerName(p); *u && n + 1 < size; u++) {
        if (*u < 0x80) {
            out[n++] = (char)*u;
        } else if ((*u == 0xC2 || *u == 0xC3) && (u[1] & 0xC0) == 0x80) {
            out[n++] = (char)(((*u & 0x03) << 6) | (u[1] & 0x3F));
            u++;
        } else {
            while ((u[1] & 0xC0) == 0x80) {
                u++; /* (no Latin-1 for it: left out) */
            }
        }
    }
    out[n] = 0;
    return out;
}

/* --- the HUD's face (statuswindow.c's party_icon, through icondrv.c's portIconTexOverride) */

#define FACE_MAX 64
#define FACE_OUTLINE 6 /* the sticker edge's margin, texels */
static unsigned char sFace[PM64_PARTNER_COUNT][FACE_MAX * FACE_MAX * 4];
static unsigned sFaceGen[PM64_PARTNER_COUNT];
static int sFaceSize[PM64_PARTNER_COUNT];

static int buildFace(int p);
static int pm64Log(void);
static void heartUnder(float* out4);

/* the game's icon in RGBA (top row first), for its HP heart: GX's RGB5A3, RGBA8, IA8, I8 and CMPR (the formats
 * the HUD's icons come in); 0 for another */
static int decodeGameIcon(const uint8_t* src, int fmt, int w, int h, uint8_t* out) {
    int bx, by, x, y;
    if (src == NULL || w <= 0 || h <= 0 || w > FACE_MAX * 2 || h > FACE_MAX * 2) {
        return 0;
    }
    if (fmt == GX_TF_RGB5A3 || fmt == GX_TF_IA8) {
        for (by = 0; by < h; by += 4) {
            for (bx = 0; bx < w; bx += 4) {
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++, src += 2) {
                        const unsigned v = (unsigned)src[0] << 8 | src[1];
                        uint8_t* o = out + ((size_t)(by + y) * w + bx + x) * 4;
                        if (by + y >= h || bx + x >= w) {
                            continue;
                        }
                        if (fmt == GX_TF_IA8) {
                            o[0] = o[1] = o[2] = src[1];
                            o[3] = src[0];
                        } else if (v & 0x8000) {
                            o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
                            o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
                            o[2] = (uint8_t)((v & 31) * 255 / 31);
                            o[3] = 255;
                        } else {
                            o[0] = (uint8_t)(((v >> 8) & 15) * 17);
                            o[1] = (uint8_t)(((v >> 4) & 15) * 17);
                            o[2] = (uint8_t)((v & 15) * 17);
                            o[3] = (uint8_t)(((v >> 12) & 7) * 255 / 7);
                        }
                    }
                }
            }
        }
        return 1;
    }
    if (fmt == GX_TF_RGBA8) {
        for (by = 0; by < h; by += 4) {
            for (bx = 0; bx < w; bx += 4, src += 64) {
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 4; x++) {
                        const int k = y * 4 + x;
                        uint8_t* o = out + ((size_t)(by + y) * w + bx + x) * 4;
                        if (by + y < h && bx + x < w) {
                            o[3] = src[k * 2];
                            o[0] = src[k * 2 + 1];
                            o[1] = src[32 + k * 2];
                            o[2] = src[32 + k * 2 + 1];
                        }
                    }
                }
            }
        }
        return 1;
    }
    if (fmt == GX_TF_I8) {
        for (by = 0; by < h; by += 4) {
            for (bx = 0; bx < w; bx += 8) {
                for (y = 0; y < 4; y++) {
                    for (x = 0; x < 8; x++, src++) {
                        uint8_t* o = out + ((size_t)(by + y) * w + bx + x) * 4;
                        if (by + y < h && bx + x < w) {
                            o[0] = o[1] = o[2] = o[3] = *src;
                        }
                    }
                }
            }
        }
        return 1;
    }
    if (fmt == GX_TF_CMPR) {
        for (by = 0; by < h; by += 8) {
            for (bx = 0; bx < w; bx += 8) {
                int sub;
                for (sub = 0; sub < 4; sub++, src += 8) {
                    const unsigned c0 = (unsigned)src[0] << 8 | src[1], c1 = (unsigned)src[2] << 8 | src[3];
                    uint8_t pal[4][4];
                    int k;
                    for (k = 0; k < 2; k++) {
                        const unsigned c = k ? c1 : c0;
                        pal[k][0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
                        pal[k][1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
                        pal[k][2] = (uint8_t)((c & 31) * 255 / 31);
                        pal[k][3] = 255;
                    }
                    for (k = 0; k < 3; k++) {
                        if (c0 > c1) {
                            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
                            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
                        } else {
                            pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
                            pal[3][k] = 0;
                        }
                    }
                    pal[2][3] = 255;
                    pal[3][3] = c0 > c1 ? 255 : 0;
                    for (y = 0; y < 4; y++) {
                        for (x = 0; x < 4; x++) {
                            const int px = bx + (sub & 1) * 4 + x, py = by + (sub >> 1) * 4 + y;
                            const int idx = (src[4 + y] >> (6 - 2 * x)) & 3;
                            if (px < w && py < h) {
                                memcpy(out + ((size_t)py * w + px) * 4, pal[idx], 4);
                            }
                        }
                    }
                }
            }
        }
        return 1;
    }
    return 0;
}

/* The HP heart of the HUD's partner icon is painted into each TTYD partner's face texture, at its top right,
 * behind the face (statuswindow.c draws the one icon; there is no separate heart). A PM64 face replaces that
 * texture, so the heart is laid under the PM64 face, cut from Koops' icon (the one whose face covers none of
 * it; the hearts sit in the same place in every partner's icon): the heart's shape found from its pink (its
 * top rows give its width; it narrows to its tip below), its pinks, reds and the white and grey edge inside that. */
static int sHeartFetch;
static void heartUnder(float* out4) {
    static uint8_t icon[FACE_MAX * 2 * FACE_MAX * 2 * 4];
    static float heart[FACE_MAX * 2 * FACE_MAX * 2 * 4]; /* premultiplied, the icon's size */
    static int lo[FACE_MAX * 2], hi[FACE_MAX * 2];
    GXTexObj koops;
    int gw, gh, x, y, found = 0, ox, oy, y0 = -1, xl = 1 << 20, xr = -1;
    float hgt, half;
    sHeartFetch = 1;
    iconGetTexObj(&koops, ICON_1A5);
    sHeartFetch = 0;
    gw = GXGetTexObjWidth(&koops);
    gh = GXGetTexObjHeight(&koops);
    if (GXGetTexObjData(&koops) == NULL || gw <= 0 || gh <= 0 || gw > FACE_MAX * 2 || gh > FACE_MAX * 2 ||
        !decodeGameIcon((const uint8_t*)GXGetTexObjData(&koops), (int)GXGetTexObjFmt(&koops), gw, gh, icon)) {
        return;
    }
    /* each row's span of the heart's pink, in the icon's top right */
    for (y = 0; y < gh; y++) {
        lo[y] = gw;
        hi[y] = -1;
        for (x = gw * 3 / 8; x < gw && y < gh * 5 / 8; x++) {
            const uint8_t* c = icon + ((size_t)y * gw + x) * 4;
            if (c[3] > 128 && c[0] > 180 && c[0] > c[1] + 60 && c[2] < c[1] + 60) {
                lo[y] = x < lo[y] ? x : lo[y];
                hi[y] = x > hi[y] ? x : hi[y];
                found++;
                y0 = y0 < 0 ? y : y0;
            }
        }
    }
    if (pm64Log()) {
        printf("[pm64] HUD heart: %d pink texels in Koops' %dx%d icon\n", found, gw, gh);
        fflush(stdout);
    }
    if (found < 16) {
        return;
    }
    for (y = y0; y < y0 + 9 && y < gh; y++) {
        if (hi[y] >= 0) {
            xl = lo[y] < xl ? lo[y] : xl;
            xr = hi[y] > xr ? hi[y] : xr;
        }
    }
    hgt = (float)(xr - xl) * 0.92f;
    half = (float)(xr - xl) * 0.5f;
    for (y = 0; y < gh; y++) {
        /* the heart's sides: straight down to the middle, then in to its tip */
        const float t = ((float)y - ((float)y0 + hgt * 0.45f)) / (hgt * 0.55f);
        const float cut = half * (t > 0.0f ? t : 0.0f);
        const float l = (float)xl + cut - 2.5f, r = (float)xr - cut + 2.5f;
        const int rowIn = y >= y0 - 3 && (float)y <= (float)y0 + hgt + 2.0f;
        for (x = 0; x < gw; x++) {
            const uint8_t* c = icon + ((size_t)y * gw + x) * 4;
            float* h = heart + ((size_t)y * gw + x) * 4;
            const int mx = c[0] > c[1] ? c[0] : c[1], mn = c[0] < c[1] ? c[0] : c[1];
            const int mx3 = mx > c[2] ? mx : c[2], mn3 = mn < c[2] ? mn : c[2];
            const int red = c[0] > c[1] + 40 && c[2] < c[1] + 70, grey = mx3 - mn3 < 40;
            const float al = rowIn && (float)x >= l && (float)x <= r && (red || grey) ? (float)c[3] / 255.0f : 0.0f;
            h[0] = c[0] * al;
            h[1] = c[1] * al;
            h[2] = c[2] * al;
            h[3] = al;
        }
    }
    /* under the face, texel for texel. icondrv.c's iconGX sizes an icon's quad by its texture (icon.bin gives the
       HUD faces 0x0) about its bottom centre, so our FACE_MAX face is a bigger quad than the game's gw x gh one:
       the game's icon is its bottom-centre gw x gh. (Scaling the heart up to FACE_MAX put it above the face.) */
    for (oy = 0; oy < FACE_MAX; oy++) {
        for (ox = 0; ox < FACE_MAX; ox++) {
            const int sx = ox - (FACE_MAX - gw) / 2, sy = oy - (FACE_MAX - gh);
            float hp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float* o = out4 + ((size_t)oy * FACE_MAX + ox) * 4;
            int c;
            if (sx >= 0 && sy >= 0 && sx < gw && sy < gh) {
                for (c = 0; c < 4; c++) {
                    hp[c] = heart[((size_t)sy * gw + sx) * 4 + c];
                }
            }
            {
                const float fa = o[3], al = fa + hp[3] * (1.0f - fa);
                for (c = 0; c < 3; c++) {
                    const float v = o[c] * fa + hp[c] * (1.0f - fa);
                    o[c] = al > 0.001f ? v / al : 0.0f;
                }
                o[3] = al;
            }
        }
    }
}

static int sIconFace; /* portPm64IconFace */

void portPm64IconFace(int p) {
    sIconFace = p > 0 && p < PM64_PARTNER_COUNT ? p : 0;
}

const void* portPm64HudFace(unsigned iconId, const void* gameTex, int gameFmt, int gameW, int gameH, int* w, int* h) {
    int host = 0, p;
    if (sIconFace > 0 && iconId >= ICON_155 && iconId <= ICON_161) {
        /* (the battle's partner list: a Paper Mario 64 row's party menu icon, his face) */
        int size;
        const void* face = portPm64MenuFace(sIconFace, &size);
        if (face != NULL) {
            *w = *h = size;
            return face;
        }
    }
    if (iconId == ICON_1A4) {
        host = 1;
    } else if (iconId == ICON_1A5) {
        host = 2;
    } else if (iconId == ICON_1A6) {
        host = 3;
    } else if (iconId >= ICON_1A7 && iconId <= ICON_1AD) {
        host = 4; /* (Yoshi's, one per colour) */
    } else if (iconId == ICON_1AE) {
        host = 5;
    } else if (iconId == ICON_1AF) {
        host = 6;
    } else if (iconId == ICON_1B0) {
        host = 7;
    }
    if (host == 0 || gp == NULL || sHeartFetch) {
        return NULL; /* (sHeartFetch: Koops' own icon, for the heart) */
    }
    p = portPm64PartnerOn(host);
    {
        /* tests (TTYD_PM64_LOG): each partner's icon's look, when it changes */
        static signed char sLogged[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        if (sLogged[host] != p && pm64Log()) {
            sLogged[host] = (signed char)p;
            printf("[pm64] partner %d's icon as %s\n", host, p > 0 ? kWho[p].english : "itself");
            fflush(stdout);
        }
    }
    if (p == 0) {
        return NULL;
    }
    {
        /* (rebuilt when the host's icon changes: Yoshi's, one per colour) */
        static const void* sFaceTex[PM64_PARTNER_COUNT];
        if (sFaceTex[p] != gameTex) {
            sFaceTex[p] = gameTex;
            sFaceGen[p] = 0;
        }
    }
    if (sFaceGen[p] != portPm64Generation() + 1) {
        sFaceSize[p] = buildFace(p);
        sFaceGen[p] = portPm64Generation() + 1;
    }
    if (sFaceSize[p] == 0) {
        return NULL;
    }
    *w = *h = sFaceSize[p];
    return sFace[p];
}

void portPm64PartnerWear(int party, int p) {
    if (gp == NULL || party < 1 || party > 7) {
        return;
    }
    if (p != 0 && (p >= PM64_PARTNER_COUNT || kWho[p].host != party)) {
        return;
    }
    if (p == 0 && portPm64SlotStandIn(party)) {
        return; /* (Watt's alone: Ms. Mowz isn't recruited) */
    }
    gp->gsw[PM64_GSW_WORN + party - 1] = (u8)p;
}

int portPm64GswOwn(int i) {
    return i >= PM64_GSW_WORN && i < PM64_GSW_WORN + 7;
}

void portPm64WornGet(unsigned char out[8]) {
    int i;
    for (i = 0; i < 8; i++) {
        out[i] = gp != NULL && i < 7 ? gp->gsw[PM64_GSW_WORN + i] : 0;
    }
}

void portPm64WornSet(const unsigned char in[8]) {
    int i;
    for (i = 0; gp != NULL && i < 7; i++) {
        gp->gsw[PM64_GSW_WORN + i] = in[i] < PM64_PARTNER_COUNT && (in[i] == 0 || kWho[in[i]].host == i + 1) ? in[i] : 0;
    }
}

/* GSW 2011: bit p (1..7) partner p's joining told (with its TTYD counterpart: portPm64JoinFrame), bit 0
 * all of them (0.5.0's first builds' one greeter); GSW 2019 bit p - 8: Bow */
int portPm64PartnerMet(int p) {
    if (gp == NULL || p < 0 || p >= PM64_PARTNER_COUNT) {
        return 0;
    }
    if (gp->gsw[PM64_GSW_MET] & 1) {
        return 1;
    }
    return p > 0 && (p < 8 ? gp->gsw[PM64_GSW_MET] >> p & 1 : gp->gsw[PM64_GSW_MET2] >> (p - 8) & 1);
}

void portPm64PartnerSetMet(int p) {
    if (gp != NULL && p > 0 && p < 8) {
        gp->gsw[PM64_GSW_MET] |= (u8)(1 << p);
    } else if (gp != NULL && p >= 8 && p < PM64_PARTNER_COUNT) {
        gp->gsw[PM64_GSW_MET2] |= (u8)(1 << (p - 8));
    }
}

/* --- "Beat their boss": each partner's PM64 boss fight (modes_aurora.c's PM64 fights: Jr. Troopa 0 .. Bowser 9, the
 * Koopa Bros.' rematch 10) */
static const signed char kBossOf[PM64_PARTNER_COUNT] = {
    -1, 0,                             /* Goombario: Jr. Troopa */
    PM64_FIGHT_KOOPA_BROS,             /* Kooper: the Koopa Bros. */
    PM64_FIGHT_KOOPA_BROS_REMATCH,     /* Bombette: their rematch (once Bobbery has joined) */
    2,                                 /* Parakarry: Tutankoopa */
    4,                                 /* Watt: General Guy */
    5,                                 /* Sushie: Lava Piranha */
    6,                                 /* Lakilester: Huff N. Puff */
    3,                                 /* Bow: Tubba Blubba */
};

int portPm64FightBoss(int fight) {
    if (fight == PM64_FIGHT_KOOPA_BROS_REMATCH) {
        return PM64_FIGHT_KOOPA_BROS;
    }
    return fight >= 0 && fight < PM64_BOSS_RUSH_FIGHTS ? fight : -1;
}

int portPm64FightAt(int i) {
    if (i < 0 || i >= PM64_BOSS_FIGHTS) {
        return -1;
    }
    if (i <= PM64_FIGHT_KOOPA_BROS) {
        return i;
    }
    return i == PM64_FIGHT_KOOPA_BROS + 1 ? PM64_FIGHT_KOOPA_BROS_REMATCH : i - 1;
}

int portPm64JoinByBoss(void) {
    /* ("from the start" over it: every one there with the TTYD partner, quietly) */
    return setting("pm64_join") != 0 && setting("pm64_met_all") == 0;
}

int portPm64BossBeaten(int f) {
    if (gp == NULL || f < 0 || f >= PM64_BOSS_FIGHTS) {
        return 0;
    }
    return gp->gsw[PM64_GSW_BOSSES + f / 8] >> (f % 8) & 1;
}

void portPm64BossSetBeaten(int f) {
    if (gp != NULL && f >= 0 && f < PM64_BOSS_FIGHTS) {
        gp->gsw[PM64_GSW_BOSSES + f / 8] |= (u8)(1 << (f % 8));
        if (f < 10) {
            /* the boss's achievement however it was won (the story, the list, the board, a rush): bit = the
               registry's order, which fights 0-9 share; the rematch (10) has none of its own */
            portAchievementCounterBit("pm64_bosses", f);
        }
        printf("[pm64] boss %d beaten on this file\n", f);
        fflush(stdout);
    }
}

int portPm64BossPartners(int f, int out[2]) {
    int p, n = 0;
    for (p = 1; p < PM64_PARTNER_COUNT && n < 2; p++) {
        if (kBossOf[p] == f) {
            out[n++] = p;
        }
    }
    return n;
}

/* --- Watt without Ms. Mowz (port/include/port/pm64.h portPm64SlotStandIn)
 *
 * Watt's time is after chapter 4 by the story, Ms. Mowz or not (her quest can come much later, or never). He joins
 * into her party slot (7): the pouch's joined flag set for it, with GSW 2019 bit 7 saying the slot is his alone;
 * the slot always wears him then, she isn't on the wheel, and the partner counts (achievements, bingo) and the open
 * world's checks leave her out (partyChkJoin(7) stays true: the game's switch and menus go by it, for him). Her quest is the game's own (its flags, not the pouch's), and her own
 * joining (partyJoin 7: the quest, the open world's item) clears the mark: from then on the slot is hers and Watt
 * is on it as on any host. While the PM64 partners are off (the setting, unlinked, online without them) the slot's
 * joined flag is taken away again, so she never stands there unrecruited; the mark stays, and he's back with them. */
#define PM64_GSW_STANDIN PM64_GSW_MET2
#define PM64_STANDIN_WATT 0x80 /* (bit 7: MET2's bits are p - 8, Bow's 0) */
#define WATT_HOST 7

static int standInMarked(void) {
    return gp != NULL && (gp->gsw[PM64_GSW_STANDIN] & PM64_STANDIN_WATT) != 0;
}

int portPm64SlotStandIn(int party) {
    return party == WATT_HOST && standInMarked();
}

/* a lent loadout runs (every TTYD partner joined for a mode's run): nobody joins meanwhile */
static int sJoinHold;

void portPm64PartyJoined(int party) {
    /* (a lent loadout's partyJoin(7) isn't Ms. Mowz joining: its pouch is put back after the run, Watt's mark stays) */
    if (party == WATT_HOST && standInMarked() && !sJoinHold) {
        gp->gsw[PM64_GSW_STANDIN] &= (u8)~PM64_STANDIN_WATT;
        printf("[pm64] Ms. Mowz joined: her slot is hers, Watt on it as on any host\n");
        fflush(stdout);
    }
}

/* Watt's time: chapter 4 over, back in Rogueport (the story as menus read it: the open world's chapter tracks too):
 * from the story value his boss, General Guy, waits from (pm64_story_aurora.c's kBosses: Frankly's house after
 * chapter 4). (It was "Off to the Keys", 229: General Guy, beaten from 222 on, then brought no Watt until Mario
 * set sail; tests/port/full_run/pm64/95p_general_guy.txt) */
static int wattTime(void) {
    return gp != NULL && swStoryPeek() >= STORY_STG4_31_FRANKLYS_HOUSE;
}

/* the TTYD counterpart (the host) has joined, by the story, a save made since or the open world's item; Watt's:
 * Ms. Mowz, or chapter 4 over */
static int hostJoined(int p) {
    PouchData* pouch = pouchGetPtr();
    unsigned flags;
    if (pouch == NULL) {
        return 0;
    }
    flags = pouch->partyData[kWho[p].host].flags;
    if (p == PM64_PARTNER_WATT && !(flags & POUCH_PARTY_FLAG_AWAY) && wattTime()) {
        return 1;
    }
    return (flags & POUCH_PARTY_FLAG_JOINED) && !(flags & POUCH_PARTY_FLAG_AWAY);
}

void portPm64JoinHold(int on) {
    sJoinHold = on;
}

int portPm64PartnerAvailable(int p) {
    if (p <= 0 || p >= PM64_PARTNER_COUNT || !portPm64PartnersActive() || pouchGetPtr() == NULL || !hostJoined(p)) {
        return 0;
    }
    /* "Beat their boss": its PM64 boss beaten too; one whose joining was told before (in the other mode) stays */
    return !portPm64JoinByBoss() || portPm64PartnerMet(p) || portPm64BossBeaten(kBossOf[p]);
}

/* each field frame: Watt in Ms. Mowz's slot (joined into it once he's available there and she isn't), and the
 * slot's flag kept as the partners being on or off says */
static void standInFrame(void) {
    PouchData* pouch = pouchGetPtr();
    PouchPartyData* slot;
    if (gp == NULL || pouch == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        return;
    }
    slot = &pouch->partyData[WATT_HOST];
    if (!portPm64PartnersActive()) {
        if (standInMarked() && (slot->flags & POUCH_PARTY_FLAG_JOINED) && !sJoinHold) {
            const PartyEntry* out = partyGetPtr(marioGetPartyId());
            if (out != NULL && out->currentMemberId == WATT_HOST) {
                /* he is the one out: an unrecruited Ms. Mowz must not follow Mario (nor be saved as out), so
                   another partner who has joined takes over, or none; at a free moment only (not in a scene) */
                int i, other = -1;
                if (!marioChkCtrl()) {
                    return;
                }
                for (i = 1; i < WATT_HOST && other < 0; i++) {
                    if ((pouch->partyData[i].flags & POUCH_PARTY_FLAG_JOINED) &&
                        !(pouch->partyData[i].flags & POUCH_PARTY_FLAG_AWAY)) {
                        other = i;
                    }
                }
                marioPartyKill();
                if (other > 0) {
                    marioPartyEntry((PartyId)other);
                }
                printf("[pm64] Watt was out as his slot is put away: partner %d out instead\n", other);
                fflush(stdout);
            }
            slot->flags &= (u16)~POUCH_PARTY_FLAG_JOINED;
            printf("[pm64] Watt's slot put away (the Paper Mario 64 partners off): Ms. Mowz not recruited\n");
            fflush(stdout);
        }
        return;
    }
    if (!standInMarked()) {
        if (!(slot->flags & POUCH_PARTY_FLAG_JOINED) && !sJoinHold && portPm64PartnerAvailable(PM64_PARTNER_WATT)) {
            gp->gsw[PM64_GSW_STANDIN] |= PM64_STANDIN_WATT;
            slot->flags |= POUCH_PARTY_FLAG_JOINED;
            gp->gsw[PM64_GSW_WORN + WATT_HOST - 1] = PM64_PARTNER_WATT;
            printf("[pm64] Watt joins into Ms. Mowz's slot (she isn't recruited)\n");
            fflush(stdout);
        }
        return;
    }
    if (!(slot->flags & POUCH_PARTY_FLAG_JOINED) && !sJoinHold && wattTime()) {
        /* (the partners on again; not in a chapter replay from before his time: chapters_aurora.c) */
        slot->flags |= POUCH_PARTY_FLAG_JOINED;
        printf("[pm64] Watt back in Ms. Mowz's slot\n");
        fflush(stdout);
    }
    gp->gsw[PM64_GSW_WORN + WATT_HOST - 1] = PM64_PARTNER_WATT;
}

/* an item of Paper Mario 64's (a badge, a dish or an ingredient: port/include/port/battlex.h) */
static int pm64Item(int id) {
    return (id >= PORT_PM64_BADGE_MIN && id < PORT_PM64_BADGE_MIN + PORT_PM64_BADGE_COUNT) ||
           (id >= PORT_PM64_FOOD_MIN && id < PORT_PM64_FOOD_MIN + PORT_PM64_FOOD_COUNT);
}

static int fileHasPm64(void) {
    PouchData* pouch = pouchGetPtr();
    int i;
    for (i = PORT_GSW_PM64_FIRST; i <= PORT_GSW_PM64_LAST; i++) {
        if (gp->gsw[i] != 0) {
            return 1;
        }
    }
    for (i = PORT_GSW_PM64_BOSSES_FIRST; i <= PORT_GSW_PM64_BOSSES_LAST; i++) {
        if (gp->gsw[i] != 0) {
            return 1;
        }
    }
    if (pouch == NULL) {
        return 0;
    }
    for (i = 0; i < MAX_HELD_ITEMS; i++) {
        if (pm64Item(pouch->heldItems[i])) {
            return 1;
        }
    }
    for (i = 0; i < MAX_STORED_ITEMS; i++) {
        if (pm64Item(pouch->storedItems[i])) {
            return 1;
        }
    }
    for (i = 0; i < MAX_BADGES; i++) {
        if (pm64Item(pouch->badges[i]) || pm64Item(pouch->equippedBadges[i])) {
            return 1;
        }
    }
    return 0;
}

int portPm64UnlinkedNotice(void) {
    static int sTold;
    if (sTold || gp == NULL || portPm64Linked() != PM64_NONE || seqGetSeq() != SEQ_GAME || gp->inBattle ||
        !marioChkCtrl() || !fileHasPm64()) {
        return 0;
    }
    sTold = 1;
    printf("[pm64] not linked: this file's Paper Mario 64 partners and items show as TTYD's\n");
    fflush(stdout);
    return 1;
}

int portPm64BossBrings(int f, int out[2]) {
    int all[2], i, n = 0;
    const int count = portPm64BossPartners(f, all);
    if (!portPm64JoinByBoss() || !portPm64PartnersActive() || sJoinHold) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (hostJoined(all[i]) && !portPm64PartnerAvailable(all[i])) {
            out[n++] = all[i];
        }
    }
    return n;
}

/* --- joining: each PM64 partner with its TTYD counterpart (kWho's host), told once ("Kooper joined too!") when
 * Mario is free again after the counterpart's own joining. A file where the counterpart joined before (an older
 * save, or the ROM linked mid-game) has them at once, told as it plays on. The told mark is GSW 2011's bit p
 * (Bow's, 2019's bit 0), written only while the partners are on (a file played without them stays the game's
 * own). "Paper Mario 64 partners from the start" marks them told quietly. */
#define NOTICES 9
static int sNoticeQ[NOTICES], sNoticeN;    /* to tell once Mario is free, in order */
static int sNoticeReady[NOTICES], sReadyN; /* for the overlay (portPm64JoinNotice) */
#define CALM_FRAMES 45  /* Mario free this long before a notice */
#define NOTICE_GAP 180  /* and this much more after the one before (the overlay shows each 3.5 s) */
static int sCalm;

void portPm64JoinFrame(void) {
    PouchData* pouch;
    int p, i;
    {
        const int seq = seqGetSeq();
        if (seq == SEQ_TITLE || seq == SEQ_LOAD || seq == SEQ_GAME_OVER || seq == SEQ_LOGO) {
            /* (a file left: what it queued isn't for the next one) */
            sNoticeN = sReadyN = 0;
            sCalm = 0;
            return;
        }
    }
    standInFrame();
    if (gp == NULL || gp->inBattle || seqGetSeq() != SEQ_GAME || !portPm64PartnersActive() || sJoinHold ||
        (pouch = pouchGetPtr()) == NULL) {
        sCalm = sCalm > 0 ? 0 : sCalm; /* (a battle, a map change: calm again from its end) */
        return;
    }
    for (p = 1; p < PM64_PARTNER_COUNT; p++) {
        const int byBoss = portPm64JoinByBoss();
        if (portPm64PartnerMet(p) || !hostJoined(p) || (byBoss && !portPm64BossBeaten(kBossOf[p]))) {
            continue;
        }
        portPm64PartnerSetMet(p);
        printf("[pm64] %s joins with partner %d%s\n", kWho[p].english, kWho[p].host, byBoss ? ", its boss beaten" : "");
        fflush(stdout);
        /* (from the start: quietly, with the TTYD partner; a boss beaten for one is always told) */
        if ((byBoss || !setting("pm64_met_all")) && sNoticeN < NOTICES) {
            sNoticeQ[sNoticeN++] = p;
        }
    }
    /* told one by one at a calm moment: Mario free in the field for a moment (no scene, menu or text), and the
       one before gone by then (a late save with the partners just turned on can have several at once: each its own
       notice and sound, none added silently) */
    if (marioChkCtrl() && marioChkKey() && marioStGetSystemLevel() == 0 && !winCheck()) {
        if (sCalm < CALM_FRAMES) {
            sCalm++;
        }
        if (sNoticeN > 0 && sReadyN < NOTICES && sCalm >= CALM_FRAMES) {
            const int who = sNoticeQ[0];
            sCalm = -NOTICE_GAP; /* (the next after this one's few seconds) */
            portPm64SfxGreet(who); /* its own sound (port/include/port/pm64_sfx.h) */
            sNoticeReady[sReadyN++] = who;
            memmove(sNoticeQ, sNoticeQ + 1, sizeof(sNoticeQ[0]) * (size_t)(--sNoticeN));
            printf("[pm64] notice: %s joined too\n", kWho[who].english);
            fflush(stdout);
        }
    } else if (sCalm > 0) {
        sCalm = 0;
    }
}

int portPm64JoinNotice(void) {
    int p;
    if (sReadyN == 0) {
        return 0;
    }
    p = sNoticeReady[0];
    memmove(sNoticeReady, sNoticeReady + 1, sizeof(sNoticeReady[0]) * (size_t)(--sReadyN));
    return p;
}

int portPm64WheelChoice(int arg, int current) {
    const int p = arg - PM64_WHEEL_BASE;
    int host;
    if (!portPm64PartnerAvailable(p)) {
        return -1;
    }
    host = kWho[p].host;
    portPm64PartnerWear(host, p);
    printf("[pm64] %s in %d's place\n", kWho[p].english, host);
    fflush(stdout);
    return host != current ? host : -1;
}

/* the pair's look after host's current one: each PM64 partner met on it in turn, then the TTYD partner again */
int portPm64PairNext(int host) {
    int cur, p;
    if (host < 1 || host > 7 || !portPm64PartnersActive()) {
        return -1;
    }
    cur = portPm64PartnerOn(host);
    for (p = cur + 1; p < PM64_PARTNER_COUNT; p++) {
        if (kWho[p].host == host && portPm64PartnerAvailable(p)) {
            return p;
        }
    }
    if (cur != 0) {
        return 0;
    }
    return -1;
}

/* BattleChangeParty: the row's look put on its host (the wheel's path: the GSW the game keeps), the unit kind */
int portPm64BattleChangeKind(int partyKind) {
    const int look = PM64_BTL_LOOK_OF(partyKind), kind = PM64_BTL_KIND_OF(partyKind);
    int host;
    if (look == 0) {
        return kind;
    }
    host = BattleTransPartyId((BattleUnitType)kind);
    if (look == PM64_LOOK_TTYD) {
        portPm64PartnerWear(host, 0);
        printf("[pm64] battle: partner %d themselves again\n", host);
    } else if (look < PM64_PARTNER_COUNT && kWho[look].host == host && portPm64PartnerAvailable(look)) {
        portPm64PartnerWear(host, look);
        printf("[pm64] battle: %s in %d's place\n", kWho[look].english, host);
    }
    fflush(stdout);
    return kind;
}

/* --- the draw */

/* the partners' models (TTYD PartyId by file; Yoshi's colours are c_babyyoshi2 .. 7) */
static int hostOf(const char* file) {
    static const char* const kModel[8] = {NULL, "c_pkr", "c_pnk", "c_bomt_n", NULL, "c_windy", "c_vivian", "c_tyutyu"};
    int p;
    if (!strncmp(file, "c_babyyoshi", 11) && (file[11] == '\0' || (file[11] >= '2' && file[11] <= '7' && file[12] == '\0'))) {
        return 4;
    }
    for (p = 1; p < 8; p++) {
        if (kModel[p] != NULL && !strcmp(file, kModel[p])) {
            return p;
        }
    }
    return 0;
}

/* a TTYD animation's move (the letter after the model's prefix: PKR_W_1 -> W)
 * as the PM64 sprite's: per partner, field then battle, the animation for
 * standing, walking, running, jumping, falling, talking, hurt, glad and the
 * partner's own move */
enum { A_IDLE, A_WALK, A_RUN, A_JUMP, A_FALL, A_TALK, A_HURT, A_GLAD, A_MOVE, A_COUNT };
static const signed char kAnims[PM64_PARTNER_COUNT][2][A_COUNT] = {
    {{0}, {0}},
    {{1, 2, 3, 4, 5, 8, 7, 9, 6}, {1, 2, 3, 4, 5, 15, 10, 17, 6}},          /* Goombario: Headbonk */
    {{3, 4, 5, 6, 7, 13, 12, 16, 9}, {4, 5, 6, 7, 8, 20, 17, 22, 12}},      /* Kooper: the shell spinning */
    {{3, 5, 7, 9, 10, 14, 13, 15, 16}, {4, 6, 8, 10, 11, 23, 18, 25, 16}},  /* Bombette: about to blow, Body Slam */
    {{1, 2, 3, 8, 1, 6, 5, 7, 9}, {1, 2, 3, 20, 5, 18, 14, 19, 21}},        /* Parakarry: carrying */
    {{1, 2, 3, 3, 1, 4, 7, 6, 5}, {1, 2, 3, 3, 1, 4, 12, 8, 6}},            /* Watt: straining, charged (17 is the bolts alone) */
    {{1, 2, 3, 12, 11, 4, 8, 6, 10}, {1, 2, 3, 5, 6, 1, 13, 17, 10}},       /* Sushie: the ride, Squirt */
    {{1, 5, 7, 7, 7, 9, 4, 13, 6}, {1, 2, 3, 3, 3, 1, 10, 7, 18}},          /* Lakilester: straining, a spiny thrown */
    {{1, 2, 3, 1, 1, 4, 12, 11, 2}, {2, 2, 3, 3, 3, 4, 21, 17, 12}},        /* Bow: floating (Outta Sight: Walk, as PM64) */
};

static int moveOf(const char* anim) {
    const char* u = anim != NULL ? strchr(anim, '_') : NULL;
    /* (Bobbery's poses have no model prefix: "A_3", "R_2": the letter before the underscore) */
    const char c = u != NULL ? (u - anim == 1 ? anim[0] : u[1]) : anim != NULL ? anim[0] : 'S';
    switch (c) {
    case 'W':
        return A_WALK;
    case 'R':
        return A_RUN;
    case 'J':
        return A_JUMP;
    case 'F':
    case 'K':
        return A_FALL;
    case 'T':
        return A_TALK;
    case 'D':
        return A_HURT;
    case 'Y':
    case 'H':
        return A_GLAD;
    case 'A':
        return A_MOVE;
    default:
        return A_IDLE;
    }
}

/* --- battle: each host pose as the PM64 battle sprite's own animation
 *
 * The letter class above can't tell apart what shares a letter (a host's D_2 low HP from D_1 hurt and D_3
 * knocked out, Koops' K_1 shell from a kiss, H_1 flipped from glad, Goombella's 2A_* Tattle), so in battle each
 * partner has its rows for every state (B_*), and the host poses its moves use are named one by one
 * (kBattlePose). Rows are the battle sprite's animations, as PM64's battle code plays them. */
enum {
    B_IDLE, B_WALK, B_RUN, B_JUMP, B_FALL, B_TALK, B_HURT, B_GLAD, B_MOVE, /* (A_* order) */
    B_LOW,      /* D_2: low HP, poison, an escape's end: PM64's partners have no HP, so standing */
    B_KO,       /* D_3 at 0 HP: knocked down */
    B_STILL,    /* D_4 / D_5: stopped, frozen: the still frame */
    B_SLEEP,    /* N_* */
    B_DIZZY,    /* X_* */
    B_GUARD,    /* G_*: bracing */
    B_APPEAL,   /* O_* */
    B_ITEM,     /* I_* */
    B_BURNHURT, /* a fire hit (the host's part blackened): the dark hurt row */
    B_BURNT,    /* after it: the dark standing row */
    B_VICTORY,  /* Y_* once the battle is won */
    B_COUNT
};
static const signed char kBattleRows[PM64_PARTNER_COUNT][B_COUNT] = {
    {0},
    /*        idl wlk run jmp fal tlk hrt gld mov low ko stl slp dzy grd apl itm bhr brn win */
    /* Goombario */ {1, 2, 3, 4, 5, 15, 10, 17, 6, 1, 20, 0, 19, 1, 9, 16, 17, 11, 12, 16},
    /* Kooper */ {4, 5, 6, 7, 8, 20, 17, 21, 12, 4, 3, 0, 27, 4, 9, 21, 21, 18, 19, 21},
    /* Bombette */ {4, 6, 8, 10, 11, 23, 18, 24, 16, 4, 30, 0, 29, 4, 22, 24, 25, 19, 20, 24},
    /* Parakarry */ {1, 1, 4, 20, 5, 18, 14, 19, 21, 1, 25, 0, 0, 1, 12, 19, 19, 15, 16, 19},
    /* Watt */ {1, 1, 1, 1, 1, 4, 12, 8, 6, 1, 14, 0, 10, 15, 5, 8, 8, 13, 13, 8},
    /* Sushie */ {1, 2, 3, 5, 6, 17, 13, 18, 10, 1, 20, 0, 19, 1, 7, 18, 18, 14, 15, 18},
    /* Lakilester */ {1, 1, 4, 1, 1, 8, 10, 7, 18, 1, 26, 0, 14, 1, 11, 7, 7, 12, 13, 7},
    /* Bow */ {2, 2, 3, 3, 3, 4, 21, 17, 12, 2, 26, 0, 19, 2, 27, 17, 17, 22, 23, 17}, /* (PM64: standing is Walk) */
};

/* the host poses of the moves (the name after the model's prefix; Bobbery's have none), per partner */
enum { K_NONE = -1, K_KOOPER_SHELL = -2, K_ASCEND = -3 };
static const struct {
    unsigned char p;
    const char* pose;
    signed char anim; /* or K_*: worked out as it plays */
} kBattlePose[] = {
    /* Goombario: the headbonk's crouch, the bonk at each strike (K_ASCEND: rising 4, coming down 6 onto the
       enemy, 5 dropping back), the spin of an avoided counter, Tattle's look and telling */
    {PM64_PARTNER_GOOMBARIO, "A_2B", 9},
    {PM64_PARTNER_GOOMBARIO, "J_1A", K_ASCEND},
    {PM64_PARTNER_GOOMBARIO, "J_1B", K_ASCEND},
    {PM64_PARTNER_GOOMBARIO, "A_1", 6},
    {PM64_PARTNER_GOOMBARIO, "2A_1", 18},
    {PM64_PARTNER_GOOMBARIO, "2A_2", 18},
    {PM64_PARTNER_GOOMBARIO, "2A_3", 18},
    {PM64_PARTNER_GOOMBARIO, "2A_4", 15},
    {PM64_PARTNER_GOOMBARIO, "2A_5", 15},
    /* Kooper: into the shell and out of it (which, by what was before), spinning, flipped, the stylish pops */
    {PM64_PARTNER_KOOPER, "A_1", 12},
    {PM64_PARTNER_KOOPER, "K_1", K_KOOPER_SHELL},
    {PM64_PARTNER_KOOPER, "K_2", K_KOOPER_SHELL},
    {PM64_PARTNER_KOOPER, "H_1", 25},
    {PM64_PARTNER_KOOPER, "N_2", 3},
    {PM64_PARTNER_KOOPER, "E_1", 15},
    {PM64_PARTNER_KOOPER, "E_2", 13},
    {PM64_PARTNER_KOOPER, "E_3", 21},
    /* Bombette: walking up with the fuse lit, the glow before the blast, thrown up by it, the heap after a
       fizzle, the throws */
    {PM64_PARTNER_BOMBETTE, "R_2", 7},
    {PM64_PARTNER_BOMBETTE, "A_3", 26},
    {PM64_PARTNER_BOMBETTE, "J_1A", 11},
    {PM64_PARTNER_BOMBETTE, "J_1B", 10},
    {PM64_PARTNER_BOMBETTE, "F_1", 30},
    {PM64_PARTNER_BOMBETTE, "A_4A", 14},
    {PM64_PARTNER_BOMBETTE, "A_4B", 16},
    {PM64_PARTNER_BOMBETTE, "R_3", 11},
    /* Parakarry on Yoshi: the climb, the dive, carrying, the shell */
    {PM64_PARTNER_PARAKARRY, "J_1A", 20},
    {PM64_PARTNER_PARAKARRY, "J_1B", 5},
    {PM64_PARTNER_PARAKARRY, "R_5", 4},
    {PM64_PARTNER_PARAKARRY, "A_1", 6},
    {PM64_PARTNER_PARAKARRY, "A_3", 4},
    {PM64_PARTNER_PARAKARRY, "A_2", 21},
    {PM64_PARTNER_PARAKARRY, "A_4", 4},
    {PM64_PARTNER_PARAKARRY, "A_5", 1},
    {PM64_PARTNER_PARAKARRY, "A_6", 22},
    {PM64_PARTNER_PARAKARRY, "A_7", 22},
    {PM64_PARTNER_PARAKARRY, "A_8", 7},
    {PM64_PARTNER_PARAKARRY, "A_12", 8},
    {PM64_PARTNER_PARAKARRY, "A_10", 4},
    {PM64_PARTNER_PARAKARRY, "A_11", 4},
    {PM64_PARTNER_PARAKARRY, "F_1", 1},
    /* Lakilester on Flurrie: his four moves are the port's scripts (they set his rows); her own move poses, should
       anything else play them: the throw for Body Slam and Lip Lock, the cloud blowing for Gale Force, the cloud
       hiding for Dodgy Fog */
    {PM64_PARTNER_LAKILESTER, "A1_1", 18},
    {PM64_PARTNER_LAKILESTER, "A1_2", 18},
    {PM64_PARTNER_LAKILESTER, "A2_1", 22},
    {PM64_PARTNER_LAKILESTER, "A2_2", 22},
    {PM64_PARTNER_LAKILESTER, "A2_3", 22},
    {PM64_PARTNER_LAKILESTER, "A3_1", 18},
    {PM64_PARTNER_LAKILESTER, "A3_2", 18},
    {PM64_PARTNER_LAKILESTER, "A3_3", 18},
    {PM64_PARTNER_LAKILESTER, "A4_2", 24},
    {PM64_PARTNER_LAKILESTER, "A4_3", 24},
    /* Watt on Ms. Mowz: charging, the burst, pleased, let down, the spins */
    {PM64_PARTNER_WATT, "A1_1", 6},
    {PM64_PARTNER_WATT, "A1_2", 7},
    {PM64_PARTNER_WATT, "A1_3", 8},
    {PM64_PARTNER_WATT, "A2_4", 12},
    {PM64_PARTNER_WATT, "I_1", 6},
    {PM64_PARTNER_WATT, "A4_1", 6},
    {PM64_PARTNER_WATT, "A3_1", 6},
    {PM64_PARTNER_WATT, "A3_2", 6},
    {PM64_PARTNER_WATT, "A3_3", 6},
    {PM64_PARTNER_WATT, "B_1", 1},
    {PM64_PARTNER_WATT, "K_1", 1},
    {PM64_PARTNER_WATT, "O_3", 8},
    /* Sushie on Vivian: the breath and the squirt, the call */
    {PM64_PARTNER_SUSHIE, "A1_1", 9},
    {PM64_PARTNER_SUSHIE, "A1_2", 10},
    {PM64_PARTNER_SUSHIE, "A2_1", 2},
    {PM64_PARTNER_SUSHIE, "A2_2", 12},
    {PM64_PARTNER_SUSHIE, "A2_3", 12},
    {PM64_PARTNER_SUSHIE, "A3_1", 12},
};

/* a battle pose's state class by its letter (rest: the name after the prefix) */
static int battleRowClass(char c, const char* rest) {
    switch (c) {
    case 'W':
        return B_WALK;
    case 'R':
        return B_RUN;
    case 'J':
        return B_JUMP;
    case 'F':
    case 'K':
        return B_FALL;
    case 'T':
        return B_TALK;
    case 'D':
        return rest[1] == '_' && rest[2] == '2'                      ? B_LOW
               : rest[1] == '_' && rest[2] == '3'                    ? B_KO
               : rest[1] == '_' && (rest[2] == '4' || rest[2] == '5') ? B_STILL
                                                                      : B_HURT;
    case 'Y':
    case 'H':
        return B_GLAD;
    case 'A':
        return B_MOVE;
    case 'N':
        return B_SLEEP;
    case 'X':
        return B_DIZZY;
    case 'G':
        return B_GUARD;
    case 'O':
        return B_APPEAL;
    case 'I':
        return B_ITEM;
    default:
        return B_IDLE;
    }
}

/* a port script's own row for a unit (pm64SetAnim): the anim, and an overlay row drawn over it (-1 none) */
static struct {
    const BattleUnit* unit;
    signed char anim, overlay;
    float ox, oy, oz; /* the overlay's place from the unit's (a thrown thing in flight) */
    int owner;        /* the event the row belongs to (pm64Owner): it is dropped once that event is gone */
} sForced[4];

/* a pose drawn as a PM64 partner, each with its animation playing */
#define PLAYS 16 /* (the field partner, the Party tab's, the battle's) */
static struct {
    int pose1; /* the pose + 1 */
    int sprite;
    int anim;
    unsigned gen;
    unsigned frame; /* gp->retraceCount it was stepped at */
    Pm64Anim a;
    /* battle */
    char name[24];      /* the host pose it last drew */
    int restart;        /* a new host pose: one-shot rows start again */
    unsigned since;     /* gp->retraceCount the host pose began */
    float lastX, lastY; /* the unit's place last frame */
    float lastRot;      /* its script's Y rotation last frame */
    unsigned rotStill;  /* frames that has stayed put */
    float faceSpin;     /* the facing that rotation gave while it stood still */
    int overlay;        /* the overlay row playing, -1 none */
    Pm64Anim over;
} sPlay[PLAYS];

/* p's face: his idle frame as it starts (its parts at their places, the later ones in front), the top square
 * of it (the whole of one wider than tall, centred), mirrored as the field draws the sprites, scaled to
 * FACE_MAX square with TTYD's sticker edge, into sFaceRGBA (straight RGBA, 0..255, top row first). hud: placed
 * as the HUD's faces sit (low and to the left, the HP heart over the top right), else centred. 0: none. */
static float sFaceRGBA[FACE_MAX * FACE_MAX * 4];
static int faceRGBA(int p, int hud) {
    enum { CANVAS = 192, BOX = 40 /* the head's size in the icon: as TTYD's partners' (Koops' as Mario's) */, SUB = 4 };
    static uint8_t canvas[CANVAS * CANVAS * 4], pixels[CANVAS * CANVAS * 4];
    static float face[FACE_MAX * FACE_MAX * 4]; /* premultiplied */
    float* out4 = sFaceRGBA;
    static float edge[FACE_MAX * FACE_MAX], shadow[FACE_MAX * FACE_MAX];
    static Pm64Anim a;
    const Pm64Sprite* s = portPm64NpcSprite(kWho[p].sprite);
    int i, x0 = 1 << 20, x1 = -(1 << 20), y0 = 1 << 20, y1 = -(1 << 20), cw, ch, uh, hx0, hx1, ox, oy;
    float scale, left, top;
    if (s == NULL) {
        return 0;
    }
    portPm64AnimStart(&a, s, kAnims[p][0][A_IDLE]);
    for (i = 0; i < a.count; i++) {
        int w, h, l, bottom;
        if (a.part[i].raster < 0 || !portPm64SpriteRaster(s, a.part[i].raster, &w, &h, NULL) || w <= 0 || h <= 0) {
            continue;
        }
        l = (int)floorf(a.part[i].x) - w / 2;
        bottom = (int)floorf(a.part[i].y);
        x0 = l < x0 ? l : x0;
        x1 = l + w > x1 ? l + w : x1;
        y0 = bottom < y0 ? bottom : y0;
        y1 = bottom + h > y1 ? bottom + h : y1;
    }
    cw = x1 - x0;
    ch = y1 - y0;
    if (cw <= 0 || ch <= 0 || cw > CANVAS || ch > CANVAS) {
        return 0;
    }
    memset(canvas, 0, (size_t)cw * ch * 4);
    for (i = 0; i < a.count; i++) {
        int w, h, l, t, x, y;
        if (a.part[i].raster < 0 || !portPm64SpriteRaster(s, a.part[i].raster, &w, &h, NULL) || w <= 0 || h <= 0 ||
            w * h > CANVAS * CANVAS || !portPm64SpriteRGBA(s, a.part[i].raster, a.part[i].palette, pixels)) {
            continue;
        }
        l = (int)floorf(a.part[i].x) - w / 2 - x0;
        t = y1 - ((int)floorf(a.part[i].y) + h); /* (the canvas top down; a raster's first row is its top) */
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                const uint8_t* px = pixels + ((size_t)y * w + x) * 4;
                if (px[3] != 0) {
                    memcpy(canvas + ((size_t)(t + y) * cw + l + x) * 4, px, 4);
                }
            }
        }
    }
    /* The head, framed as TTYD's partner icons frame theirs: a round partner (Goombario, Bombette, Watt)
       whole, a tall one from the top down to a little past a square; the size of Mario's head beside it,
       centred, smoothly resampled (filtered, as the game's own icons are, not the sprite's hard pixels), and
       mirrored as the field draws the sprites. */
    uh = ch * 86 / 100; /* (a round one's feet below the icon's edge, as Goombella's chin) */
    hx0 = 0;
    hx1 = cw;
    if (ch > cw * 5 / 4) {
        /* a tall one (Kooper, Parakarry, Lakilester): his head, the top half, as wide as it is there */
        int x, y;
        uh = ch / 2;
        hx0 = cw;
        hx1 = 0;
        for (y = 0; y < uh; y++) {
            for (x = 0; x < cw; x++) {
                if (canvas[((size_t)y * cw + x) * 4 + 3] != 0) {
                    hx0 = x < hx0 ? x : hx0;
                    hx1 = x + 1 > hx1 ? x + 1 : hx1;
                }
            }
        }
        if (hx1 <= hx0) {
            hx0 = 0;
            hx1 = cw;
        }
    }
    scale = (float)BOX / (float)(hx1 - hx0 > uh ? hx1 - hx0 : uh);
    /* (low and to the left, as the game's faces sit, the HP heart over the top right) */
    left = ((float)FACE_MAX - (float)(hx1 - hx0) * scale) * 0.5f - (hud ? 4.0f : 0.0f) - (float)(cw - hx1) * scale;
    top = ((float)FACE_MAX - (float)uh * scale) * 0.5f + (hud ? 5.0f : 0.0f);
    memset(face, 0, sizeof(face));
    for (oy = 0; oy < FACE_MAX; oy++) {
        for (ox = 0; ox < FACE_MAX; ox++) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int sx, sy, k;
            for (sy = 0; sy < SUB; sy++) {
                for (sx = 0; sx < SUB; sx++) {
                    /* bilinear on the canvas at this sub-sample, premultiplied */
                    const float fx = (float)cw - ((float)ox + ((float)sx + 0.5f) / SUB - left) / scale - 0.5f;
                    const float fy = ((float)oy + ((float)sy + 0.5f) / SUB - top) / scale - 0.5f;
                    const int ix = (int)floorf(fx), iy = (int)floorf(fy);
                    const float tx = fx - (float)ix, ty = fy - (float)iy;
                    int dx, dy;
                    for (dy = 0; dy < 2; dy++) {
                        for (dx = 0; dx < 2; dx++) {
                            const int cx = ix + dx, cy = iy + dy;
                            const float wgt = (dx ? tx : 1.0f - tx) * (dy ? ty : 1.0f - ty);
                            const uint8_t* px;
                            float al;
                            if (cx < 0 || cy < 0 || cx >= cw || cy >= uh) {
                                continue;
                            }
                            px = canvas + ((size_t)cy * cw + cx) * 4;
                            al = (float)px[3] / 255.0f * wgt;
                            acc[0] += (float)px[0] * al;
                            acc[1] += (float)px[1] * al;
                            acc[2] += (float)px[2] * al;
                            acc[3] += al;
                        }
                    }
                }
            }
            for (k = 0; k < 4; k++) {
                face[((size_t)oy * FACE_MAX + ox) * 4 + k] = acc[k] / (float)(SUB * SUB);
            }
        }
    }
    /* TTYD's sticker edge around it: a white band and a thin grey line outside that, both soft, over a soft
       shadow down and to the right; the colours a touch lighter, as the game's printed-paper icons */
    for (oy = 0; oy < FACE_MAX; oy++) {
        for (ox = 0; ox < FACE_MAX; ox++) {
            float d2 = 1e9f;
            int dx, dy;
            for (dy = -FACE_OUTLINE; dy <= FACE_OUTLINE; dy++) {
                for (dx = -FACE_OUTLINE; dx <= FACE_OUTLINE; dx++) {
                    const int x = ox + dx, y = oy + dy;
                    if (x < 0 || y < 0 || x >= FACE_MAX || y >= FACE_MAX ||
                        face[((size_t)y * FACE_MAX + x) * 4 + 3] < 0.5f) {
                        continue;
                    }
                    if ((float)(dx * dx + dy * dy) < d2) {
                        d2 = (float)(dx * dx + dy * dy);
                    }
                }
            }
            edge[oy * FACE_MAX + ox] = sqrtf(d2);
        }
    }
    for (oy = 0; oy < FACE_MAX; oy++) {
        for (ox = 0; ox < FACE_MAX; ox++) {
            /* the shadow: the edged shape's coverage, blurred, 2 texels down and right */
            float sum = 0.0f;
            int dx, dy;
            for (dy = -2; dy <= 2; dy++) {
                for (dx = -2; dx <= 2; dx++) {
                    const int x = ox + dx - 2, y = oy + dy - 2;
                    if (x >= 0 && y >= 0 && x < FACE_MAX && y < FACE_MAX) {
                        const float d = edge[y * FACE_MAX + x];
                        sum += d <= 3.4f ? 1.0f : d >= 4.4f ? 0.0f : 4.4f - d;
                    }
                }
            }
            shadow[oy * FACE_MAX + ox] = 0.30f * sum / 25.0f;
        }
    }
    for (oy = 0; oy < FACE_MAX; oy++) {
        for (ox = 0; ox < FACE_MAX; ox++) {
            const float* f = face + ((size_t)oy * FACE_MAX + ox) * 4;
            float* o = out4 + ((size_t)oy * FACE_MAX + ox) * 4;
            const float d = edge[oy * FACE_MAX + ox];
            /* the band: white to 2.2 texels, grey to 3.4, soft to 4.4 */
            const float white = d <= 2.2f ? 1.0f : d >= 2.8f ? 0.0f : (2.8f - d) / 0.6f;
            const float bandA = d <= 3.4f ? 1.0f : d >= 4.4f ? 0.0f : 4.4f - d;
            const float band = 96.0f + 159.0f * white;
            const float sh = shadow[oy * FACE_MAX + ox];
            float r, g, b, al;
            /* the shadow, the band over it, the face over that (premultiplied "over") */
            r = 40.0f * sh;
            g = 30.0f * sh;
            b = 30.0f * sh;
            al = sh;
            r = band * bandA + r * (1.0f - bandA);
            g = band * bandA + g * (1.0f - bandA);
            b = band * bandA + b * (1.0f - bandA);
            al = bandA + al * (1.0f - bandA);
            r = (f[0] * 0.94f + 255.0f * 0.06f * f[3]) + r * (1.0f - f[3]);
            g = (f[1] * 0.94f + 255.0f * 0.06f * f[3]) + g * (1.0f - f[3]);
            b = (f[2] * 0.94f + 255.0f * 0.06f * f[3]) + b * (1.0f - f[3]);
            al = f[3] + al * (1.0f - f[3]);
            o[3] = al;
            o[0] = al > 0.001f ? r / al : 0.0f;
            o[1] = al > 0.001f ? g / al : 0.0f;
            o[2] = al > 0.001f ? b / al : 0.0f;
        }
    }
    if (hud) {
        heartUnder(out4); /* the HUD's only (the launcher's cast shows the face alone) */
    }
    return 1;
}

/* p's face for the HUD (faceRGBA's, placed as the HUD's) as GX RGBA8: FACE_MAX, or 0: none */
/* sFaceRGBA as a GX RGBA8 texture (FACE_MAX square, 4x4 tiles: AR then GB) */
static void tileFace(uint8_t* out) {
    int ox, oy;
    memset(out, 0, (size_t)FACE_MAX * FACE_MAX * 4);
    for (oy = 0; oy < FACE_MAX; oy++) {
        for (ox = 0; ox < FACE_MAX; ox++) {
            const int k = (oy & 3) * 4 + (ox & 3);
            uint8_t* tile = out + ((size_t)(oy >> 2) * (FACE_MAX / 4) + (ox >> 2)) * 64;
            const float* px = sFaceRGBA + ((size_t)oy * FACE_MAX + ox) * 4;
            uint8_t v[4];
            int c;
            for (c = 0; c < 4; c++) {
                const float f = c == 3 ? px[3] * 255.0f : px[c];
                v[c] = (uint8_t)(f < 0.0f ? 0.0f : f > 255.0f ? 255.0f : f + 0.5f);
            }
            tile[k * 2] = v[3];
            tile[k * 2 + 1] = v[0];
            tile[32 + k * 2] = v[1];
            tile[32 + k * 2 + 1] = v[2];
        }
    }
}

static int buildFace(int p) {
    if (!faceRGBA(p, 1)) {
        return 0;
    }
    tileFace(sFace[p]);
    return FACE_MAX;
}

/* The players 2-4 cards' Character strip (player_menu_pc.c): p's face centred, without the HUD's heart */
static unsigned char sMenuFace[PM64_PARTNER_COUNT][FACE_MAX * FACE_MAX * 4];
static unsigned sMenuFaceGen[PM64_PARTNER_COUNT];
static int sMenuFaceOk[PM64_PARTNER_COUNT];

const void* portPm64MenuFace(int p, int* size) {
    if (p <= 0 || p >= PM64_PARTNER_COUNT || gp == NULL || portPm64Linked() == PM64_NONE) {
        return NULL;
    }
    if (sMenuFaceGen[p] != portPm64Generation() + 1) {
        sMenuFaceOk[p] = faceRGBA(p, 0);
        if (sMenuFaceOk[p]) {
            tileFace(sMenuFace[p]);
        }
        sMenuFaceGen[p] = portPm64Generation() + 1;
    }
    if (!sMenuFaceOk[p]) {
        return NULL;
    }
    *size = FACE_MAX;
    return sMenuFace[p];
}

/* The launcher's cast: every partner with a row in kWho, in their move sets' order (PM64's own) */
_Static_assert(FACE_MAX <= PM64_CAST_FACE_MAX, "the launcher's faces fit its buffer");
int portPm64CastCount(void) {
    return portPm64Linked() != PM64_NONE ? PM64_PARTNER_COUNT - 1 : 0;
}

int portPm64CastFace(int i, uint8_t* rgba, int* w, int* h) {
    int order[PM64_PARTNER_COUNT], n = 0, p, k, x, y, x0 = FACE_MAX, x1 = 0, y0 = FACE_MAX, y1 = 0;
    if (i < 0 || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    for (p = 1; p < PM64_PARTNER_COUNT; p++) {
        for (k = n; k > 0 && kWho[order[k - 1]].order > kWho[p].order; k--) {
            order[k] = order[k - 1];
        }
        order[k] = p;
        n++;
    }
    if (i >= n || !faceRGBA(order[i], 0)) {
        return 0;
    }
    /* cropped to the sticker (its shadow's faint fringe left out), as the game's own faces fill theirs */
    for (y = 0; y < FACE_MAX; y++) {
        for (x = 0; x < FACE_MAX; x++) {
            if (sFaceRGBA[((size_t)y * FACE_MAX + x) * 4 + 3] > 0.08f) {
                x0 = x < x0 ? x : x0;
                x1 = x + 1 > x1 ? x + 1 : x1;
                y0 = y < y0 ? y : y0;
                y1 = y + 1 > y1 ? y + 1 : y1;
            }
        }
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const float* px = sFaceRGBA + ((size_t)y * FACE_MAX + x) * 4;
            uint8_t* o = rgba + ((size_t)(y - y0) * (size_t)(x1 - x0) + (size_t)(x - x0)) * 4;
            int c;
            for (c = 0; c < 4; c++) {
                const float f = c == 3 ? px[3] * 255.0f : px[c];
                o[c] = (uint8_t)(f < 0.0f ? 0.0f : f > 255.0f ? 255.0f : f + 0.5f);
            }
        }
    }
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

static int slotOf(int poseId) {
    int i, free = -1, oldest = 0;
    for (i = 0; i < PLAYS; i++) {
        if (sPlay[i].pose1 == poseId + 1) {
            return i;
        }
        if (sPlay[i].pose1 == 0 && free < 0) {
            free = i;
        }
        if (sPlay[i].frame < sPlay[oldest].frame) {
            oldest = i;
        }
    }
    i = free >= 0 ? free : oldest;
    sPlay[i].pose1 = poseId + 1;
    sPlay[i].sprite = -1;
    sPlay[i].name[0] = '\0';
    sPlay[i].overlay = -1;
    sPlay[i].rotStill = 0;
    sPlay[i].faceSpin = 1.0f;
    return i;
}

static Pm64Anim* playOf(int poseId, int sprite, int anim) {
    const int i = slotOf(poseId);
    if (sPlay[i].sprite != sprite || sPlay[i].anim != anim || sPlay[i].gen != portPm64Generation() ||
        sPlay[i].restart) {
        sPlay[i].restart = 0;
        sPlay[i].sprite = sprite;
        sPlay[i].anim = anim;
        sPlay[i].gen = portPm64Generation();
        sPlay[i].frame = gp->retraceCount;
        portPm64AnimStart(&sPlay[i].a, portPm64NpcSprite(sprite), anim);
    } else if (sPlay[i].frame != gp->retraceCount) {
        portPm64AnimStep(&sPlay[i].a, (float)(gp->retraceCount - sPlay[i].frame > 4 ? 4 : gp->retraceCount - sPlay[i].frame));
        sPlay[i].frame = gp->retraceCount;
    }
    return &sPlay[i].a;
}

static float sSeeThrough = 1.0f; /* the draw's alpha besides the pose's (Bow over Mario in the field) */

/* m: the model matrix (half-size model units: a sprite pixel is half a unit); unblacken: the pose's colour
 * leaves out its blackening (a fire hit drawn with PM64's own dark rows) */
static void drawSpriteTint(const Pm64Anim* a, Mtx m, const AnimationPose* pose, int unblacken) {
    CameraEntry* cam = camGetCurPtr();
    Mtx mv, nrm;
    Vec at = {0.0f, 0.0f, 0.0f};
    GXColor tint = {0xFF, 0xFF, 0xFF, 0xFF};
    int i;
    if (cam == NULL || a->sprite == NULL) {
        return;
    }
    if ((pose->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) &&
        !(unblacken && pose->materialColor.r == 0 && pose->materialColor.g == 0 && pose->materialColor.b == 0)) {
        tint = pose->materialColor;
    }
    if (pose->materialColor2.r != 0xFF) {
        tint.a = (u8)(tint.a * pose->materialColor2.r / 255); /* a fade */
    }
    if (sSeeThrough < 1.0f) {
        tint.a = (u8)((float)tint.a * sSeeThrough); /* (Bow's Outta Sight) */
    }
    /* the map's lights where it stands, as on the partners' own models */
    PSMTXMultVec(m, &at, &at);
    mapSetMaterialLight(pose->materialLightFlag | ANIM_MATERIAL_LIGHT_NO_VTX_COLOR, at);
    PSMTXConcat(cam->view, m, mv);
    GXLoadPosMtxImm(mv, GX_PNMTX0);
    PSMTXInvXpose(mv, nrm);
    GXLoadNrmMtxImm(nrm, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(2);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevDirect(GX_TEVSTAGE1);
    GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevKColor(GX_KCOLOR0, tint);
    GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
    GXSetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K0_A);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_KONST, GX_CA_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetAlphaCompare(GX_GREATER, 8, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZCompLoc(GX_FALSE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, tint.a == 0xFF ? GX_TRUE : GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetCullMode(GX_CULL_NONE);
    for (i = 0; i < a->count; i++) {
        const Pm64Part* p = &a->part[i];
        GXTexObj tex;
        const void* data;
        int w, h, tw, th;
        float x0, x1, y0, y1, z, u, v;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) ||
            (data = portPm64RasterTexture(a->sprite, p->raster, p->palette, &tw, &th)) == NULL) {
            continue;
        }
        GXInitTexObj(&tex, (void*)data, (u16)tw, (u16)th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GXInitTexObjLOD(&tex, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        GXLoadTexObj(&tex, GX_TEXMAP0);
        /* PM64's quad: -w/2..w/2 across, 0..h up, about the part's point, turned
           by the part's own rotation (degrees: x, then y, then z, as PM64's
           sprite draw); half units */
        x0 = -(float)(w / 2) * p->sx;
        x1 = (float)(w - w / 2) * p->sx;
        y0 = 0.0f;
        y1 = (float)h * p->sy;
        z = p->z * 0.1f + (float)i * 0.04f; /* (the later parts in front; halved below) */
        u = (float)w / (float)tw;
        v = (float)h / (float)th;
        {
            const float cx[4] = {x0, x1, x1, x0}, cy[4] = {y1, y1, y0, y0};
            const float su[4] = {0.0f, u, u, 0.0f}, sv[4] = {0.0f, 0.0f, v, v};
            const float ax = 0.017453292f * p->rx, ay = 0.017453292f * p->ry, az = 0.017453292f * p->rz;
            const float sa = sinf(ax), ca = cosf(ax), sb = sinf(ay), cb = cosf(ay), sc = sinf(az), cc = cosf(az);
            int k;
            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
            for (k = 0; k < 4; k++) {
                float px = cx[k], py = cy[k], pz = 0.0f, t;
                t = py * ca - pz * sa; /* about x */
                pz = py * sa + pz * ca;
                py = t;
                t = px * cb + pz * sb; /* about y */
                pz = -px * sb + pz * cb;
                px = t;
                t = px * cc - py * sc; /* about z */
                py = px * sc + py * cc;
                px = t;
                GXPosition3f32((p->x + px) * 0.5f, (p->y + py) * 0.5f, (pz + z) * 0.5f);
                GXNormal3f32(0.0f, 0.0f, 1.0f);
                GXTexCoord2f32(su[k], sv[k]);
            }
            GXEnd();
        }
    }
}

/* Cool Mode (port/include/port/toggles.h): each partner's sunglasses on its sprite, as shares of the
 * frame's box (PM64's sprites face left, before the field's mirror): their middle across and up, and
 * their width. Set by eye from the captures. */
static const float kCoolAt[PM64_PARTNER_COUNT][3] = {
    {0.0f, 0.0f, 0.0f},
    {0.40f, 0.55f, 0.55f}, /* Goombario */
    {0.32f, 0.77f, 0.45f}, /* Kooper: on the eyes at the top of his face, not above them */
    {0.40f, 0.54f, 0.38f}, /* Bombette */
    {0.30f, 0.70f, 0.38f}, /* Parakarry: off the beak */
    {0.45f, 0.54f, 0.42f}, /* Watt */
    {0.25f, 0.62f, 0.35f}, /* Sushie */
    {0.35f, 0.66f, 0.40f}, /* Lakilester: off the forehead */
    {0.40f, 0.60f, 0.45f}, /* Bow */
};

/* the glasses on partner p's sprite as drawSprite drew it with m (its frame's box, half units) */
static void coolSprite(const Pm64Anim* a, Mtx m, const AnimationPose* pose, int p) {
    GXColor tint = {0xFF, 0xFF, 0xFF, 0xFF};
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int i;
    if (p <= 0 || p >= PM64_PARTNER_COUNT || a->sprite == NULL || !portFunOn(PORT_FUN_COOL)) {
        return;
    }
    for (i = 0; i < a->count; i++) {
        const Pm64Part* pt = &a->part[i];
        int w, h;
        if (pt->raster < 0 || !portPm64SpriteRaster(a->sprite, pt->raster, &w, &h, NULL) || w <= 0 || h <= 0) {
            continue;
        }
        x0 = fminf(x0, (pt->x - (float)(w / 2) * pt->sx) * 0.5f);
        x1 = fmaxf(x1, (pt->x + (float)(w - w / 2) * pt->sx) * 0.5f);
        y0 = fminf(y0, pt->y * 0.5f);
        y1 = fmaxf(y1, (pt->y + (float)h * pt->sy) * 0.5f);
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    if (pose->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) {
        tint = pose->materialColor;
    }
    if (pose->materialColor2.r != 0xFF) {
        tint.a = (u8)(tint.a * pose->materialColor2.r / 255);
    }
    tint.a = (u8)((float)tint.a * sSeeThrough); /* fading with Bow's Outta Sight */
    portFunCoolSprite(m, x0, x1, y0, y1, kCoolAt[p], &tint);
}

static void drawSprite(const Pm64Anim* a, Mtx m, const AnimationPose* pose) {
    drawSpriteTint(a, m, pose, 0);
}

static int drawAs(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale, int as);

/* the battle's partner unit (Mario's side, not Mario) one of whose parts is poseId, and that part */
static const BattleUnit* partyUnitOfPose(int poseId, const BattleUnitPart** partOut) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (wp == NULL || gp == NULL || !gp->inBattle) {
        return NULL;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* unit = BattleGetUnitPtr(wp, i);
        const BattleUnitPart* part;
        if (unit == NULL || unit->alliance != 0 || unit->currentKind == UNIT_MARIO) {
            continue;
        }
        for (part = unit->parts; part != NULL; part = part->nextPart) {
            if (part->poseId == poseId) {
                *partOut = part;
                return unit;
            }
        }
    }
    return NULL;
}

int portPm64DrawPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale) {
    return drawAs(poseId, mtx, xluMode, rotY, scale, 0);
}

int portPm64DrawPoseAs(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale, int p) {
    return p > 0 && p < PM64_PARTNER_COUNT && drawAs(poseId, mtx, xluMode, rotY, scale, p);
}

/* the battle unit (any side) one of whose parts is poseId */
static const BattleUnit* unitOfPose(int poseId) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (wp == NULL || gp == NULL || !gp->inBattle) {
        return NULL;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* unit = BattleGetUnitPtr(wp, i);
        const BattleUnitPart* part;
        for (part = unit != NULL ? unit->parts : NULL; part != NULL; part = part->nextPart) {
            if (part->poseId == poseId) {
                return unit;
            }
        }
    }
    return NULL;
}

static int sForcedOverlay;      /* (battleAnim's out: the forced overlay row, -1 none, */
static float sForcedOff[3];     /*  and where it is from the unit) */

/* The battle row for partner p's unit in host pose name (slot: its sPlay), and whether the host's fire
 * blackening is to be left off (the burnt rows are dark already). */
static int battleAnim(int p, int slot, const BattleUnit* unit, const BattleUnitPart* part, const char* name,
                      int* unblacken) {
    const char* u = name != NULL ? strchr(name, '_') : NULL;
    const char* rest = name == NULL ? "S_1" : u != NULL && u - name > 1 ? u + 1 : name;
    const signed char* rows = kBattleRows[p];
    const float dy = unit->position.y - sPlay[slot].lastY, dx = unit->position.x - sPlay[slot].lastX;
    BattleWork* wp = _battleWorkPointer;
    int i, cls, anim = -1;
    *unblacken = 0;
    sForcedOverlay = -1;
    sForcedOff[0] = sForcedOff[1] = sForcedOff[2] = 0.0f;
    /* a new host pose: one-shot rows start again */
    if (strncmp(sPlay[slot].name, name != NULL ? name : "", sizeof(sPlay[slot].name) - 1) != 0) {
        snprintf(sPlay[slot].name, sizeof(sPlay[slot].name), "%s", name != NULL ? name : "");
        sPlay[slot].restart = 1;
        sPlay[slot].since = gp->retraceCount;
    }
    for (i = 0; i < (int)(sizeof(sForced) / sizeof(sForced[0])); i++) {
        if (sForced[i].unit == unit && sForced[i].anim >= 0) {
            sForcedOverlay = sForced[i].overlay;
            sForcedOff[0] = sForced[i].ox;
            sForcedOff[1] = sForced[i].oy;
            sForcedOff[2] = sForced[i].oz;
            return sForced[i].anim;
        }
    }
    if (part->animFlags & PART_ANIM_BLACKEN) {
        /* a fire hit: the dark hurt row, then (after a third of a second) the dark one standing */
        *unblacken = 1;
        return gp->retraceCount - sPlay[slot].since < 20 ? rows[B_BURNHURT] : rows[B_BURNT];
    }
    for (i = 0; i < (int)(sizeof(kBattlePose) / sizeof(kBattlePose[0])); i++) {
        if (kBattlePose[i].p == p && !strcmp(kBattlePose[i].pose, rest)) {
            anim = kBattlePose[i].anim;
            break;
        }
    }
    if (anim == K_KOOPER_SHELL) {
        /* K_2 then K_1 going in, K_1 then K_2 coming out: in from standing, out from the shell */
        const int was = sPlay[slot].anim;
        if (was == 14 || was == 16 || was == 15) {
            sPlay[slot].restart = 0; /* (the second half of the same turn) */
            return was;
        }
        return was >= 9 && was <= 13 ? 15 : 14;
    }
    if (anim == K_ASCEND) {
        /* rising, then coming down: onto the enemy (towards where it faces, or straight down) the bonk */
        sPlay[slot].restart = 0;
        if (dy > 0.05f) {
            return rows[B_JUMP];
        }
        return dx * (float)unit->faceDirection >= -0.05f ? 6 : rows[B_FALL];
    }
    if (anim >= 0) {
        return anim;
    }
    cls = battleRowClass(rest[0], rest);
    if (cls == B_KO && unit->currentHp > 0) {
        cls = B_HURT; /* (D_3 out of a move, not knocked out) */
    }
    if (cls == B_HURT && (unit->statusFlags & STATUS_FLAG_FLIPPED) && p == PM64_PARTNER_KOOPER) {
        return 25;
    }
    if (cls == B_GLAD) {
        if (wp != NULL && BattleGetSeq(wp, SEQ_END) != 0) {
            cls = B_VICTORY;
        } else if (p == PM64_PARTNER_BOMBETTE && wp != NULL && wp->actionCommands.acUnit == unit &&
                   wp->actionCommands.acState > 0 && wp->actionCommands.acState < 1000) {
            return 14; /* charging up while the command runs (Power Bomb, Mega Bomb) */
        }
    }
    if (cls == B_JUMP) {
        sPlay[slot].restart = 0;
        cls = dy < -0.05f ? B_FALL : B_JUMP;
    }
    return rows[cls];
}

/* How tall (pixels, feet to top, the opaque ones) animation anim of sprite s stands at its first frame; 0 if
 * it can't be read. Rows from the top: row r of a part lies at y p->y + h - 1 - r .. p->y + h - r. */
static float spriteHeight(const Pm64Sprite* s, int anim) {
    static Pm64Anim a;
    float top = -1e9f, bottom = 1e9f;
    int i;
    if (s == NULL || anim < 0 || anim >= portPm64SpriteAnimCount(s)) {
        return 0.0f;
    }
    portPm64AnimStart(&a, s, anim);
    for (i = 0; i < a.count; i++) {
        const Pm64Part* p = &a.part[i];
        uint8_t* px;
        int w, h, x, y, first = -1, last = -1;
        if (p->raster < 0 || !portPm64SpriteRaster(s, p->raster, &w, &h, NULL) || w <= 0 || h <= 0 ||
            w * h > 256 * 256 || (px = (uint8_t*)malloc((size_t)w * h * 4)) == NULL) {
            continue;
        }
        if (portPm64SpriteRGBA(s, p->raster, p->palette, px)) {
            for (y = 0; y < h; y++) {
                for (x = 0; x < w && px[((size_t)y * w + x) * 4 + 3] == 0; x++) {
                }
                if (x < w) {
                    if (first < 0) {
                        first = y;
                    }
                    last = y;
                }
            }
        }
        free(px);
        if (first >= 0) {
            const float t = p->y + (float)(h - first) * p->sy, b = p->y + (float)(h - 1 - last) * p->sy;
            top = t > top ? t : top;
            bottom = b < bottom ? b : bottom;
        }
    }
    return top > bottom ? top - bottom : 0.0f;
}

/* The scale on the partners' sprites (drawn at half a unit a pixel) that makes every PM64 partner stand beside
 * TTYD Mario as it stood beside PM64's Mario: TTYD Mario's height / PM64 Mario's drawn height. TTYD Mario: his
 * model a_mario stands 21.8 units over his feet (its bounding box: -1.66 .. 21.8, the bottom under the floor);
 * PM64's Mario: the player sprite Mario1's Idle, measured from the linked ROM (about 54 pixels; PM64 draws
 * players and NPCs alike, at 5/7 a unit a pixel, field and battle). One factor for all, so partner / Mario is
 * PM64's own ratio for each (the table, logged once a link: "[pm64] sizes"; local MATCH.md). Before the ROM's
 * Mario can be read: 0.81 (measured on screen beside him, 78 / 97 pixels). */
#define TTYD_MARIO_HEIGHT 21.8f
static float pm64Size(void) {
    static unsigned sGenSeen;
    static float sScale = 0.81f;
    if (sGenSeen != portPm64Generation() + 1) {
        const float mario = spriteHeight(portPm64PlayerSprite(0), portPm64MarioAnim("M_S_1"));
        int p;
        sGenSeen = portPm64Generation() + 1;
        if (mario < 20.0f) {
            sScale = 0.81f;
            return sScale;
        }
        sScale = TTYD_MARIO_HEIGHT / (0.5f * mario);
        if (sScale < 0.6f || sScale > 1.0f) {
            sScale = sScale < 0.6f ? 0.6f : 1.0f; /* (a ROM read wrong: never far from what was measured) */
        }
        printf("[pm64] sizes: Mario %.0f px, TTYD Mario %.1f units, scale %.3f\n", mario, TTYD_MARIO_HEIGHT, sScale);
        for (p = 1; p < PM64_PARTNER_COUNT; p++) {
            const float f = spriteHeight(portPm64NpcSprite(kWho[p].sprite), kAnims[p][0][A_IDLE]);
            const float b = spriteHeight(portPm64NpcSprite(kWho[p].sprite + 8), kAnims[p][1][A_IDLE]);
            printf("[pm64] sizes: %-10s field %3.0f px (%.2f of Mario, %.1f units)  battle %3.0f px (%.2f, %.1f units)\n",
                   kWho[p].english, f, f / mario, 0.5f * f * sScale, b, b / mario, 0.5f * b * sScale);
        }
        fflush(stdout);
    }
    return sScale;
}

float portPm64Size(void) {
    return pm64Size();
}

float portPm64NpcSpriteHeight(int sprite, int anim) {
    return spriteHeight(portPm64NpcSprite(sprite), anim);
}

float portPm64MarioHeightPx(void) {
    return spriteHeight(portPm64PlayerSprite(0), portPm64MarioAnim("M_S_1"));
}

/* as: the partner to draw it as (another player's ghost), 0 this game's own */
/* tests (TTYD_PM64_LOG): the partner out in the field drawn facing away from where it walks across the screen, logged
 * ("[pm64] field facing: ..."; pm64_field_facing): m, its sprite's matrix (the sprite's front is its -x) */
static void facingCheck(int p, Mtx m) {
    static float sLastX;
    static int sRun, sLogged;
    CameraEntry* cam = camGetCurPtr();
    float x, front;
    if (cam == NULL) {
        return;
    }
    /* (its place along the camera's right: the camera follows Mario, so not its place on the screen) */
    x = cam->view[0][0] * m[0][3] + cam->view[0][1] * m[1][3] + cam->view[0][2] * m[2][3];
    front = -(cam->view[0][0] * m[0][0] + cam->view[0][1] * m[1][0] + cam->view[0][2] * m[2][0]);
    /* (walking: 10 frames in a row moving one way across the screen) */
    if (fabsf(x - sLastX) > 0.3f && fabsf(x - sLastX) < 20.0f) {
        const int way = x > sLastX ? 1 : -1;
        sRun = (sRun * way > 0) ? sRun + way : way;
        if ((sRun >= 10 || sRun <= -10) && front * (float)way < 0.0f && sLogged < 4) {
            sLogged++;
            printf("[pm64] field facing: %s faces away from where it walks (%s)\n", kWho[p].english,
                   way > 0 ? "right" : "left");
            fflush(stdout);
        }
    } else {
        sRun = 0;
    }
    sLastX = x;
}

/* tests (TTYD_PM64_LOG): the look Mario's partner in the field is drawn with, each time it changes (p 0 its own) */
static void fieldPartnerLookLog(int poseId, int p) {
    static int sLast = -1;
    if (gp == NULL || gp->inBattle || !partnerPose(poseId) || p == sLast || !pm64Log()) {
        return;
    }
    sLast = p;
    printf("[pm64] the field partner drawn as %s\n", p > 0 && p < PM64_PARTNER_COUNT ? kWho[p].english : "itself");
    fflush(stdout);
}

static int drawAs(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale, int as) {
    AnimationPose* pose;
    AnimationPoseData* data;
    const char* animName = NULL;
    int host, p, look, battle, sprite, anim, ride, slot, unblacken, own;
    Mtx m, t;
    const Pm64Anim* a;
    const BattleUnit* unit;
    const BattleUnitPart* part = NULL;
    float turn, flip;
    if (poseId < 0 || gp == NULL || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    /* PM64's sizes beside Mario (pm64Size): each partner stands as tall beside TTYD Mario as it stood beside
       PM64's Mario. With Mario's PM64 look on, he is PM64's size (half a unit a pixel) and they stay at theirs. */
    if (!portPm64MarioLookOn()) {
        scale *= pm64Size();
    }
    pose = animPoseGetAnimPosePtr(poseId);
    if (pose == NULL || !(pose->flags & ANIM_POSE_FLAG_USED) || pose->typeFlags != 0 ||
        (data = animPoseGetAnimBaseDataPtr(poseId)) == NULL) {
        return 0;
    }
    look = 0;
    if (as > 0) {
        p = as;
    } else if ((host = hostOf(data->fileName)) == 0 || (look = slotLook(poseId, host)) < 0 ||
               (p = look > 0 ? look : portPm64PartnerOn(host)) == 0) {
        fieldPartnerLookLog(poseId, 0);
        return 0;
    }
    if (as == 0) {
        fieldPartnerLookLog(poseId, p);
    }
    /* a battle unit's (a paper pose of one, Goombella's and Bobbery's, has type 0 there: found by its unit) */
    unit = as == 0 ? partyUnitOfPose(poseId, &part) : NULL;
    if (as == 0 && unit == NULL && unitOfPose(poseId) != NULL) {
        return 0; /* an enemy on a host's model (Doopliss as a partner) keeps its own look */
    }
    battle = pose->type == 1 || unit != NULL;
    sprite = kWho[p].sprite + (battle ? 8 : 0);
    if (pose->animId >= 0 && pose->animId < data->animCount) {
        animName = data->anims[pose->animId].name;
    }
    slot = -1;
    unblacken = 0;
    if (unit != NULL) {
        slot = slotOf(poseId);
        anim = battleAnim(p, slot, unit, part, animName, &unblacken);
    } else {
        anim = kAnims[p][battle][moveOf(animName)];
    }
    /* their own field abilities (pm64_field_pc.c): Sushie swimming with Mario
       on her back, Lakilester carrying Mario on his cloud, Parakarry ridden */
    ride = 0;
    own = as == 0 && !battle && partnerPose(poseId); /* (not a co-op player's companion: theirs aren't his) */
    if (own) {
        if (p == PM64_PARTNER_SUSHIE && portPm64Swimming()) {
            ride = 1;
        } else if (p == PM64_PARTNER_LAKILESTER && portPm64Carried()) {
            ride = 1;
        } else if (p == PM64_PARTNER_PARAKARRY && yoshiGetStatus() != 0) {
            ride = 2;
        }
        if (ride) {
            anim = kAnims[p][0][A_MOVE];
        }
    }

    /* the host isn't drawn (a pose drawn with disableDraw still draws its shapes, in the last material set:
       another model's textures on it); its paper turn steps once a frame (the game waits on it:
       animPoseGetPeraEnd), and the sprite is drawn in the opaque stage; no paper shadow of it */
    if (xluMode != 1) {
        return 1;
    }
    turn = portAnimPoseStepTurn(poseId, rotY, &flip);
    if (portPaperShadowPass()) {
        return 1;
    }
    a = playOf(poseId, sprite, anim);
    PSMTXCopy(mtx, m);
    if (unit != NULL) {
        /* In battle the sprite faces where the unit does: its faceDirection (+1 towards +x, the enemies),
           turned around by the unit's own script rotation (a partner running home turned 180). The paper
           turn and the hosts' models' own facing (their base rotations, some poses paper and some not)
           are left out: they differ by host, and turned some partners away from the enemies. PM64's
           sprites face left (-x): drawn with x scaled by sx, the front lies along -sx times the matrix's
           x axis. */
        /* A spinning unit (Koops' shell, Mowz's twirls, a stylish turn) keeps the facing it had before the
           spin: the sprite doesn't flip every half turn (PM64's rows spin by themselves); a turn that stays
           (running home turned around) counts once it has held for 4 frames. */
        const float face = unit->faceDirection >= 0 ? 1.0f : -1.0f;
        const float rot = unit->rotation.y + part->rotation.y;
        const float axis = m[0][0] < 0.0f ? -1.0f : 1.0f;
        float spin;
        if (fabsf(rot - sPlay[slot].lastRot) > 0.5f) {
            sPlay[slot].rotStill = 0;
        } else if (sPlay[slot].rotStill < 1000) {
            sPlay[slot].rotStill++;
        }
        if (sPlay[slot].rotStill >= 4) {
            sPlay[slot].faceSpin = cosf(0.017453292f * rot) < 0.0f ? -1.0f : 1.0f;
        }
        spin = sPlay[slot].faceSpin;
        sPlay[slot].lastRot = rot;
        sPlay[slot].lastX = unit->position.x;
        sPlay[slot].lastY = unit->position.y;
        if (p == PM64_PARTNER_WATT) {
            m[1][3] += 10.0f + 3.0f * sinf(0.07f * (float)gp->retraceCount);
        }
        if (scale != 1.0f) {
            PSMTXScale(t, scale, scale, scale);
            PSMTXConcat(m, t, m);
        }
        PSMTXScale(t, -face * spin * axis, 1.0f, 1.0f);
        PSMTXConcat(m, t, m);
        drawSpriteTint(a, m, pose, unblacken);
        coolSprite(a, m, pose, p);
        /* Watt's sparks over him while he's charged (the rows of bolts alone, played beside his) */
        {
            int over = sForcedOverlay;
            if (over < 0 && p == PM64_PARTNER_WATT && anim == 7) {
                over = 16;
            }
            if (over != sPlay[slot].overlay) {
                sPlay[slot].overlay = over;
                if (over >= 0) {
                    portPm64AnimStart(&sPlay[slot].over, portPm64NpcSprite(sprite), over);
                }
            } else if (over >= 0) {
                portPm64AnimStep(&sPlay[slot].over, 1.0f);
            }
            if (over >= 0 && sPlay[slot].over.sprite != NULL) {
                /* (in front of him, or where it has flown to: world units on the matrix's place) */
                PSMTXTrans(t, 0.0f, 0.0f, 0.5f);
                PSMTXConcat(m, t, m);
                m[0][3] += sForcedOff[0];
                m[1][3] += sForcedOff[1];
                m[2][3] += sForcedOff[2];
                drawSpriteTint(&sPlay[slot].over, m, pose, 0);
            }
        }
        return 1;
    }
    if (ride == 1 && p == PM64_PARTNER_LAKILESTER && marioGetPtr() != NULL) {
        /* under Mario on the cloud, a little behind him (his own place stays the game's: draw only) */
        const MarioWork* mario = marioGetPtr();
        m[0][3] = mario->position.x;
        m[1][3] = mario->position.y - 2.0f;
        m[2][3] = mario->position.z - 2.0f;
    } else if (ride == 1 && marioGetPtr() != NULL) {
        /* under Mario where he swims (her own place stays the game's: draw only) */
        const MarioWork* mario = marioGetPtr();
        m[0][3] = mario->position.x;
        m[1][3] = mario->position.y - 4.0f;
        m[2][3] = mario->position.z + 1.0f;
    } else if (ride == 2 && p == PM64_PARTNER_PARAKARRY) {
        m[1][3] += 30.0f; /* over Mario, holding him up */
    } else if (p == PM64_PARTNER_WATT && as == 0 && !(own && portPm64WattHeld())) {
        /* Watt hovers, bobbing slowly, as in Paper Mario (draw only: his place stays the host's); held, she sits
           in Mario's hands (pm64_field_pc.c) */
        m[1][3] += 10.0f + 3.0f * sinf(0.07f * (float)gp->retraceCount);
    } else if (p == PM64_PARTNER_BOW && own) {
        /* Outta Sight (PM64 world_bow OUTTA_SIGHT_GATHER): she closes on Mario, at his middle and a little
           before him towards the camera, and fades with him (pm64_field_pc.c); draw only */
        const float v = portPm64BowVeil();
        const MarioWork* mario = marioGetPtr();
        if (v > 0.0f && mario != NULL) {
            m[0][3] += (mario->position.x - m[0][3]) * v;
            m[1][3] += (mario->position.y + 18.0f - m[1][3]) * v;
            m[2][3] += (mario->position.z + 4.0f - m[2][3]) * v;
            sSeeThrough = (float)portPm64BowVeilAlpha() / 255.0f;
        }
    }
    if (turn != 0.0f) {
        PSMTXRotRad(t, 'y', 0.017453292f * turn);
        PSMTXConcat(m, t, m);
    }
    if (flip < 0.0f) {
        PSMTXScale(t, 1.0f, 1.0f, -1.0f);
        PSMTXConcat(m, t, m);
    }
    if (scale != 1.0f) {
        PSMTXScale(t, scale, scale, scale);
        PSMTXConcat(m, t, m);
    }
    /* PM64's sprites face left, as the hosts' models do unturned: drawn as they are, the host's own turn
     * (partyDisp's matrix: displayDirection less the camera's, and its mirror) faces them where they go. (A
     * mirror here turned every PM64 partner, co-op companion and online ghost away from where it walked.) */
    drawSprite(a, m, pose);
    coolSprite(a, m, pose, p);
    sSeeThrough = 1.0f;
    if (own && ride == 0 && pm64Log()) {
        facingCheck(p, m);
    }
    return 1;
}

/* The Start menu's Party tab (win_party.c): the host's menu model drawn as the PM64 partner in its place, standing
 * (walking while it's the one picked, as the TTYD models do), with the model's tint; 1 if it was. m is the
 * model's matrix there (on the menu's camera, mirrored to face it as the models are), scale the sprite's. */
int portPm64PartyMenuDraw(int poseId, int party, float (*mtx)[4], int selected, float scale) {
    AnimationPose* pose;
    const Pm64Anim* a;
    Mtx m, t;
    int p;
    if (poseId < 0 || gp == NULL || (p = portPm64PartnerOn(party)) == 0 ||
        (pose = animPoseGetAnimPosePtr(poseId)) == NULL) {
        return 0;
    }
    a = playOf(poseId, kWho[p].sprite, kAnims[p][0][selected ? A_WALK : A_IDLE]);
    if (a->sprite == NULL) {
        return 0;
    }
    PSMTXCopy(mtx, m);
    PSMTXScale(t, scale, scale, scale); /* (the menu's matrix is mirrored already: PM64's left-facing sprite comes out facing right, as the TTYD partners stand there) */
    PSMTXConcat(m, t, m);
    drawSprite(a, m, pose);
    coolSprite(a, m, pose, p);
    return 1;
}

/* --- battle: the host's moves as the PM64 partner's */

extern BattleWeapon partyWeapon_ChristineNormalAttack, partyWeapon_ChristineMonosiri, partyWeapon_ChristineRenzokuAttack;
extern BattleWeapon partyWeapon_NokotarouKouraAttack, partyWeapon_NokotarouSyubibinKoura;
extern BattleWeapon partyWeapon_SandersNormalAttack, partyWeapon_SandersTimeBombSet, partyWeapon_SandersSuperBombAttack;
extern BattleWeapon partyWeapon_YoshiNormalAttack, partyWeapon_YoshiNomikomi_Shot, partyWeapon_YoshiEggAttack_Minimini,
    partyWeapon_YoshiCallGuard;
extern BattleWeapon partyWeapon_ChuchurinaNormalAttackLeft, partyWeapon_ChuchurinaNormalAttackRight,
    partyWeapon_ChuchurinaNormalAttackLeftLast, partyWeapon_ChuchurinaNormalAttackRightLast,
    partyWeapon_ChuchurinaMadowaseAttack;
extern BattleWeapon partyWeapon_VivianNormalAttack, partyWeapon_VivianShadowGuard, partyWeapon_VivianMagicalPowder;
extern BattleWeapon partyWeapon_ChristineKiss, partyWeapon_NokotarouKouraGuard, partyWeapon_NokotarouTsuranukiKoura,
    partyWeapon_SandersCounterSet, partyWeapon_SandersSuperBombAttack, partyWeapon_ChuchurinaItemSteal,
    partyWeapon_ChuchurinaKiss, partyWeapon_VivianCharmKissAttack, partyWeapon_ClaudaBreathAttack;
extern BattleWeapon partyWeapon_ClaudaPredationAttack;

/* a host's move and the PM64 move (0..3 of the partner's four) it stands for,
 * and what of PM64's it takes beyond the name, text and FP cost:
 *   dmg: PM64's damage by rank (Normal, Super, Ultra) without and with the
 *        action command done well, where the host's weapon goes by the same
 *        six numbers (weaponGetPowerFromPartyAttackLv); {0} keeps the host's.
 *        PM64's own numbers (its partners' battle scripts, the decomp).
 *   fx:  the move's other effects, mapped onto TTYD's (FX_*). */
enum {
    FX_NONE = 0,
    FX_SHOCK = 1,    /* electric, through defense (Electro Dash) */
    FX_WATER = 2,    /* water: no fire, no burn (TTYD has no water element: normal) */
    FX_PARALYZE = 4, /* paralysis as TTYD's stop, 3 turns, at the action command's chance (Mega Shock) */
    FX_SINGLE = 8,   /* one enemy, not all (Power Shock on Tease's script) */
    FX_FIRE = 16,    /* fire; PM64 has no burn (Fire Shell) */
    FX_DIZZY = 32,   /* no damage: dizzy, 2 turns (Dizzy Shell) */
    FX_GROUND = 64,  /* enemies on the ground only (Power Bomb) */
    FX_CONTACT = 128, /* a plain ram (Body Slam: PM64's damage type 0, ignoring a spiky top only): normal
                         element, so no blast look, no grounding or flipping; other hazards hurt her */
    FX_JUMP = 256,    /* PM64's DAMAGE_TYPE_JUMP (Headbonk, Multibonk, Belly Flop): flips a shelled enemy, grounds a
                         winged one, as a stomp */
    FX_BLAST = 512,   /* PM64's DAMAGE_TYPE_BLAST (Bombette's bombs): TTYD's explosion element, flips what a bomb flips */
};

/* the moves that help rather than hit: the port's own script (sBuffEvt), the
 * status on the one it targets */
enum {
    BUFF_NONE,
    BUFF_CHARGE_SELF, /* Goombario's Charge: TTYD's Charge on himself, +2 (+3 at Ultra rank) */
    BUFF_ATK_MARIO,   /* Watt's Turbo Charge: Mario's attack +1 for 3 turns */
    BUFF_DODGY_MARIO, /* Lakilester's Cloud Nine: Mario dodgy (TTYD's 50% evasion) for 3 turns */
    BUFF_DEF_MARIO,   /* Sushie's Water Block: Mario's defense +1 for 3 turns */
};

/* a move row's targets over its weapon's (MoveMap.targets) */
enum {
    TARGETS_WEAPON,     /* as the weapon has them */
    TARGETS_ALL,        /* every enemy it can reach */
    TARGETS_ANY_ONE,    /* any one enemy: in the air or on the ceiling too */
    TARGETS_NO_CEILING, /* not one on the ceiling */
};

/* args[0] := the unit running the script's glad pose (its host model's Y_1: drawn as the PM64 sprite's glad
 * animation) */
static USER_FUNC(pm64GladPose) {
    /* (the hosts of the buff moves: Goombella, Yoshi, Flurrie, Ms. Mowz) */
    const BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, BTL_UNIT_SELF));
    const char* name = "PKR_Y_1";
    if (unit != NULL && unit->currentKind == UNIT_YOSHI) {
        name = "PYS_Y_1";
    } else if (unit != NULL && unit->currentKind == UNIT_FLURRIE) {
        name = "PWD_Y_1";
    } else if (unit != NULL && unit->currentKind == UNIT_MS_MOWZ) {
        name = "PCH_Y_1";
    }
    evtSetValue(event, event->args[0], (EvtWord)name);
    return EVT_RETURN_DONE;
}

/* The buff moves: the partner turns to the one it helps, cheers (the PM64
 * sprite's glad animation), and the status lands; then back to standing. */
static EVT_BEGIN(sBuffEvt)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_GetUnitId, BTL_UNIT_SELF, LW(0))
    IF_INT_NOT_EQUAL(LW(0), LW(3))
        CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
        CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    END_IF
    CALL(pm64GladPose, LW(0))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, LW(0))
    WAIT_FRAMES(30)
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    WAIT_FRAMES(45)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(99)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- the port's own battle scripts: PM64's choreography with TTYD's commands and damage */

static BattleUnit* evtUnit(EventEntry* event, EvtWord arg) {
    return BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, arg)));
}

/* The event a move's leftovers (a forced row, a held effect, held damage) belong to: the unit's attack while
 * it runs (a move's children and siblings with it), else the event at the top of the caller's chain (Outta
 * Sight's rest). A counter, a superguard or a torn-down battle can end that event before the move's own
 * PM64_END undoes them: pm64MovesFrame then drops whatever is left of an event that is gone. */
static int pm64Owner(EventEntry* event, const BattleUnit* unit) {
    int n;
    if (unit != NULL && unit->attackEventId != 0 && evtCheckID(unit->attackEventId)) {
        return unit->attackEventId;
    }
    for (n = 0; event != NULL && n < 64; n++) {
        if (event->waitingEvent != NULL) {
            event = event->waitingEvent;
        } else if (event->brotherEvent != NULL) {
            event = event->brotherEvent;
        } else {
            break;
        }
    }
    return event != NULL ? event->eventId : 0;
}

static int forcedSlot(const BattleUnit* unit, int make) {
    int i, free = -1;
    if (unit == NULL) {
        return -1;
    }
    for (i = 0; i < (int)(sizeof(sForced) / sizeof(sForced[0])); i++) {
        if (sForced[i].unit == unit) {
            return i;
        }
        if (free < 0 && (sForced[i].unit == NULL || sForced[i].anim < 0)) {
            free = i;
        }
    }
    if (make && free >= 0) {
        sForced[free].unit = unit;
        sForced[free].ox = sForced[free].oy = sForced[free].oz = 0.0f;
    }
    return make ? free : -1;
}

/* pm64SetAnim(unit, anim, overlay): the unit's PM64 sprite plays battle row anim (-1: back to its host
 * pose's), with row overlay drawn over it (-1 none) */
static USER_FUNC(pm64SetAnim) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    const int anim = evtGetValue(event, event->args[1]), over = evtGetValue(event, event->args[2]);
    const int i = forcedSlot(unit, anim >= 0);
    if (i >= 0) {
        sForced[i].anim = (signed char)anim;
        sForced[i].overlay = (signed char)over;
        sForced[i].ox = sForced[i].oy = sForced[i].oz = 0.0f;
        sForced[i].owner = pm64Owner(event, unit);
        if (anim < 0) {
            sForced[i].unit = NULL;
        }
    } else if (anim >= 0 && unit != NULL && pm64Log()) {
        printf("[pm64] no free slot: row %d not shown\n", anim); /* (tests: never, now that leftovers go) */
        fflush(stdout);
    }
    return EVT_RETURN_DONE;
}

static void forcedClear(int i) {
    sForced[i].unit = NULL;
    sForced[i].anim = sForced[i].overlay = -1;
    sForced[i].ox = sForced[i].oy = sForced[i].oz = 0.0f;
    sForced[i].owner = 0;
}

/* pm64Fx(kind, unit, height): an effect on the unit, height units up (the game's own PM64 effects, eff/n64):
 *   0 the held ones end            1 the charge sparkles          2 electricity (held)
 *   3 a cloud puff                 4 a splash                     5 Watt's charge glow (held)
 *   6 a shock flash                7 a lightning strike           8 a block of water (Water Block)
 *   9 a whirlwind                  10 a flame                     11 dizzy stars
 *   12 a curtain of water          13 water spray                 14 Watt's flash
 *   15, 16, 17 Bombette's blast (small, big, huge)                18 confetti (Fan Smack)
 *   19 a lens flare (PM64 EFFECT_LENS_FLARE: Air Raid)            20 energy gathering in (EFFECT_ENERGY_IN_OUT:
 *   Spook)                                                        21 a shockwave of light (Mega Shock's) */
enum { HELD_SPARK, HELD_GLOW, HELD_COUNT };
static struct {
    EffectEntry* e;
    void* work;       /* (its userdata and type when made: a pool entry another effect has taken since is left be) */
    const char* type;
    u32 inBattle;     /* (and whether a battle's or the field's) */
    int owner;        /* (pm64Owner) */
} sHeld[HELD_COUNT];

/* still the effect made then (shockFrame's test) */
static int effStillOurs(EffectEntry* e, void* work, const char* type) {
    return e != NULL && e->flags != 0 && e->userdata == work && e->type != NULL && type != NULL &&
           strcmp(e->type, type) == 0;
}

/* held effect i is still the one made then, and not already asked to go */
static int heldOurs(int i) {
    return effStillOurs(sHeld[i].e, sHeld[i].work, sHeld[i].type) && sHeld[i].e->inBattle == sHeld[i].inBattle &&
           !(sHeld[i].e->flags & EFFECT_FLAG_DELETE_REQUESTED);
}

/* held effect i let go of without touching it (it ended, or its entry is another's) */
static void heldForget(int i) {
    sHeld[i].e = NULL;
    sHeld[i].work = NULL;
    sHeld[i].type = NULL;
    sHeld[i].inBattle = 0;
    sHeld[i].owner = 0;
}

static void heldEnd(int i) {
    if (sHeld[i].e != NULL && heldOurs(i)) {
        effSoftDelete(sHeld[i].e);
    }
    heldForget(i);
}

static void heldSet(int i, EffectEntry* e, EventEntry* event) {
    sHeld[i].e = e;
    sHeld[i].work = e != NULL ? e->userdata : NULL;
    sHeld[i].type = e != NULL ? e->type : NULL;
    sHeld[i].inBattle = e != NULL ? e->inBattle : 0;
    sHeld[i].owner = pm64Owner(event, evtUnit(event, BTL_UNIT_SELF));
}

static void shockSized(EffectEntry* e, float wide, float tall);

static USER_FUNC(pm64Fx) {
    const int kind = evtGetValue(event, event->args[0]);
    const BattleUnit* unit = evtUnit(event, event->args[1]);
    const float up = (float)evtGetValue(event, event->args[2]);
    float x, y, z, wide, tall;
    EffectEntry* e;
    if (kind == 0 || kind == 2 || kind == 5) {
        int i;
        for (i = 0; i < HELD_COUNT; i++) {
            if (kind == 0 || i == (kind == 2 ? HELD_SPARK : HELD_GLOW)) {
                heldEnd(i);
            }
        }
    }
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    x = unit->position.x;
    y = unit->position.y + up;
    z = unit->position.z + 5.0f;
    wide = (float)unit->width;
    tall = (float)unit->height;
    switch (kind) {
    case 1:
        effChargeEntry(1, 50, x, y, z, 1.0f);
        break;
    case 2:
        heldSet(HELD_SPARK, effSparkEntry(0, x, y, z), event);
        break;
    case 3:
        effKemuTestEntry(5, x, y, z, 1.0f);
        break;
    case 4:
        effSplashEntry(1, x, y, z, 0.8f);
        break;
    case 5:
        heldSet(HELD_GLOW, effAkariChargeN64Entry(0, 0, x, y, z, 1.0f), event);
        break;
    case 6:
        shockSized(effShockN64Entry(3, x, y, z, wide, tall), wide, tall); /* (sized to the target, as PM64's) */
        break;
    case 7:
        effThunderN64Entry(3, x, y, z, wide, tall);
        break;
    case 8:
        effOpukuWprotectN64Entry(0, PM64_FRAMES(60), x, y - up, z, 1.0f);
        break;
    case 9:
        e = effWhirlwindN64Entry(unit->faceDirection < 0, NULL, unit->sizeMultiplier, PM64_FRAMES(60));
        if (e == NULL || e->userdata == NULL) {
            break; /* (the effect pool full) */
        }
        ((EffWhirlwindN64Work*)e->userdata)->position.x = x;
        ((EffWhirlwindN64Work*)e->userdata)->position.y = y - up;
        ((EffWhirlwindN64Work*)e->userdata)->position.z = z;
        break;
    case 10:
        effFlameN64Entry(0, x, y, z, 0.6f);
        break;
    case 11:
        effGuruguruN64Entry(0, 5, x, y, z, wide * 0.5f + 6.0f);
        break;
    case 12:
        effOpukuWcurtainN64Entry(0, PM64_FRAMES(40), x, y - up, z, 1.0f);
        break;
    case 13:
        effWaterDamageN64Entry(0, PM64_FRAMES(30), x, y, z, 1.0f);
        break;
    case 14:
        effAkariSflushN64Entry(0, PM64_FRAMES(20), x, y, z, 1.0f);
        break;
    case 15:
    case 16:
    case 17:
        /* Bombette's blast, small / big / huge (PM64's fx_explosion 0-2, 20 above her) */
        effBombN64Entry(kind - 15, x, y, z);
        break;
    case 18: /* (Bow's Fan Smack confetti) */
        effConfettiN64Entry(5, 40, x, y + tall * 0.5f, z, 1.0f);
        break;
    case 19: /* (parakarry.c FlyAround: EFFECT_LENS_FLARE, 10 frames) */
        effLensflareN64Entry(0, PM64_FRAMES(10), x, y, z, 1.0f);
        break;
    case 20: /* (bow.c spook: EFFECT_ENERGY_IN_OUT, 45 frames, at 1.8: the charge sparkles, longer and bigger) */
        effChargeEntry(1, PM64_FRAMES(45), x, y, z, 1.8f);
        break;
    case 21: /* (watt.c EVS_Attack_MegaShock: EFFECT_FLASHING_BOX_SHOCKWAVE, the flash of light outward) */
        effAkariSflushN64Entry(0, PM64_FRAMES(20), x, y, z, 2.5f);
        break;
    default:
        break;
    }
    return EVT_RETURN_DONE;
}

/* pm64Beam(kind, from, to, frames): from one unit to another: 1 a jet of water, 3 a big one (Squirt), 2 a bolt */
static USER_FUNC(pm64Beam) {
    const int kind = evtGetValue(event, event->args[0]);
    const BattleUnit* a = evtUnit(event, event->args[1]);
    const BattleUnit* b = evtUnit(event, event->args[2]);
    const int frames = PM64_FRAMES(evtGetValue(event, event->args[3])); /* (PM64's: doubled) */
    float ax, ay, bx, by;
    if (a == NULL || b == NULL) {
        return EVT_RETURN_DONE;
    }
    ax = a->position.x + (float)a->faceDirection * (float)a->width * 0.4f;
    ay = a->position.y + (float)a->height * 0.55f;
    bx = b->position.x;
    by = b->position.y + (float)b->height * 0.5f;
    if (kind == 1 || kind == 3) {
        effOpukuWgunN64Entry(0, frames, ax, ay, a->position.z + 5.0f, bx, by, b->position.z + 5.0f,
                             kind == 3 ? 2.0f : 1.0f); /* (3: Squirt's, PM64's full-charge size) */
    } else {
        effPokopiThunderN64Entry(0, frames, ax, ay, a->position.z + 5.0f, bx, by, b->position.z + 5.0f, 1.0f);
    }
    return EVT_RETURN_DONE;
}

/* PM64's backdrop dimming (its battle scripts' SetBackgroundAlpha: Watt darkens the background behind the stage
 * while he charges). TTYD's own stage darkness (evt_batstage_set_stg_dark) is a black sheet before the stage, so it
 * dims the units too; this one dims only what lies behind the hindmost unit: black sheets drawn depth-tested
 * at four depths from 25 behind it to 100 behind (each a share of the dark), so the floor darkens gradually
 * toward the back rather than at a line, and the units, in front of all four, keep their light. */
static struct {
    float cur, from, to;
    int time, max;
} sBackdrop;

/* pm64Backdrop(alpha, frames): the backdrop's dark to alpha (0-255) over frames (TTYD frames: PM64_FRAMES(n) for
 * a PM64 count), at once for 0 */
static USER_FUNC(pm64Backdrop) {
    const int to = evtGetValue(event, event->args[0]);
    const int frames = evtGetValue(event, event->args[1]);
    sBackdrop.from = sBackdrop.cur;
    sBackdrop.to = (float)(to < 0 ? 0 : to > 255 ? 255 : to);
    sBackdrop.time = sBackdrop.max = frames > 0 ? frames : 0;
    if (frames <= 0) {
        sBackdrop.cur = sBackdrop.to;
    }
    return EVT_RETURN_DONE;
}

static void backdropSheet(float z, u8 alpha) {
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-1200.0f, 900.0f, z);
    GXColor4u8(0, 0, 0, alpha);
    GXPosition3f32(1200.0f, 900.0f, z);
    GXColor4u8(0, 0, 0, alpha);
    GXPosition3f32(1200.0f, -300.0f, z);
    GXColor4u8(0, 0, 0, alpha);
    GXPosition3f32(-1200.0f, -300.0f, z);
    GXColor4u8(0, 0, 0, alpha);
    GXEnd();
}

static void backdropDisp(CameraId camId, void* param) {
    enum { SHEETS = 4 };
    CameraEntry* cam = camGetPtr(CAMERA_3D);
    BattleWork* wp = _battleWorkPointer;
    float back = 0.0f, share;
    int i;
    (void)camId;
    (void)param;
    if (cam == NULL || wp == NULL) {
        return;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* unit = BattleGetUnitPtr(wp, i);
        if (unit != NULL && unit->position.z < back) {
            back = unit->position.z;
        }
    }
    /* each sheet's alpha so that all four over one another make the whole dark */
    share = 1.0f - powf(1.0f - sBackdrop.cur / 255.0f, 1.0f / (float)SHEETS);
    GXSetCullMode(GX_CULL_NONE);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_CLAMP, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumIndStages(0);
    GXLoadPosMtxImm(cam->view, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetZMode(GX_TRUE, GX_LESS, GX_FALSE); /* (only where something lies behind the sheet) */
    for (i = 0; i < SHEETS; i++) {
        backdropSheet(back - 25.0f - 25.0f * (float)i, (u8)(255.0f * share + 0.5f));
    }
}

/* each frame (portPm64BattleCosts): the fade stepped, the sheets queued while there's any dark */
static void backdropFrame(void) {
    if (gp == NULL || !gp->inBattle || _battleWorkPointer == NULL) {
        sBackdrop.cur = sBackdrop.to = 0.0f;
        sBackdrop.time = 0;
        return;
    }
    if (sBackdrop.time > 0) {
        sBackdrop.time--;
        sBackdrop.cur = sBackdrop.to + (sBackdrop.from - sBackdrop.to) * (float)sBackdrop.time / (float)sBackdrop.max;
    }
    if (sBackdrop.cur >= 1.0f) {
        dispEntry(CAMERA_3D, 4990.0f, 2, backdropDisp, NULL);
    }
}

/* The shock flash (pm64Fx 6, TTYD's port of PM64's shock hit) grows to one size whatever it hits, far bigger
 * than an enemy; PM64's (apply_shock_effect: fx_flashing_box_shockwave) is sized by the target's box,
 * 1.25 x its width by its height - 2. Each one made here is kept to that: its scale, which eases toward 8 from 0
 * (8 x (1 - 0.95^age)), is set each frame (after the effects have stepped: the frame hook) to that curve times
 * the target's share, the mesh's dark rim (about 180 across at a 0.25 scale) round the box. */
static struct {
    EffectEntry* e;
    void* work;
    float f;
} sShock[8];

static void shockSized(EffectEntry* e, float wide, float tall) {
    const float box = wide * 1.25f > tall - 2.0f ? wide * 1.25f : tall - 2.0f;
    float f = (0.5f * box + 4.0f) / (180.0f * 0.25f * 8.0f);
    int i, at = 0;
    if (e == NULL) {
        return;
    }
    f = f < 0.03f ? 0.03f : f > 1.0f ? 1.0f : f;
    for (i = 0; i < (int)(sizeof(sShock) / sizeof(sShock[0])); i++) {
        if (sShock[i].e == NULL) {
            at = i;
            break;
        }
    }
    sShock[at].e = e;
    sShock[at].work = e->userdata;
    sShock[at].f = f;
    ((EffShockN64Work*)e->userdata)->scale = 0.0f;
}

static void shockFrame(void) {
    int i;
    for (i = 0; i < (int)(sizeof(sShock) / sizeof(sShock[0])); i++) {
        EffectEntry* e = sShock[i].e;
        if (e == NULL) {
            continue;
        }
        if (e->flags == 0 || e->userdata != sShock[i].work || e->type == NULL || strcmp(e->type, "ShockN64") != 0) {
            sShock[i].e = NULL;
            continue;
        }
        {
            EffShockN64Work* w = (EffShockN64Work*)e->userdata;
            w->scale = sShock[i].f * 8.0f * (1.0f - powf(0.95f, (float)w->age));
        }
    }
}

/* pm64Zoom(unit, kind): the unit flies about the stage at speed, bouncing off a box, then off the side it next
 * meets after the time is up, 5 frames more. Blocks. PM64's N(FlyAround) (parakarry.c, Air Raid: kind 1, 48 a
 * frame from 60 less up to 10 degrees, box x -190..190, y -30..160, 90 frames, a new drift at the left side) and
 * N(ProcessTidalWave) (sushie.c, Tidal Wave: kind 2, 32 a frame from 315, box x -160..160, y 0..130, 90 frames,
 * a new drift at every side, a splash each frame). Angles as PM64's: 0 up, 90 forward. */
static USER_FUNC(pm64Zoom) {
    static struct {
        float angle, drift;
        int time, tail;
    } z;
    BattleUnit* unit = evtUnit(event, event->args[0]);
    const int kind = evtGetValue(event, event->args[1]);
    const float speed = PM64_PER_FRAME(kind == 1 ? 48.0f : 32.0f);
    const float x0 = kind == 1 ? -190.0f : -160.0f, x1 = -x0, y0 = kind == 1 ? -30.0f : 0.0f;
    const float y1 = kind == 1 ? 160.0f : 130.0f;
    float x, y, dir;
    int hit = 0;
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        z.angle = kind == 1 ? (float)(60 - irand(10)) : 315.0f;
        z.drift = 0.0f;
        z.time = PM64_FRAMES(90);
        z.tail = -1;
    }
    dir = unit->faceDirection < 0 ? -1.0f : 1.0f;
    x = unit->position.x * dir;
    y = unit->position.y;
    x += speed * sinf(z.angle * 3.14159265f / 180.0f);
    y -= speed * cosf(z.angle * 3.14159265f / 180.0f);
    z.angle += PM64_PER_FRAME(z.drift * 0.5f);
    if (x < x0 || x > x1) {
        z.angle = 360.0f - z.angle;
        if (kind == 2 || x < x0) {
            z.drift = (float)(irand(5) - 2);
        }
        x = x < x0 ? x0 : x1;
        hit = 1;
    }
    if (y < y0 || y > y1) {
        z.angle = 180.0f - z.angle;
        if (kind == 2) {
            z.drift = (float)(irand(5) - 2);
        }
        y = y < y0 ? y0 : y1;
        hit = 1;
    }
    while (z.angle < 0.0f) {
        z.angle += 360.0f;
    }
    while (z.angle >= 360.0f) {
        z.angle -= 360.0f;
    }
    if (hit && z.tail < 0) {
        psndSFXOn(kind == 1 ? "SFX_PARTY_BATTLE_JUMP1" : "SFX_BTL_STAGE_FALL_WATER1");
    }
    BtlUnit_SetPos(unit, x * dir, y, unit->position.z);
    if (kind == 2 && (z.time & 1) == 0) { /* (a splash each PM64 frame) */
        effSplashEntry(1, x * dir, y, unit->position.z + 5.0f, 0.6f);
    }
    if (z.time > 0) {
        z.time--;
    } else if (z.tail < 0 && hit) {
        z.tail = PM64_FRAMES(5);
    }
    if (z.tail >= 0 && z.tail-- == 0) {
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* pm64StatusWeapon(weapon, kind, value): weapon := a copy of it (one per kind) with the action command's result
 * in it: 1 Power/Mega Shock's paralysis chance, 2 Hurricane's blow-away chance, 3 Spook's scare (flee) chance */
static USER_FUNC(pm64StatusWeapon) {
    static BattleWeapon sCopy[3];
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    const int kind = evtGetValue(event, event->args[1]);
    const int value = evtGetValue(event, event->args[2]);
    const int v = value < 0 ? 0 : value > 100 ? 100 : value;
    BattleWeapon* w;
    if (src == NULL || kind < 1 || kind > 3) {
        return EVT_RETURN_DONE;
    }
    w = &sCopy[kind - 1];
    if (src != w) {
        memcpy(w, src, sizeof(*w));
    }
    if (kind == 1) {
        w->stopChance = (u8)v;
        w->stopTime = 3;
        w->dizzyChance = 0;
        w->dizzyTime = 0;
    } else if (kind == 2) {
        w->galeForceChance = (u8)v;
    } else {
        w->frightChance = (u8)v;
    }
    if (pm64Log()) {
        printf("[pm64] %s: chance %d\n",
               kind == 1 ? "shock paralysis" : kind == 2 ? "Hurricane blow-away" : "Spook scare", v);
        fflush(stdout);
    }
    evtSetValue(event, event->args[0], (EvtWord)w);
    return EVT_RETURN_DONE;
}

/* pm64Throw(unit, x, y, z, frames, height, row): row (a thrown thing: Lakilester's spiny) flies from over the
 * unit to (x, y, z) along an arc height units high, in frames; the script waits for it */
static USER_FUNC(pm64Throw) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    const int frames = PM64_FRAMES(evtGetValue(event, event->args[4]));
    int i;
    if (unit == NULL || (i = forcedSlot(unit, 0)) < 0) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        event->userdata[0] = 0;
        sForced[i].overlay = (signed char)evtGetValue(event, event->args[6]);
    }
    event->userdata[0]++;
    {
        const float t = (float)event->userdata[0] / (float)(frames > 0 ? frames : 1);
        const float sx = 0.0f, sy = 30.0f; /* (from over his head) */
        const float tx = (float)evtGetValue(event, event->args[1]) - unit->position.x;
        const float ty = (float)evtGetValue(event, event->args[2]) - unit->position.y;
        const float tz = (float)evtGetValue(event, event->args[3]) - unit->position.z;
        const float h = (float)evtGetValue(event, event->args[5]);
        sForced[i].ox = sx + (tx - sx) * t;
        sForced[i].oy = sy + (ty - sy) * t + 4.0f * h * t * (1.0f - t);
        sForced[i].oz = tz * t + 2.0f;
    }
    if (event->userdata[0] >= frames) {
        sForced[i].overlay = -1;
        sForced[i].ox = sForced[i].oy = sForced[i].oz = 0.0f;
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* --- the rest of the moves, each its own: PM64's choreography, TTYD's action commands (the host move's kind
 * of command where PM64's is the same kind), the damage rules TTYD's (PM64's numbers by rank: sMoves) */

/* the start of an attack: its target(s), the weapon (LW(12)), declared at one (declare: AttackDeclare) or all */
#define PM64_BEGIN(declare)                                                                                   \
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))                                                              \
    IF_INT_EQUAL(LW(3), -1)                                                                                   \
        GOTO(99)                                                                                              \
    END_IF                                                                                                    \
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))                                            \
    CALL(btlevtcmd_WeaponAftereffect, LW(12))                                                                 \
    declare CALL(btlevtcmd_WaitGuardMove)                                                                     \
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)                                                       \
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
#define PM64_ONE CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
#define PM64_ALL CALL(btlevtcmd_AttackDeclareAll, BTL_UNIT_SELF)

/* the end: standing again, facing the way it does, the badges' after-attack effects, the wait */
static USER_FUNC(pm64ReleaseDamage);
#define PM64_END                                                                                              \
    LABEL(99)                                                                                                 \
    CALL(pm64ReleaseDamage)                                                                                   \
    CALL(pm64Fx, 0, BTL_UNIT_SELF, 0)                                                                         \
    CALL(pm64SetAnim, BTL_UNIT_SELF, -1, -1)                                                                  \
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)                                      \
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)                                                         \
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)                                                              \
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)                                                             \
    RETURN

/* the action commands, set up (StartAC to run them): */
/* a timed press as the move lands (Headbonk's) */
#define PM64_AC_TIMED                                                                                         \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 1, 1, 0)
/* (its window, once the move is there) */
#define PM64_AC_TIMED_WINDOW                                                                                  \
    CALL(btlevtcmd_ac_timing_flag_onoff, 1, 1)                                                                \
    CALL(btlevtcmd_ac_timing_get_success_frame, LW(0))                                                        \
    DIVIDE(LW(0), 2)                                                                                          \
    WAIT_FRAMES(LW(0))                                                                                        \
    CALL(btlevtcmd_ac_timing_flag_onoff, 0, 1)                                                                \
    CALL(btlevtcmd_ResultAC)
/* hold A, let go when the light comes on (Bobbery's Bomb) */
#define PM64_AC_HOLD_A                                                                                        \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(1), 60)                                                                                  \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(1), 45)                                                                                  \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(1), 30)                                                                                  \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(1), 25)                                                                                  \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(1), 20)                                                                                  \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(1), 12)                                                                                  \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(1), 6)                                                                                   \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, 2, 120, LW(1), 0, 100, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)               \
    CALL(btlevtcmd_AcSetFlag, 15)                                                                             \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 15, 1, 0)
/* hold the stick left, let go when the light comes on (Shell Toss) */
#define PM64_AC_HOLD_LEFT                                                                                     \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(0), 360)                                                                                 \
        SET_VALUE(LW(1), 100)                                                                                 \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(0), 280)                                                                                 \
        SET_VALUE(LW(1), 150)                                                                                 \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(0), 240)                                                                                 \
        SET_VALUE(LW(1), 200)                                                                                 \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(0), 200)                                                                                 \
        SET_VALUE(LW(1), 250)                                                                                 \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(0), 200)                                                                                 \
        SET_VALUE(LW(1), 360)                                                                                 \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(0), 200)                                                                                 \
        SET_VALUE(LW(1), 600)                                                                                 \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(0), 200)                                                                                 \
        SET_VALUE(LW(1), 960)                                                                                 \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, 2, LW(0), 0, 1, LW(1), EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)               \
    CALL(btlevtcmd_AcSetFlag, 3)                                                                              \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 19, 1, 60)
/* mash A to fill the gauge (Bob-ombast's); AcGetOutputParam 1: how full */
#define PM64_AC_MASH                                                                                          \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(0), 300)                                                                                 \
        SET_VALUE(LW(1), 20)                                                                                  \
        SET_VALUE(LW(2), 20)                                                                                  \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(0), 270)                                                                                 \
        SET_VALUE(LW(1), 18)                                                                                  \
        SET_VALUE(LW(2), 22)                                                                                  \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(0), 255)                                                                                 \
        SET_VALUE(LW(1), 17)                                                                                  \
        SET_VALUE(LW(2), 23)                                                                                  \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(0), 225)                                                                                 \
        SET_VALUE(LW(1), 16)                                                                                  \
        SET_VALUE(LW(2), 24)                                                                                  \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(0), 210)                                                                                 \
        SET_VALUE(LW(1), 15)                                                                                  \
        SET_VALUE(LW(2), 26)                                                                                  \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(0), 180)                                                                                 \
        SET_VALUE(LW(1), 14)                                                                                  \
        SET_VALUE(LW(2), 28)                                                                                  \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(0), 150)                                                                                 \
        SET_VALUE(LW(1), 13)                                                                                  \
        SET_VALUE(LW(2), 28)                                                                                  \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, 1, LW(0), 178, LW(1), LW(2), 15, 46, EVT_NULLPTR)                           \
    CALL(btlevtcmd_AcSetGaugeParam, 45, 75, 100, 100)                                                         \
    CALL(btlevtcmd_AcSetFlag, 64)                                                                             \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 6, 1, 0)
/* hold A and let go in the gauge's top (Tease's); AcGetOutputParam 1: the chance it gives */
#define PM64_AC_GAUGE                                                                                         \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcSetFlag, 41)                                                                             \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(0), 3)                                                                                   \
        SET_VALUE(LW(1), 50)                                                                                  \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(0), 4)                                                                                   \
        SET_VALUE(LW(1), 80)                                                                                  \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(0), 5)                                                                                   \
        SET_VALUE(LW(1), 100)                                                                                 \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(0), 7)                                                                                   \
        SET_VALUE(LW(1), 100)                                                                                 \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(0), 8)                                                                                   \
        SET_VALUE(LW(1), 110)                                                                                 \
        CALL(btlevtcmd_AcSetFlag, 49)                                                                         \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(0), 10)                                                                                  \
        SET_VALUE(LW(1), 120)                                                                                 \
        CALL(btlevtcmd_AcSetFlag, 49)                                                                         \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(0), 12)                                                                                  \
        SET_VALUE(LW(1), 130)                                                                                 \
        CALL(btlevtcmd_AcSetFlag, 49)                                                                         \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, 1, 240, 178, LW(0), LW(1), 100, 1, EVT_NULLPTR)                             \
    CALL(btlevtcmd_AcSetGaugeParam, 34, 68, 100, 100)                                                         \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 6, 1, 0)

/* tests (TTYD_PM64_LOG): pm64MissLog(unit, pre-check): a hit the pre-check turned away (dodged, intangible,
 * ...) logged, as portPm64HitLog logs the ones dealt */
static USER_FUNC(pm64MissLog) {
    const BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, event->args[0])));
    if (pm64Log()) {
        printf("[pm64] hit on %s turned away: pre-check %d\n",
               unit != NULL && unit->kindParams != NULL && unit->kindParams->name != NULL ? unit->kindParams->name : "?",
               (int)evtGetValue(event, event->args[1]));
        fflush(stdout);
    }
    return EVT_RETURN_DONE;
}

/* tests (TTYD_PM64_LOG): pm64Trace(what, a, b, c): a move's command outcome logged ("[pm64] Shell Shot: ...") */
static USER_FUNC(pm64Trace) {
    (void)isFirstCall;
    if (pm64Log()) {
        printf("[pm64] %s: %d %d %d\n", (const char*)evtGetValue(event, event->args[0]),
               (int)evtGetValue(event, event->args[1]), (int)evtGetValue(event, event->args[2]),
               (int)evtGetValue(event, event->args[3]));
        fflush(stdout);
    }
    return EVT_RETURN_DONE;
}

/* pm64UnitHp(unit, out hp): the unit's HP, -1 when it is gone (btlevtcmd_GetHp without the NULL unit) */
static USER_FUNC(pm64UnitHp) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], unit != NULL ? unit->currentHp : -1);
    return EVT_RETURN_DONE;
}

/* pm64KoAt0(unit, part): a unit an earlier hit of the move (not its last) took to 0 HP is knocked out, with no
 * hit of its own: no damage, no pre-check, no counter on the attacker (BattleCheckDamage's own KO at 0 HP: a last
 * hit, through BattleDamageDirect, with nothing dealt). Nothing when it still has HP or is gone. */
static USER_FUNC(pm64KoAt0) {
    BattleUnit* self = evtUnit(event, BTL_UNIT_SELF);
    BattleUnit* unit = evtUnit(event, event->args[0]);
    (void)isFirstCall;
    if (unit == NULL || unit->currentHp > 0) {
        return EVT_RETURN_DONE;
    }
    if (pm64Log()) {
        printf("[pm64] a 0-HP target knocked out with no hit of its own\n");
        fflush(stdout);
    }
    BattleDamageDirect(self != NULL ? self->unitId : -5, unit, BtlUnit_GetPartsPtr(unit, evtGetValue(event, event->args[1])),
                       0, 0, DAMAGE_FLAG_LAST_HIT | DAMAGE_HIT_STATUS_ONLY, 0, 0);
    return EVT_RETURN_DONE;
}

/* One hit on LW(3)/LW(4) with weapon LW(12), the command's result in: a dodge (LW(5) -1), or the damage with
 * the Nice/Good rating when the command went well (LW(15) counts those). sHitEvt rolls the hit (a sweep's or
 * sHitAllEvt's next target); sHitCheckedEvt follows a move's own CommandPreCheckDamage that already came back 1,
 * so it does not roll the accuracy again (only an evasive, invisible or intangible target still turns it away). */
#define PM64_HIT_EVT(name, preflags)                                                                          \
    static EVT_BEGIN(name)                                                                                    \
        CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), preflags, LW(6))                  \
        IF_INT_NOT_EQUAL(LW(6), 1)                                                                            \
            CALL(pm64MissLog, LW(3), LW(6))                                                                   \
            IF_INT_EQUAL(LW(6), 3)                                                                            \
                CALL(btlevtcmd_StartAvoid, LW(3), 38)                                                         \
            END_IF                                                                                            \
            IF_INT_EQUAL(LW(6), 6)                                                                            \
                CALL(btlevtcmd_StartAvoid, LW(3), 39)                                                         \
            END_IF                                                                                            \
            IF_INT_EQUAL(LW(6), 2)                                                                            \
                CALL(btlevtcmd_StartAvoid, LW(3), 40)                                                         \
            END_IF                                                                                            \
            SET_VALUE(LW(5), -1)                                                                              \
            RETURN                                                                                            \
        END_IF                                                                                                \
        CALL_CHILD(sHitDealEvt)                                                                               \
        RETURN                                                                                                \
    EVT_END()

static EVT_BEGIN(sHitDealEvt)
    CALL(btlevtcmd_GetResultAC, LW(0))
    IF_BITS_SET(LW(0), 2)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        IF_INT_GREATER_OR_EQUAL(LW(6), 0)
            CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
            ADD(LW(15), 1)
        END_IF
    ELSE
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    END_IF
    RETURN
EVT_END()

PM64_HIT_EVT(sHitEvt, DAMAGE_FLAG_LAST_HIT)
PM64_HIT_EVT(sHitCheckedEvt, DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_ALWAYS_HIT)

/* Every target from LW(3)/LW(4) on, one after another LW(11) frames apart: effect LW(13) on it (pm64Fx; -1
 * none), a beam LW(14) to it from the partner (pm64Beam; 0 none), the hit; the rating once if none went well */
static EVT_BEGIN(sHitAllEvt)
    SET_VALUE(LW(15), 0)
    LABEL(10)
    CALL(pm64Fx, LW(13), LW(3), 12)
    IF_INT_NOT_EQUAL(LW(14), 0)
        CALL(pm64Beam, LW(14), BTL_UNIT_SELF, LW(3), 18)
    END_IF
    CALL_CHILD(sHitEvt)
    MULTIPLY(LW(11), 2) /* (PM64's frames apart: PM64_FRAMES) */
    WAIT_FRAMES(LW(11))
    DIVIDE(LW(11), 2)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(10)
    END_IF
    IF_INT_EQUAL(LW(15), 0)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    RETURN
EVT_END()

/* the camera on the partner, or following it to the target */
#define PM64_CAM_SELF                                                                                         \
    CALL(evt_btl_camera_set_mode, 0, 7)                                                                       \
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_SELF)                                     \
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)                                                                \
    CALL(evt_btl_camera_set_zoom, 0, 250)
#define PM64_CAM_TARGET                                                                                       \
    CALL(evt_btl_camera_set_mode, 0, 11)                                                                      \
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))                              \
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)                                                                \
    CALL(evt_btl_camera_set_zoom, 0, 300)
#define PM64_CAM_STAGE                                                                                        \
    CALL(evt_btl_camera_set_mode, 0, 0)                                                                       \
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)                                                                \
    CALL(evt_btl_camera_set_posoffset, 0, 0, 0, 0)
#define PM64_FACE_TARGET                                                                                      \
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))                       \
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
/* a dodged hit's reaction (LW(5): CommandPreCheckDamage's result) */
#define PM64_AVOID                                                                                            \
    IF_INT_EQUAL(LW(5), 3)                                                                                    \
        CALL(btlevtcmd_StartAvoid, LW(3), 38)                                                                 \
    END_IF                                                                                                    \
    IF_INT_EQUAL(LW(5), 6)                                                                                    \
        CALL(btlevtcmd_StartAvoid, LW(3), 39)                                                                 \
    END_IF                                                                                                    \
    IF_INT_EQUAL(LW(5), 2)                                                                                    \
        CALL(btlevtcmd_StartAvoid, LW(3), 40)                                                                 \
    END_IF
/* a single hit's end: the rating if it didn't go well, the audience's Star Power */
#define PM64_ONE_DONE                                                                                         \
    IF_INT_EQUAL(LW(15), 0)                                                                                   \
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)                                                   \
    END_IF                                                                                                    \
    CALL(btlevtcmd_StopAC)                                                                                    \
    CALL(evt_audience_ap_recovery)                                                                            \
    CALL(btlevtcmd_InviteApInfoReport)
/* home again, as row anim (flying, floating, running), at speed */
#define PM64_HOME(anim, speed)                                                                                \
    PM64_CAM_STAGE                                                                                            \
    CALL(pm64SetAnim, BTL_UNIT_SELF, anim, -1)                                                                \
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(speed))                                            \
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))                                            \
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
/* bounced up and back off the target, as row anim */
#define PM64_BOUNCE(anim, back, frames)                                                                       \
    CALL(pm64SetAnim, BTL_UNIT_SELF, anim, -1)                                                                \
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(0.5f))                                             \
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))                                                \
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), back)                                              \
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)                                \
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(frames), -1)

/* A move's damage set by its script this use (pm64GaugeDamage, pm64BombDamage): portPm64BattleCosts re-wears
 * the worn weapons every frame, so wear() lays this back on until PM64_END lets it go (hits frames apart, Mega
 * Bomb's, Air Raid's, keep it). */
static struct {
    const BattleWeapon* w;
    u32 v[6];
    int owner; /* (pm64Owner: let go once that event is gone, a move cut short by a counter included) */
} sHeldDmg;

static void pm64HoldDamage(EventEntry* event, const BattleWeapon* w) {
    int i;
    sHeldDmg.w = w;
    sHeldDmg.owner = pm64Owner(event, evtUnit(event, BTL_UNIT_SELF));
    for (i = 0; i < 6; i++) {
        sHeldDmg.v[i] = w->hpDamageWork[i];
    }
}

static USER_FUNC(pm64ReleaseDamage) {
    sHeldDmg.w = NULL;
    sHeldDmg.owner = 0;
    return EVT_RETURN_DONE;
}

/* Each frame (portPm64BattleCosts): what a move left that its PM64_END never undid (the move's event ended
 * early: a counter, a superguard, a battle torn down) let go. Out of battle all of it goes. */
static void pm64MovesFrame(void) {
    const int out = gp == NULL || !gp->inBattle || _battleWorkPointer == NULL;
    int i;
    for (i = 0; i < (int)(sizeof(sForced) / sizeof(sForced[0])); i++) {
        if (sForced[i].unit != NULL) {
            if (out || sForced[i].owner == 0 || !evtCheckID(sForced[i].owner)) {
                if (sForced[i].anim >= 0 && pm64Log()) {
                    printf("[pm64] row %d left by an ended move cleared%s\n", sForced[i].anim, out ? " (battle over)" : "");
                    fflush(stdout);
                }
                forcedClear(i);
            }
        }
    }
    {
        /* (the frame a battle starts or ends: a battle's effects were all freed with it (effAutoRelease), so a
           held one is only forgotten, never deleted: its pool entry and userdata may be another effect's now) */
        static int sWasIn = -1;
        const int in = gp != NULL && gp->inBattle;
        if (in != sWasIn) {
            sWasIn = in;
            for (i = 0; i < HELD_COUNT; i++) {
                heldForget(i);
            }
        }
    }
    for (i = 0; i < HELD_COUNT; i++) {
        /* (one that ended on its own is forgotten at once, the first frame its entry is free or another's: a later
           effect of the same type that takes the entry and the same userdata is not mistaken for it) */
        if (sHeld[i].e != NULL && !heldOurs(i)) {
            heldForget(i);
        }
        if (sHeld[i].e != NULL && (out || sHeld[i].owner == 0 || !evtCheckID(sHeld[i].owner))) {
            heldEnd(i);
        }
    }
    if (sHeldDmg.w != NULL && (out || sHeldDmg.owner == 0 || !evtCheckID(sHeldDmg.owner))) {
        sHeldDmg.w = NULL;
        sHeldDmg.owner = 0;
    }
    if (out) {
        for (i = 0; i < (int)(sizeof(sShock) / sizeof(sShock[0])); i++) {
            sShock[i].e = NULL;
        }
    }
}

static int selectedMove(void);

/* the unit is a partner using a worn PM64 move: any side's (a versus fight's other side included), not only
 * the alliance-0 partner's */
static int pm64MoveUser(const BattleUnit* unit) {
    return unit->currentKind >= TYPE_PARTNER_MIN && unit->currentKind < TYPE_PARTNER_MAX && selectedMove() >= 0;
}

/* btlevtcmd_CheckDamage / CommandCheckDamage: the attacker's event was ended by the hit (a counter: spiky,
 * fiery, electric, poison, Payback, ...), so a PM64 move's PM64_END will not run. Its leftovers go now, before
 * the hurt the counter plays: the row (the hurt pose shows), the held effects and damage, and the move's
 * offset, turn, size and fade back to plain (TTYD's own partners are reset the same way by their hurt). */
void portPm64MoveCut(BattleUnit* attacker) {
    BattleUnitPart* part;
    int i;
    if (attacker == NULL || _battleWorkPointer == NULL || !pm64MoveUser(attacker)) {
        return;
    }
    if (pm64Log()) {
        printf("[pm64] move cut short by a counter: its leftovers cleared\n");
        fflush(stdout);
    }
    i = forcedSlot(attacker, 0);
    if (i >= 0) {
        forcedClear(i);
    }
    for (i = 0; i < HELD_COUNT; i++) {
        heldEnd(i);
    }
    sHeldDmg.w = NULL;
    sHeldDmg.owner = 0;
    BtlUnit_SetDispOffset(attacker, 0.0f, 0.0f, 0.0f);
    BtlUnit_SetRotate(attacker, 0.0f, 0.0f, 0.0f);
    BtlUnit_SetRotateOffset(attacker, 0.0f, 0.0f, 0.0f);
    BtlUnit_SetScale(attacker, 1.0f, 1.0f, 1.0f);
    part = BtlUnit_GetPartsPtr(attacker, 1);
    if (part != NULL) {
        part->color.a = 255;
    }
}

/* pm64GaugeDamage(weapon, fill 0-100, base, per): the weapon's damage this time, by how full the gauge got (PM64's
 * moves whose damage is the meter's, not the rank's): base + fill / per, on every rank and result */
static USER_FUNC(pm64GaugeDamage) {
    BattleWeapon* w = (BattleWeapon*)evtGetValue(event, event->args[0]);
    const int fill = evtGetValue(event, event->args[1]);
    const int base = evtGetValue(event, event->args[2]);
    const int per = evtGetValue(event, event->args[3]);
    int i;
    if (w != NULL && w->hpDamageFn == weaponGetPowerFromPartyAttackLv && per > 0) {
        for (i = 0; i < 6; i++) {
            w->hpDamageWork[i] = (u32)(base + (fill < 0 ? 0 : fill > 100 ? 100 : fill) / per);
        }
        pm64HoldDamage(event, w);
    }
    return EVT_RETURN_DONE;
}

static int weaponRank(const BattleWeapon* w);

/* pm64WeaponRank(weapon, out): the rank (0-2) of the PM64 partner using it */
static USER_FUNC(pm64WeaponRank) {
    evtSetValue(event, event->args[1], weaponRank((const BattleWeapon*)evtGetValue(event, event->args[0])));
    return EVT_RETURN_DONE;
}

/* --- each partner's own move scripts, one file each in pm64_moves/ */
#include "pm64_moves/goombario.inc"
#include "pm64_moves/kooper.inc"
#include "pm64_moves/bombette.inc"
#include "pm64_moves/parakarry.inc"
#include "pm64_moves/watt.inc"
#include "pm64_moves/sushie.inc"
#include "pm64_moves/lakilester.inc"

/* --- Bow (on Vivian): Smack and Fan Smack (Shade Fist, Infatuate), Outta Sight (Veil), Spook (Fiery Jinx).
 * Each follows PM64's own battle script for her (battle_partner_bow in the PM64 decompilation: smack, fanSmack,
 * outtaSight with hidePlayer, spook), re-made in TTYD's script commands. PM64's battles run 30 frames a second
 * and TTYD's 60, so every PM64 wait and flight here is twice as many frames, every per-frame step half. Her
 * battle sprite's animations (PM64's BattleBow list): */
#define BOW_STILL 0
#define BOW_IDLE 1
#define BOW_WALK 2 /* (her standing in battle: PM64's DefaultAnims) */
#define BOW_RUN 3  /* (flying) */
#define BOW_PRESLAP 5
#define BOW_SLAP_ONCE 7
#define BOW_SLAP_BACK 8
#define BOW_FAN_ONCE 10
#define BOW_FAN_BACK 11
#define BOW_CONCEAL 12
#define BOW_SPOOK_LOOP 14
#define BOW_CELEBRATE 17
#define BOW_CELEBRATE_FAST 18
#define BOW_INJURED 26

/* pm64BowSlaps(progress, rank, fan, out): the slaps PM64's Smack command earns (action_cmd/smack: the result
 * tier, how many of the rank's thresholds the meter passed; PM64's scripts slap tier + 1 times):
 * Normal 40/70/99, Super 35/60/80/99, Ultra 35/35/60/80/99 (Fan Smack at Ultra 35/60/80/99) */
static USER_FUNC(pm64BowSlaps) {
    static const signed char kNormal[] = {40, 70, 99, -1}, kSuper[] = {35, 60, 80, 99, -1};
    static const signed char kUltra[] = {35, 35, 60, 80, 99, -1};
    const int progress = evtGetValue(event, event->args[0]), rank = evtGetValue(event, event->args[1]);
    const int fan = evtGetValue(event, event->args[2]);
    const signed char* t = rank <= 0 ? kNormal : rank == 1 || fan ? kSuper : kUltra;
    int tier = 0;
    (void)isFirstCall;
    while (t[tier] >= 0 && progress >= t[tier]) {
        tier++;
    }
    if (pm64Log()) {
        printf("[pm64] Bow: %s, gauge %d, rank %d, %d slaps\n", fan ? "Fan Smack" : "Smack", progress, rank,
               tier + 1);
        fflush(stdout);
    }
    evtSetValue(event, event->args[3], tier + 1);
    return EVT_RETURN_DONE;
}

/* pm64Fly(unit, x, y, z, frames, arc, ease): PM64's FlyToGoal: the unit flies there in frames, arc units up at
 * the middle, eased in and out (ease 1, EASING_COS_IN_OUT) or not (0, EASING_LINEAR); the script waits */
static USER_FUNC(pm64Fly) {
    BattleUnit* unit = evtUnit(event, event->args[0]);
    const int frames = evtGetValue(event, event->args[4]);
    float t, k;
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        event->userdataf[0] = unit->position.x;
        event->userdataf[1] = unit->position.y;
        event->userdataf[2] = unit->position.z;
        event->userdata3 = 0;
    }
    event->userdata3++;
    t = frames > 0 ? (float)event->userdata3 / (float)frames : 1.0f;
    t = t > 1.0f ? 1.0f : t;
    k = evtGetValue(event, event->args[6]) ? 0.5f - 0.5f * cosf(3.14159265f * t) : t;
    {
        const float x = (float)evtGetValue(event, event->args[1]), y = (float)evtGetValue(event, event->args[2]);
        const float z = (float)evtGetValue(event, event->args[3]), arc = (float)evtGetValue(event, event->args[5]);
        BtlUnit_SetPos(unit, event->userdataf[0] + (x - event->userdataf[0]) * k,
                       event->userdataf[1] + (y - event->userdataf[1]) * k + arc * 4.0f * t * (1.0f - t),
                       event->userdataf[2] + (z - event->userdataf[2]) * k);
    }
    return t >= 1.0f ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* PM64's EVS_ReturnHome_Success / _Miss: she fades out (200 down by 10 a frame) as she flies home (Run, 30
 * frames, eased), then fades back in (55 up by 10 a frame), with the Boo sounds */
#define BOW_HOME                                                                                              \
    PM64_CAM_STAGE                                                                                            \
    CALL(evt_snd_sfxon, STRING("SOUND_BOO_VANISH_A|SFX_AUDIENCE_TELESA_INVISIBLE1"), 0)                                               \
    BEGIN_INLINE_EVENT                                                                                        \
        SET_VALUE(LW(0), 200)                                                                                 \
        LOOP(40)                                                                                              \
            SUBTRACT(LW(0), 5)                                                                                \
            CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, LW(0))                                                 \
            WAIT_FRAMES(1)                                                                                    \
        END_LOOP                                                                                              \
    END_INLINE_EVENT                                                                                          \
    WAIT_FRAMES(20)                                                                                           \
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_RUN, -1)                                                             \
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))                                            \
    CALL(pm64Fly, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 60, 0, 1)                                               \
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_WALK, -1)                                                            \
    CALL(evt_snd_sfxon, STRING("SOUND_BOO_APPEAR_A|SFX_STG4_TELESA_APPEAR1"), 0)                                                  \
    SET_VALUE(LW(0), 55)                                                                                      \
    LOOP(40)                                                                                                  \
        ADD(LW(0), 5)                                                                                         \
        CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, LW(0))                                                     \
        WAIT_FRAMES(1)                                                                                        \
    END_LOOP                                                                                                  \
    CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, 255)

/* PM64's smack command (action_cmd/smack): flick the stick left again and again to fill the meter (TTYD's
 * repeatedly-lv command on letting go of the stick held left, tuned as Koops' Shell Slam's, the TTYD move with
 * that command), for LW(9) frames; AcGetOutputParam 1: how full (%) */
#define BOW_AC_FLICK                                                                                          \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(1), 25)                                                                                  \
        SET_VALUE(LW(2), 15)                                                                                  \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(1), 24)                                                                                  \
        SET_VALUE(LW(2), 21)                                                                                  \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(1), 22)                                                                                  \
        SET_VALUE(LW(2), 24)                                                                                  \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(1), 20)                                                                                  \
        SET_VALUE(LW(2), 25)                                                                                  \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(1), 17)                                                                                  \
        SET_VALUE(LW(2), 27)                                                                                  \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(1), 15)                                                                                  \
        SET_VALUE(LW(2), 30)                                                                                  \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(1), 12)                                                                                  \
        SET_VALUE(LW(2), 30)                                                                                  \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, 7, LW(9), 178, LW(1), LW(2), 20, 100, EVT_NULLPTR)                          \
    CALL(btlevtcmd_AcSetGaugeParam, 35, 60, 80, 100)                                                          \
    CALL(btlevtcmd_AcSetFlag, 0)                                                                              \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 6, 1, 0)

/* Smack and Fan Smack (PM64 battle_partner_bow smack, fanSmack): while the command runs she vanishes (55) and
 * flies to the enemy (Run, 15 PM64 frames, eased), reappears (by 40 a frame), winds up (PreSlap; Fan Smack:
 * Celebrate, then CelebrateFast) shivering on the spot (half a unit each way), then slaps tier + 1 times, back
 * and forth (SlapOnce/SlapBack, FanSmackOnce/Back with confetti), each slap swelling her to 1.4 for 2 frames,
 * shaking the camera, 1 damage (Fan Smack 2) a slap and a Nice when the meter was full; Fan Smack spins her
 * after each. Should a slap miss, she spins, sees stars, Injured, and goes home. LW(13): Fan Smack. */
static EVT_BEGIN(sSmackEvt)
    PM64_BEGIN(PM64_ONE)
    SET_VALUE(LW(13), 0)
    SET_VALUE(LW(9), 114) /* (PM64: 60 - 3 frames) */
    IF_INT_EQUAL(LW(12), PTR(&partyWeapon_VivianCharmKissAttack))
        SET_VALUE(LW(13), 1)
        SET_VALUE(LW(9), 174) /* (90 - 3) */
    END_IF
    BOW_AC_FLICK
    WAIT_FRAMES(20)
    BEGIN_INLINE_EVENT
        WAIT_FRAMES(20)
        CALL(btlevtcmd_StartAC, 1)
    END_INLINE_EVENT
    CALL(evt_snd_sfxon, STRING("SOUND_BOO_VANISH_A|SFX_AUDIENCE_TELESA_INVISIBLE1"), 0)
    CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, 55)
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_RUN, -1)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetWidth, LW(3), LW(5))
    DIVIDE(LW(5), 2)
    IF_INT_EQUAL(LW(13), 1)
        ADD(LW(5), 25)
    ELSE
        ADD(LW(5), 15)
    END_IF
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), LW(5))
    ADD(LW(2), 5)
    BEGIN_INLINE_EVENT
        CALL(pm64Fly, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 30, 0, 1)
        IF_INT_EQUAL(LW(13), 0)
            CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_IDLE, -1)
        END_IF
    END_INLINE_EVENT
    PM64_CAM_TARGET
    WAIT_FRAMES(30)
    CALL(evt_snd_sfxon, STRING("SOUND_BOO_APPEAR_A|SFX_STG4_TELESA_APPEAR1"), 0)
    SET_VALUE(LW(0), 55)
    LOOP(10)
        ADD(LW(0), 20)
        CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, LW(0))
        WAIT_FRAMES(1)
    END_LOOP
    CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, 255)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    IF_INT_EQUAL(LW(13), 1)
        CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_CELEBRATE, -1)
    ELSE
        CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_PRESLAP, -1)
    END_IF
    WAIT_FRAMES(40)
    IF_INT_EQUAL(LW(13), 1)
        CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_CELEBRATE_FAST, -1)
        SET_VALUE(LW(0), 60)
    ELSE
        SET_VALUE(LW(0), 30)
    END_IF
    LOOP(LW(0))
        CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, FLOAT(0.5f), 0, 0)
        WAIT_FRAMES(2)
        CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, FLOAT(-0.5f), 0, 0)
        WAIT_FRAMES(2)
    END_LOOP
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    /* (the command is over by now: its meter decides the slaps) */
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(0))
    CALL(btlevtcmd_GetPartyTechLv, BTL_UNIT_SELF, LW(1))
    CALL(pm64BowSlaps, LW(0), LW(1), LW(13), LW(10))
    SET_VALUE(LW(7), 0)
    IF_INT_GREATER_OR_EQUAL(LW(0), 100)
        SET_VALUE(LW(7), DAMAGE_FLAG_AC_SUCCESS) /* (PM64: a Nice at a full meter) */
    END_IF
    CALL(btlevtcmd_StopAC)
    SET_VALUE(LW(11), 0)
    LABEL(10)
    /* (the accuracy is rolled once, for the first slap; the rest land unless the target can't be touched) */
    IF_INT_EQUAL(LW(11), 0)
        CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    ELSE
        CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_ALWAYS_HIT, LW(5))
        IF_INT_NOT_EQUAL(LW(5), 1)
            CALL(pm64MissLog, LW(3), LW(5))
            GOTO(20) /* (a slap has landed: the rating, and home) */
        END_IF
    END_IF
    IF_INT_NOT_EQUAL(LW(5), 1)
        /* missed: she spins off, dazed */
        PM64_AVOID
        IF_INT_EQUAL(LW(13), 1)
            CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_FAN_ONCE, -1)
        ELSE
            CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_SLAP_ONCE, -1)
        END_IF
        SET_VALUE(LW(0), 0)
        LOOP(40)
            SUBTRACT(LW(0), 23)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
        CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_INJURED, -1)
        CALL(pm64Fx, 11, BTL_UNIT_SELF, 30)
        WAIT_FRAMES(60)
        CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_IDLE, -1)
        WAIT_FRAMES(20)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        CALL(evt_audience_ap_recovery)
        CALL(btlevtcmd_InviteApInfoReport)
        GOTO(95)
    END_IF
    ADD(LW(11), 1)
    /* the slap: swelling, back and forth */
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.4f), FLOAT(1.4f), FLOAT(1.0f))
    SET_VALUE(LW(0), LW(11))
    MODULO(LW(0), 2)
    IF_INT_EQUAL(LW(13), 1)
        IF_INT_EQUAL(LW(0), 1)
            CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_FAN_ONCE, -1)
        ELSE
            CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_FAN_BACK, -1)
        END_IF
        CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
        CALL(pm64Fx, 18, LW(3), 0)
        CALL(evt_snd_sfxon, STRING("SOUND_BOW_FAN_SMACK|SFX_BTL_CHUCHURINA_ATTACK2"), 0)
    ELSE
        IF_INT_EQUAL(LW(0), 1)
            CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_SLAP_ONCE, -1)
        ELSE
            CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_SLAP_BACK, -1)
        END_IF
        CALL(evt_snd_sfxon, STRING("SOUND_BOW_SMACK|SFX_BTL_CHUCHURINA_ATTACK1"), 0)
    END_IF
    WAIT_FRAMES(4)
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    CALL(evt_btl_camera_shake_h, 0, 2, 0, 12, 13)
    /* (CheckDamage takes its flags as written, not a variable's value: one call for each. Every slap is a last
     * hit, so the one that takes the target to 0 knocks it out, and the slaps stop there) */
    IF_INT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    ELSE
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
    END_IF
    CALL(pm64UnitHp, LW(3), LW(0))
    IF_INT_LESS_OR_EQUAL(LW(0), 0)
        SET_VALUE(LW(10), LW(11))
    END_IF
    IF_INT_EQUAL(LW(13), 1)
        /* Fan Smack: a whirl after each (8 PM64 frames, 144 degrees a frame; the last one twice) */
        SET_VALUE(LW(0), 0)
        LOOP(16)
            ADD(LW(0), 72)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
            WAIT_FRAMES(1)
        END_LOOP
        IF_INT_GREATER_OR_EQUAL(LW(11), LW(10))
            LOOP(16)
                ADD(LW(0), 36)
                CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
                WAIT_FRAMES(1)
            END_LOOP
        END_IF
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    ELSE
        WAIT_FRAMES(16)
    END_IF
    IF_INT_LESS(LW(11), LW(10))
        CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_IDLE, -1)
        GOTO(10)
    END_IF
    LABEL(20)
    IF_INT_GREATER_OR_EQUAL(LW(7), 1)
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    WAIT_FRAMES(20)
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_IDLE, -1)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(95)
    BOW_HOME
    PM64_END
EVT_END()

/* Outta Sight (PM64 battle_partner_bow outtaSight; hidePlayer at the next turn's start). No command: she rises
 * a little turning round (10 frames), flies over to Mario in an arc (20, 30 up), turns back and fades to 105
 * on her way down over him (Mario + (-10, 20) by his size); Mario fades to 105 too. Then, through the enemies'
 * turn, every attack at him misses (TTYD's Invisible) and she can't be reached (veiled). As the next turn
 * starts, the two fade back in (by 5 a frame from 105) and she flies home; that turn is hers no more (PM64's
 * PARTNER_TURN_USED: Veil's skipped turn here). Used before Mario's own act, Mario can't act this turn either.
 * sBowRest*: the event that waits for the next turn (it outlives her move). */
static int sBowRestUnit = -1;
static int sBowRestEvtId = -1;
static int sBowRestTurn;

int portPm64DrawVeiled(const void* u) {
    const BattleUnit* unit = (const BattleUnit*)u;
    return unit != NULL && unit->currentKind == UNIT_VIVIAN && portPm64PartnerOn(6) == PM64_PARTNER_BOW;
}

/* pm64BowHide(on): Mario hidden (Invisible until the next turn starts) or out again; on 1 also ends Mario's
 * turn when he hasn't acted yet (PM64: playerCantMove while Outta Sight is up) */
static USER_FUNC(pm64BowHide) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* mario = wp != NULL ? BattleGetMarioPtr(wp) : NULL;
    const int on = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (mario == NULL) {
        return EVT_RETURN_DONE;
    }
    if (on) {
        BtlUnit_SetStatus(mario, STATUS_INVISIBLE, 99, 0);
        if (mario->movesRemaining > 0) {
            mario->movesRemaining = 0;
            if (pm64Log()) {
                printf("[pm64] Bow: Outta Sight before Mario's act: his turn is over\n");
                fflush(stdout);
            }
        }
        sBowRestUnit = BattleTransID(event, BTL_UNIT_SELF);
        sBowRestTurn = wp->turnCount;
        if (pm64Log()) {
            printf("[pm64] Bow: Outta Sight on (turn %d: Mario invisible, Bow veiled)\n", (int)wp->turnCount);
            fflush(stdout);
        }
    } else {
        BtlUnit_SetStatus(mario, STATUS_INVISIBLE, 0, 0);
    }
    return EVT_RETURN_DONE;
}

/* pm64BowRestWait(stage, out ok, out unit): stage 0 blocks until the next turn starts, stage 1 until her
 * skipped turn has passed; ok 0 when the battle or she is gone first */
static USER_FUNC(pm64BowRestWait) {
    BattleWork* wp = _battleWorkPointer;
    const BattleUnit* unit;
    const int stage = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (gp == NULL || !gp->inBattle || wp == NULL || sBowRestUnit < 0 ||
        (unit = BattleGetUnitPtr(wp, sBowRestUnit)) == NULL || unit->currentKind != UNIT_VIVIAN ||
        !(unit->attributeFlags & UNIT_ATTR_VEILED)) {
        if (pm64Log()) {
            printf("[pm64] Bow: Outta Sight's rest ended early (battle over or she left)\n");
            fflush(stdout);
        }
        evtSetValue(event, event->args[1], 0);
        sBowRestEvtId = -1;
        return EVT_RETURN_DONE;
    }
    if (stage == 0 ? wp->turnCount == sBowRestTurn : unit->work[UW_VIVIAN_VEIL_TURNS] >= 2) {
        return EVT_RETURN_BLOCK;
    }
    if (pm64Log()) {
        printf(stage == 0 ? "[pm64] Bow: Outta Sight over (turn %d starts): back out\n"
                          : "[pm64] Bow: her turn skipped (turn %d)\n",
               (int)wp->turnCount);
        fflush(stdout);
    }
    evtSetValue(event, event->args[1], 1);
    evtSetValue(event, event->args[2], sBowRestUnit);
    if (stage == 1) {
        sBowRestEvtId = -1;
    }
    return EVT_RETURN_DONE;
}

/* PM64's hidePlayer: wait 20, then both fade in from 105 (Mario 10 frames later) and she flies home (20) */
static EVT_BEGIN(sBowRestEvt)
    CALL(pm64BowRestWait, 0, LW(0), LW(10))
    IF_INT_EQUAL(LW(0), 0)
        CALL(pm64BowHide, 0)
        RETURN
    END_IF
    WAIT_FRAMES(40)
    CALL(evt_snd_sfxon, STRING("SOUND_BOW_APPEAR|SFX_STG4_TELESA_APPEAR1"), 0)
    BEGIN_INLINE_EVENT
        WAIT_FRAMES(20)
        CALL(pm64BowHide, 0)
        CALL(btlevtcmd_SetAlpha, -3, 1, 210) /* (Invisible's half: 105, where PM64's fade starts) */
        WAIT_FRAMES(1)
        CALL(btlevtcmd_SetAlpha, -3, 1, 105)
        SET_VALUE(LW(1), 105)
        LOOP(60)
            ADD(LW(1), 2)
            IF_INT_GREATER(LW(1), 255)
                SET_VALUE(LW(1), 255)
            END_IF
            CALL(btlevtcmd_SetAlpha, -3, 1, LW(1))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetAlpha, -3, 1, 255)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        SET_VALUE(LW(1), 105)
        LOOP(60)
            ADD(LW(1), 2)
            IF_INT_GREATER(LW(1), 255)
                SET_VALUE(LW(1), 255)
            END_IF
            CALL(btlevtcmd_SetAlpha, LW(10), 1, LW(1))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetAlpha, LW(10), 1, 255)
    END_INLINE_EVENT
    CALL(pm64SetAnim, LW(10), BOW_RUN, -1)
    CALL(btlevtcmd_GetHomePos, LW(10), LW(0), LW(1), LW(2))
    CALL(pm64Fly, LW(10), LW(0), LW(1), LW(2), 40, 0, 1)
    CALL(pm64SetAnim, LW(10), -1, -1)
    CALL(btlevtcmd_ResetFaceDirection, LW(10))
    /* her turn this round is used: veiled until it comes, then plain again */
    CALL(pm64BowRestWait, 1, LW(0), LW(10))
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(btlevtcmd_OffAttribute, LW(10), UNIT_ATTR_VEILED)
    CALL(btlevtcmd_SetUnitWork, LW(10), UW_VIVIAN_VEIL_TURNS, 0)
    CALL(btlevtcmd_SetAlpha, LW(10), 1, 255)
    RETURN
EVT_END()

/* pm64BowRestStart: the rest's watch, an event of its own (one at a time) */
static USER_FUNC(pm64BowRestStart) {
    EventEntry* e;
    int i;
    (void)isFirstCall;
    if (sBowRestEvtId < 0 || !evtCheckID(sBowRestEvtId)) {
        e = evtEntry(sBowRestEvt, 10, 0);
        sBowRestEvtId = e != NULL ? e->eventId : -1;
    }
    /* (her row over Mario, Walk, outlives the move: the rest's from now, dropped if it ends early) */
    i = forcedSlot(evtUnit(event, BTL_UNIT_SELF), 0);
    if (i >= 0 && sBowRestEvtId >= 0) {
        sForced[i].owner = sBowRestEvtId;
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(sOuttaSightEvt)
    SET_VALUE(LW(3), -3)
    CALL(btlevtcmd_GetBodyId, LW(3), LW(4))
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 260)
    /* up a little, turning round */
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_RUN, -1)
    BEGIN_INLINE_EVENT
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 5)
        CALL(pm64Fly, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 20, 0, 1)
    END_INLINE_EVENT
    SET_VALUE(LW(0), 0)
    LOOP(20)
        ADD(LW(0), 9)
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
        WAIT_FRAMES(1)
    END_LOOP
    WAIT_FRAMES(20)
    /* over to Mario, in an arc */
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 30)
    ADD(LW(1), 30)
    CALL(pm64Fly, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 40, 30, 1)
    /* turning back and fading as she comes down over him */
    BEGIN_INLINE_EVENT
        SET_VALUE(LW(0), 180)
        LOOP(20)
            SUBTRACT(LW(0), 9)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
            WAIT_FRAMES(1)
        END_LOOP
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        SET_VALUE(LW(0), 255)
        LOOP(60)
            SUBTRACT(LW(0), 2)
            IF_INT_LESS(LW(0), 105)
                SET_VALUE(LW(0), 105)
            END_IF
            CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, LW(0))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, 105)
    END_INLINE_EVENT
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 10)
    ADD(LW(1), 20)
    ADD(LW(2), 5)
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_WALK, -1)
    CALL(pm64Fly, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 40, 0, 0)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    /* Mario fades under her */
    CALL(evt_snd_sfxon, STRING("SOUND_BOW_VANISH|SFX_BTL_VIVIAN_SHADOW1_2"), 0)
    SET_VALUE(LW(0), 255)
    LOOP(30)
        SUBTRACT(LW(0), 5)
        CALL(btlevtcmd_SetAlpha, -3, 1, LW(0))
        WAIT_FRAMES(1)
    END_LOOP
    /* (TTYD's Invisible halves him: 210 at half is PM64's 105) */
    CALL(pm64BowHide, 1)
    CALL(pm64BowRestStart)
    CALL(btlevtcmd_SetAlpha, -3, 1, 210)
    CALL(btlevtcmd_OnAttribute, BTL_UNIT_SELF, UNIT_ATTR_VEILED)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_VIVIAN_VEIL_TURNS, 2)
    CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    WAIT_FRAMES(30)
    PM64_CAM_STAGE
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Spook (PM64 battle_partner_bow spook): she floats forward and up (+40, +10; 20 PM64 frames) and, while the
 * stick is flicked left (the same meter, 87 frames, 100 at the top), conceals herself, shivering harder near
 * the end; then the scare: SpookLoop, fading to 105, swelling by 0.3 to 0.7 a frame for 5 frames by how full
 * the meter was, then throbbing; each enemy may flee (PM64's fear: the meter's % as the chance, through each
 * enemy's own resistance; a full meter a Nice); she shrinks back, opaque again, and flies home. */
static EVT_BEGIN(sSpookEvt)
    PM64_BEGIN(PM64_ALL)
    SET_VALUE(LW(9), 174)
    BOW_AC_FLICK
    WAIT_FRAMES(20)
    PM64_CAM_SELF
    CALL(evt_btl_camera_set_zoom, 0, 300)
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_RUN, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
    ADD(LW(1), 10)
    CALL(pm64Fly, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 40, 0, 1)
    WAIT_FRAMES(30)
    BEGIN_INLINE_EVENT
        WAIT_FRAMES(148)
        LOOP(8)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 1, 0, 0)
            WAIT_FRAMES(2)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, -1, 0, 0)
            WAIT_FRAMES(2)
        END_LOOP
        LOOP(7)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 2, 0, 0)
            WAIT_FRAMES(2)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, -2, 0, 0)
            WAIT_FRAMES(2)
        END_LOOP
        LOOP(8)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 3, 0, 0)
            WAIT_FRAMES(2)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, -3, 0, 0)
            WAIT_FRAMES(2)
        END_LOOP
        CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    END_INLINE_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_CONCEAL, -1)
    /* the camera closes in as she gathers herself (PM64: 20 nearer each tenth of the meter) */
    SET_VALUE(LW(9), 300)
    LOOP(9)
        WAIT_FRAMES(20)
        CALL(evt_btl_camera_set_zoom, 0, LW(9))
        SUBTRACT(LW(9), 15)
    END_LOOP
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(14))
    CALL(btlevtcmd_StopAC)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    /* the scare */
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_SPOOK_LOOP, -1)
    CALL(evt_btl_camera_set_zoom, 0, 300)
    CALL(evt_btl_camera_shake_h, 0, 4, 0, 20, 13)
    BEGIN_INLINE_EVENT
        SET_VALUE(LW(0), 255)
        LOOP(30)
            SUBTRACT(LW(0), 5)
            CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, LW(0))
            WAIT_FRAMES(1)
        END_LOOP
    END_INLINE_EVENT
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_BOW_SPOOK|SFX_ITEM_HORROR1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    /* swelling by how full the meter was: 1 + 5 x 0.3 .. 0.7 */
    SWITCH(LW(14))
        CASE_LESS_OR_EQUAL(20)
        SET_FLOAT_VALUE(LW(13), FLOAT(0.15f))
        CASE_LESS_OR_EQUAL(40)
        SET_FLOAT_VALUE(LW(13), FLOAT(0.2f))
        CASE_LESS_OR_EQUAL(60)
        SET_FLOAT_VALUE(LW(13), FLOAT(0.25f))
        CASE_LESS_OR_EQUAL(80)
        SET_FLOAT_VALUE(LW(13), FLOAT(0.3f))
        CASE_DEFAULT
        SET_FLOAT_VALUE(LW(13), FLOAT(0.35f))
    END_SWITCH
    SET_FLOAT_VALUE(LW(8), FLOAT(1.0f))
    LOOP(10)
        ADD_FLOAT(LW(8), LW(13))
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, LW(8), LW(8), LW(8))
        WAIT_FRAMES(1)
    END_LOOP
    SET_FLOAT_VALUE(LW(11), LW(8))
    BEGIN_INLINE_EVENT
        LOOP(5)
            LOOP(6)
                SUBTRACT_FLOAT(LW(11), FLOAT(0.115f))
                CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, LW(11), LW(11), LW(11))
                WAIT_FRAMES(1)
            END_LOOP
            LOOP(6)
                ADD_FLOAT(LW(11), FLOAT(0.115f))
                CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, LW(11), LW(11), LW(11))
                WAIT_FRAMES(1)
            END_LOOP
        END_LOOP
    END_INLINE_EVENT
    /* (bow.c spook: EFFECT_ENERGY_IN_OUT twice on her, 32 up, as she swells) */
    CALL(pm64Fx, 20, BTL_UNIT_SELF, 32)
    WAIT_FRAMES(30)
    /* each enemy: the fear (PM64: the meter's % as the chance) */
    CALL(pm64StatusWeapon, LW(12), 3, LW(14))
    SET_VALUE(LW(15), 0)
    LABEL(10)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_EQUAL(LW(5), 1)
        IF_INT_GREATER_OR_EQUAL(LW(14), 100)
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
            ADD(LW(15), 1)
        ELSE
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        END_IF
    ELSE
        PM64_AVOID
    END_IF
    WAIT_FRAMES(10)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(10)
    END_IF
    IF_INT_GREATER(LW(15), 0)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_NONE, 0, LW(7))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 30)
        CALL(btlevtcmd_ACSuccessEffect, LW(7), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(7))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    PM64_CAM_STAGE
    WAIT_FRAMES(60)
    BEGIN_INLINE_EVENT
        SET_VALUE(LW(0), 105)
        LOOP(30)
            ADD(LW(0), 5)
            CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, LW(0))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetAlpha, BTL_UNIT_SELF, 1, 255)
    END_INLINE_EVENT
    LOOP(10)
        SUBTRACT_FLOAT(LW(8), LW(13))
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, LW(8), LW(8), LW(8))
        WAIT_FRAMES(1)
    END_LOOP
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    CALL(pm64SetAnim, BTL_UNIT_SELF, BOW_RUN, -1)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(pm64Fly, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 30, 0, 1)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    PM64_END
EVT_END()

/* tests: TTYD_PM64_LOG=1 (as pm64_field_pc.c's) */
static int pm64Log(void) {
    static int sOn = -1;
    if (sOn < 0) {
        const char* v = getenv("TTYD_PM64_LOG");
        sOn = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    return sOn;
}

typedef struct {
    int p;
    BattleWeapon* weapon;
    int move;
    signed char dmg[6];
    unsigned short fx;
    BattleWeapon* base; /* another host move whose script, targets and damage it takes (NULL: its own) */
    unsigned char buff; /* BUFF_*: the port's helping script instead */
    const EvtWord* script; /* the port's own script for it (PM64's choreography), NULL the host's */
    const BattleWeapon* help; /* the TTYD move whose action-command help text fits the script's command */
    unsigned char targets;    /* TARGETS_*: the script's targets, over the weapon's */
    char kept;
    BattleWeapon own;     /* the host's weapon as the game has it */
    BattleWeapon baseOwn; /* base's, as the game has it */
} MoveMap;

static MoveMap sMoves[] = {
    /* Headbonk: Headbonk's weapon, the port's leap and bounce (PM64's 1 / 2 / 3 a hit by rank, two hits in time) */
    {PM64_PARTNER_GOOMBARIO, &partyWeapon_ChristineNormalAttack, 0, {1, 1, 2, 2, 3, 3}, FX_JUMP, NULL, BUFF_NONE, sHeadbonkEvt,
     &partyWeapon_ChristineNormalAttack, 0},
    /* Tattle: Tattle's weapon, the port's (PM64's: no action command) */
    {PM64_PARTNER_GOOMBARIO, &partyWeapon_ChristineMonosiri, 1, {0}, 0, NULL, BUFF_NONE, sTattleEvt, &sGoombarioNoHelp, 0},
    /* Multibonk: Multibonk's weapon, the port's bounces (PM64's 3 each) */
    {PM64_PARTNER_GOOMBARIO, &partyWeapon_ChristineRenzokuAttack, 3, {3, 3, 3, 3, 3, 3}, FX_JUMP, NULL, BUFF_NONE,
     sMultibonkEvt, &partyWeapon_ChristineRenzokuAttack, 0},
    {PM64_PARTNER_GOOMBARIO, &partyWeapon_ChristineKiss, 2, {0}, 0, NULL, BUFF_CHARGE_SELF}, /* Charge: Rally Wink */
    /* Shell Toss: Shell Toss's weapon and hold-left command, the port's toss (PM64's damage by rank) */
    {PM64_PARTNER_KOOPER, &partyWeapon_NokotarouKouraAttack, 0, {1, 2, 2, 3, 3, 5}, 0, NULL, BUFF_NONE, sShellTossEvt,
     &partyWeapon_NokotarouKouraAttack, 0},
    /* Power Shell: Power Shell's weapon (every enemy on the ground), Shell Toss's command (PM64's same hammer one) */
    {PM64_PARTNER_KOOPER, &partyWeapon_NokotarouSyubibinKoura, 1, {1, 2, 1, 3, 1, 4}, 0, NULL, BUFF_NONE,
     sPowerShellEvt, &partyWeapon_NokotarouKouraAttack, 0},
    /* Dizzy Shell: Shell Shield's slot on Shell Slam's targets (the enemies on the ground), no damage, the port's
       spin (the meter's dizzy chance: kooperDizzyWeapon) */
    {PM64_PARTNER_KOOPER, &partyWeapon_NokotarouKouraGuard, 2, {0}, FX_DIZZY, &partyWeapon_NokotarouTsuranukiKoura,
     BUFF_NONE, sDizzyShellEvt, &partyWeapon_SandersSuperBombAttack, 0},
    /* Fire Shell: Shell Slam's weapon, the port's (the meter's damage: kooperFireDamage) */
    {PM64_PARTNER_KOOPER, &partyWeapon_NokotarouTsuranukiKoura, 3, {1, 6, 1, 6, 1, 6}, FX_FIRE, NULL, BUFF_NONE,
     sFireShellEvt, &partyWeapon_SandersSuperBombAttack, 0},
    /* (Bobbery's free Bomb as Bombette's free Body Slam, not her 3 FP Bomb: she had no free move) */
    /* Body Slam: Bomb's weapon, the port's ram (PM64's damage by rank) */
    {PM64_PARTNER_BOMBETTE, &partyWeapon_SandersNormalAttack, 0, {1, 2, 2, 3, 3, 5}, FX_CONTACT, NULL, BUFF_NONE, sBodySlamEvt},
    /* Bomb: Bomb Squad's slot on Bobbery's Bomb's targets (one enemy), the port's run and blast (the meter's
       damage: pm64BombDamage) */
    {PM64_PARTNER_BOMBETTE, &partyWeapon_SandersTimeBombSet, 1, {1, 5, 1, 6, 1, 7}, FX_BLAST, &partyWeapon_SandersNormalAttack,
     BUFF_NONE, sBombEvt, &partyWeapon_SandersSuperBombAttack, 0},
    /* Power Bomb: Hold Fast's slot on Bob-ombast's targets (the enemies on the ground), the port's */
    {PM64_PARTNER_BOMBETTE, &partyWeapon_SandersCounterSet, 2, {1, 6, 1, 6, 1, 7}, FX_GROUND | FX_BLAST,
     &partyWeapon_SandersSuperBombAttack, BUFF_NONE, sPowerBombEvt, &partyWeapon_SandersSuperBombAttack, 0},
    /* Mega Bomb: Bob-ombast's, the port's */
    {PM64_PARTNER_BOMBETTE, &partyWeapon_SandersSuperBombAttack, 3, {1, 7, 1, 7, 1, 7}, FX_BLAST, NULL, BUFF_NONE, sMegaBombEvt, &partyWeapon_SandersSuperBombAttack, 0},
    /* Sky Dive: Ground Pound's weapon, the port's swoop onto one enemy (PM64's damage by rank) */
    {PM64_PARTNER_PARAKARRY, &partyWeapon_YoshiNormalAttack, 0, {1, 2, 2, 3, 4, 5}, 0, NULL, BUFF_NONE, sSkyDiveEvt, &partyWeapon_ChristineNormalAttack, 0},
    /* Air Lift: Gulp's slot on Gale Force's weapon at one enemy (no damage; carried off), the port's lift */
    {PM64_PARTNER_PARAKARRY, &partyWeapon_YoshiNomikomi_Shot, 2, {0}, FX_SINGLE, &partyWeapon_ClaudaBreathAttack,
     BUFF_NONE, sAirLiftEvt, &partyWeapon_SandersSuperBombAttack, 0},
    /* Shell Shot: Mini-Egg's weapon, the port's winged shell into one enemy */
    {PM64_PARTNER_PARAKARRY, &partyWeapon_YoshiEggAttack_Minimini, 1, {0, 5, 0, 6, 0, 7}, 0, NULL, BUFF_NONE, sShellShotEvt, &partyWeapon_NokotarouKouraAttack, TARGETS_ANY_ONE},
    /* Air Raid: Stampede's weapon, the port's zoom over every enemy */
    {PM64_PARTNER_PARAKARRY, &partyWeapon_YoshiCallGuard, 3, {2, 6, 2, 6, 2, 6}, 0, NULL, BUFF_NONE, sAirRaidEvt, &partyWeapon_SandersSuperBombAttack, TARGETS_ALL},
    /* Lakilester on Flurrie (her four slots, the ranks they open at as Yoshi's were: Body Slam and Gale Force
       first, Lip Lock at Super, Dodgy Fog at Ultra) */
    /* Spiny Flip: Body Slam's slot on Ground Pound's weapon (one enemy, a throw: no contact), the port's aimed throw
       from where he floats (PM64's damage by rank, only on a hit aim); Body Slam's sight */
    {PM64_PARTNER_LAKILESTER, &partyWeapon_ClaudaNormalAttack, 0, {0, 3, 0, 4, 0, 5}, 0, &partyWeapon_YoshiNormalAttack,
     BUFF_NONE, sSpinyFlipEvt, &partyWeapon_ClaudaNormalAttack, TARGETS_NO_CEILING},
    /* Hurricane: Gale Force's weapon (no damage; blown away), the port's breath; Smooch's A-and-B gauge */
    {PM64_PARTNER_LAKILESTER, &partyWeapon_ClaudaBreathAttack, 3, {0}, 0, NULL, BUFF_NONE, sHurricaneEvt,
     &partyWeapon_ChuchurinaKiss, 0},
    /* Spiny Surge: Lip Lock's slot on Mini-Egg's weapon (thrown: no contact, nothing drained), the port's tosses then
       every enemy (the meter's damage by rank: lakilesterSurgeDamage); Shell Slam's let-go-of-left gauge */
    {PM64_PARTNER_LAKILESTER, &partyWeapon_ClaudaPredationAttack, 1, {1, 2, 2, 3, 3, 4}, 0,
     &partyWeapon_YoshiEggAttack_Minimini, BUFF_NONE, sSpinySurgeEvt, &partyWeapon_NokotarouTsuranukiKoura, TARGETS_ALL},
    /* Cloud Nine: Dodgy Fog's slot, Mario Dodgy for as many turns as signals hit; the port's ride, Dodgy Fog's
       signals */
    {PM64_PARTNER_LAKILESTER, &partyWeapon_ClaudaKumogakureAttack, 2, {0}, 0, NULL, BUFF_DODGY_MARIO, sCloudNineEvt,
     &partyWeapon_ClaudaKumogakureAttack, 0},
    /* Electro Dash: Love Slap's weapons, PM64's dash and charge on a hold-A command (one hit) */
    {PM64_PARTNER_WATT, &partyWeapon_ChuchurinaNormalAttackLeft, 0, {1, 3, 1, 4, 1, 5}, FX_SHOCK, NULL, BUFF_NONE, sElectroDashEvt, &sWattHelpHoldA, 0},
    {PM64_PARTNER_WATT, &partyWeapon_ChuchurinaNormalAttackRight, 0, {1, 3, 1, 4, 1, 5}, FX_SHOCK, NULL, BUFF_NONE, sElectroDashEvt, &sWattHelpHoldA, 0},
    {PM64_PARTNER_WATT, &partyWeapon_ChuchurinaNormalAttackLeftLast, 0, {1, 3, 1, 4, 1, 5}, FX_SHOCK, NULL, BUFF_NONE,
     sElectroDashEvt, &sWattHelpHoldA, 0},
    {PM64_PARTNER_WATT, &partyWeapon_ChuchurinaNormalAttackRightLast, 0, {1, 3, 1, 4, 1, 5}, FX_SHOCK, NULL, BUFF_NONE,
     sElectroDashEvt, &sWattHelpHoldA, 0},
    /* Power Shock: Kiss Thief on Tease's weapon, at one enemy; Mega Shock: Tease, at all; PM64's charge and
       discharge on a mash gauge, its fill the paralysis chance */
    {PM64_PARTNER_WATT, &partyWeapon_ChuchurinaItemSteal, 1, {0}, FX_SHOCK | FX_PARALYZE | FX_SINGLE,
     &partyWeapon_ChuchurinaMadowaseAttack, BUFF_NONE, sPowerShockEvt, &sWattHelpMash, 0},
    {PM64_PARTNER_WATT, &partyWeapon_ChuchurinaMadowaseAttack, 3, {0}, FX_SHOCK | FX_PARALYZE, NULL, BUFF_NONE, sMegaShockEvt,
     &sWattHelpMash, 0},
    /* Turbo Charge: Smooch, at Mario; PM64's whirl round him on a three-lights command, attack +1 a turn per light */
    {PM64_PARTNER_WATT, &partyWeapon_ChuchurinaKiss, 2, {0}, 0, NULL, BUFF_ATK_MARIO, sTurboChargeEvt, &sWattHelpThreeLights, 0},
    /* Belly Flop: Shade Fist's weapon, the port's leap and flop */
    {PM64_PARTNER_SUSHIE, &partyWeapon_VivianNormalAttack, 0, {1, 3, 2, 4, 3, 5}, FX_JUMP, NULL, BUFF_NONE, sBellyFlopEvt, &partyWeapon_NokotarouKouraAttack, 0},
    /* Squirt: Infatuate's slot on Shade Fist's weapon (one enemy), the port's jet of water; PM64's meter bands (sushie.c
       N(GetSquirtDamage): 3 up to 5 / 6 / 7 by rank) as the command's fail / success */
    {PM64_PARTNER_SUSHIE, &partyWeapon_VivianCharmKissAttack, 1, {3, 5, 3, 6, 3, 7}, FX_WATER, &partyWeapon_VivianNormalAttack, BUFF_NONE,
     sSquirtEvt, &partyWeapon_SandersNormalAttack, 0},
    /* Water Block: Veil's slot, the defense-up buff on Mario (its turns the script's three chances), the port's
       script; its command Mario's jump's timed A */
    {PM64_PARTNER_SUSHIE, &partyWeapon_VivianShadowGuard, 2, {0}, 0, NULL, BUFF_DEF_MARIO, sWaterBlockEvt,
     &partyWeapon_ChristineNormalAttack, 0},
    /* Tidal Wave: Fiery Jinx's weapon and command, the port's wave; damage the buttons pressed (the script's
       pm64GaugeDamage, PM64's N(EVS_Move_TidalWave) quality) */
    {PM64_PARTNER_SUSHIE, &partyWeapon_VivianMagicalPowder, 3, {1, 1, 1, 1, 1, 1}, FX_WATER, NULL, BUFF_NONE, sTidalWaveEvt},
    /* Bow's (wearBow): Smack on Shade Fist (1 a slap at every rank: PM64's Smack 1-3), Outta Sight on Veil,
       Spook on Fiery Jinx, Fan Smack on Infatuate, on Shade Fist's weapon (one enemy; 2 a slap) */
    {PM64_PARTNER_BOW, &partyWeapon_VivianNormalAttack, 0, {1, 1, 1, 1, 1, 1}, FX_CONTACT, NULL, BUFF_NONE, sSmackEvt},
    {PM64_PARTNER_BOW, &partyWeapon_VivianShadowGuard, 1, {0}, 0, NULL, BUFF_NONE, sOuttaSightEvt},
    {PM64_PARTNER_BOW, &partyWeapon_VivianMagicalPowder, 2, {0}, 0, NULL, BUFF_NONE, sSpookEvt},
    {PM64_PARTNER_BOW, &partyWeapon_VivianCharmKissAttack, 3, {2, 2, 2, 2, 2, 2}, 0, &partyWeapon_VivianNormalAttack,
     BUFF_NONE, sSmackEvt},
};
#define MOVES (int)(sizeof(sMoves) / sizeof(sMoves[0]))

/* the rank (0-2) of the PM64 partner whose move weapon w is now */
static int weaponRank(const BattleWeapon* w) {
    int i;
    for (i = 0; i < MOVES; i++) {
        if (sMoves[i].weapon == w && portPm64PartnerOn(kWho[sMoves[i].p].host) == sMoves[i].p) {
            return partyGetTechLv((PartyId)kWho[sMoves[i].p].host);
        }
    }
    return 0;
}

/* PM64's message ids for move k of partner p: its name, its battle
 * description, and the full one the move table is found by */
static uint32_t moveName(int p, int k) {
    return PM64_MSG(0x2B, kWho[p].order * 4 + k);
}

static uint32_t moveShort(int p, int k) {
    return PM64_MSG(0x28, kWho[p].order * 13 + 2 + k);
}

static uint32_t moveFull(int p, int k) {
    return PM64_MSG(0x28, kWho[p].order * 13 + (k == 0 ? 7 : 9 + k));
}

int portPm64MoveText(int which, char* name, size_t nameSize, char* desc, size_t descSize) {
    const MoveMap* m;
    const int lang = portPm64TextLanguage();
    if (which < 0 || which >= MOVES || lang < 0) {
        return 0; /* (the ROM's text not in the game's language: the game's own names stay) */
    }
    m = &sMoves[which];
    if (name != NULL && portPm64MessageText(moveName(m->p, m->move), lang, name, nameSize) <= 0) {
        return 0;
    }
    if (desc != NULL && portPm64MessageText(moveShort(m->p, m->move), lang, desc, descSize) <= 0) {
        desc[0] = '\0';
    }
    return 1;
}

/* the Party tab's help for host: the PM64 partner out on it, his partner menu description
 * (PartnerDesc_<name>_Pause: the seventh of his move set's 13 messages), laid out as the game's help text */
const char* portPm64PartyHelp(int host) {
#ifdef PORT_MODS
    /* (one tag each: the help window takes a new text by its tag, and Yoshi's place has two of them) */
    static const char* const kTag[PM64_PARTNER_COUNT] = {
        "", "pc_pm64_party_help1", "pc_pm64_party_help2", "pc_pm64_party_help3", "pc_pm64_party_help4",
        "pc_pm64_party_help5", "pc_pm64_party_help6", "pc_pm64_party_help7", "pc_pm64_party_help8"};
    static unsigned sGen[PM64_PARTNER_COUNT];
    static char sHave[PM64_PARTNER_COUNT];
    char raw[768], text[800];
    const int p = portPm64PartnerOn(host), lang = portPm64TextLanguage();
    unsigned gen;
    int i, n;
    size_t o = 0;
    if (p <= 0 || p >= PM64_PARTNER_COUNT || lang < 0) {
        return NULL;
    }
    gen = (portPm64Generation() + 1) * 8u + (unsigned)lang;
    if (gen != sGen[p]) {
        sGen[p] = gen;
        sHave[p] = 0;
        text[0] = '\0';
        n = portPm64MessageText(PM64_MSG(0x28, kWho[p].order * 13 + 6), lang, raw, sizeof(raw));
        if (n > 0) {
            /* "Press <C-Down> to ...": PM64's field ability button (glyph 0x9E, which the text leaves out
               between its two spaces) as TTYD's, X ({X}: the game's icon, as the button prompts draw it) */
            size_t rawLen = 0;
            const uint8_t* m = portPm64MessageRaw(PM64_MSG(0x28, kWho[p].order * 13 + 6), lang, &rawLen);
            const int button = m != NULL && memchr(m, 0x9E, rawLen) != NULL;
            /* (PM64's line breaks are its own box's: one text, laid out in the game's lines and pages; a word
               broken at its hyphen joined again) */
            for (i = 0; i < n && o + 6 < sizeof(text); i++) {
                if (raw[i] == '\n') {
                    if (o > 0 && text[o - 1] != ' ' && text[o - 1] != '-') {
                        text[o++] = ' ';
                    }
                } else if (button && raw[i] == ' ' && i + 1 < n && raw[i + 1] == ' ') {
                    memcpy(text + o, " {X} ", 5);
                    o += 5;
                    i++;
                } else {
                    text[o++] = raw[i];
                }
            }
            while (o > 0 && text[o - 1] == ' ') {
                o--;
            }
            text[o] = '\0';
            portModsSetMessage(kTag[p], portGameLayout(text, 30, 0));
            sHave[p] = 1;
        }
        if (pm64Log()) {
            printf("[pm64] Party tab: %s's description: %s\n", kWho[p].english, sHave[p] ? text : "(none in the ROM)");
            fflush(stdout);
        }
    }
    return sHave[p] ? kTag[p] : NULL;
#else
    (void)host;
    return NULL;
#endif
}

int portPm64MoveCountMapped(void) {
    return MOVES;
}

/* the move's weapon's message tags, and whether it's the PM64 partner's now */
const char* portPm64MoveTag(int which, int description) {
    if (which < 0 || which >= MOVES) {
        return NULL;
    }
    return description ? sMoves[which].weapon->description : sMoves[which].weapon->name;
}

int portPm64MoveWorn(int which) {
    return which >= 0 && which < MOVES && portPm64PartnerOn(kWho[sMoves[which].p].host) == sMoves[which].p;
}

/* Bow's moves beyond the script (wear, last): Smack's and Fan Smack's slaps plain (Shade Fist burns; PM64's
 * no contact for the fan), the stick-flick help text (Shell Slam's); Outta Sight at Mario, nothing
 * dealt (the script hides him); Spook a fright (the chance set from the meter), no damage */
static const char* helpOf(const BattleWeapon* h);
static void wearBow(const MoveMap* m, BattleWeapon* w) {
    if (m->p != PM64_PARTNER_BOW) {
        return;
    }
    if (m->script == sSmackEvt) {
        w->element = ELEMENT_NORMAL;
        w->burnChance = 0;
        w->burnTime = 0;
        w->acHelpMsg = helpOf(&partyWeapon_NokotarouTsuranukiKoura); /* (Shell Slam's stick flicks) */
        /* (the borrowed-weapon strip: PM64's Smack is a plain slap that a spiky top or front doesn't prick,
           a fiery one still burns; Fan Smack doesn't touch at all: nothing pricks or burns her) */
        w->counterResistFlags = m->weapon == &partyWeapon_VivianCharmKissAttack
                                    ? COUNTER_RESIST_TOP_SPIKY | COUNTER_RESIST_PREEMPTIVE_SPIKY |
                                          COUNTER_RESIST_FRONT_SPIKY | COUNTER_RESIST_FIERY | COUNTER_RESIST_ICY |
                                          COUNTER_RESIST_POISON | COUNTER_RESIST_EXPLOSIVE
                                    : COUNTER_RESIST_TOP_SPIKY | COUNTER_RESIST_FRONT_SPIKY;
        w->special &= ~(SPECIAL_SHATTERS_FREEZE | SPECIAL_FLIPS_BOMB_FLIPPABLE | SPECIAL_GROUNDS_WINGED |
                        SPECIAL_FLIPS_SHELLED);
    } else if (m->script == sOuttaSightEvt) {
        w->hpDamageFn = NULL;
        w->fpDamageFn = NULL;
        memset(&w->sleepChance, 0, (size_t)((char*)w->pad_AE - (char*)&w->sleepChance));
        w->special = SPECIAL_ALWAYS_HITS | SPECIAL_IGNORE_STATUS_VULNERABILITY | SPECIAL_UNGUARDABLE;
        w->counterResistFlags = COUNTER_RESIST_ALL;
        w->acHelpMsg = NULL;
    } else if (m->script == sSpookEvt) {
        w->hpDamageFn = NULL;
        w->fpDamageFn = NULL;
        memset(&w->sleepChance, 0, (size_t)((char*)w->pad_AE - (char*)&w->sleepChance));
        w->frightChance = 100;
        w->element = ELEMENT_NORMAL;
        w->damagePattern = 0;
        w->special = SPECIAL_USABLE_IF_CONFUSED | SPECIAL_UNGUARDABLE;
        w->counterResistFlags = COUNTER_RESIST_ALL;
        w->acHelpMsg = helpOf(&partyWeapon_NokotarouTsuranukiKoura); /* (Shell Slam's stick flicks) */
    }
}

/* the TTYD move h's action-command help text as the game has it (h may be a mapped weapon, worn now: its own) */
static const char* helpOf(const BattleWeapon* h) {
    int i;
    for (i = 0; i < MOVES; i++) {
        if (sMoves[i].weapon == h && sMoves[i].kept) {
            return sMoves[i].own.acHelpMsg;
        }
    }
    return h->acHelpMsg;
}

/* A worn move's weapon: the host's, with PM64's FP cost (the ROM's move
 * table), damage and effects. */
static void wear(MoveMap* m) {
    BattleWeapon* w = m->weapon;
    const char* help = m->help != NULL ? helpOf(m->help) : NULL; /* (before w changes: it may be the help) */
    Pm64Move pm;
    int i;
    if (m->base != NULL) {
        /* another host move's script, targets, damage and effects, under this one's name, text and icon */
        const char* name = w->name;
        const char* desc = w->description;
        const u16 icon = w->icon;
        *w = m->baseOwn;
        w->name = name;
        w->description = desc;
        w->icon = icon;
    }
    {
        /* tests: TTYD_PM64_ACCURACY=<0-100> every PM64 attack that accurate (a fogged stage's or a dizzy
           partner's misses, on demand) */
        static int sAcc = -2;
        if (sAcc == -2) {
            const char* v = getenv("TTYD_PM64_ACCURACY");
            sAcc = v != NULL && v[0] != '\0' ? atoi(v) : -1;
        }
        if (sAcc >= 0 && m->buff == BUFF_NONE) {
            w->baseAccuracy = (u8)(sAcc > 100 ? 100 : sAcc);
        }
    }
    if (m->buff != BUFF_NONE) {
        const int rank = partyGetTechLv((PartyId)kWho[m->p].host);
        w->attackEvtScript = m->buff == BUFF_CHARGE_SELF ? sChargeEvt
                             : m->buff == BUFF_ATK_MARIO ? sTurboChargeEvt
                             : m->buff == BUFF_DEF_MARIO ? sWaterBlockEvt
                                                         : sCloudNineEvt;
        w->hpDamageFn = NULL;
        w->fpDamageFn = NULL;
        memset(&w->sleepChance, 0, (size_t)((char*)w->pad_AE - (char*)&w->sleepChance));
        w->special = SPECIAL_ALWAYS_HITS | SPECIAL_IGNORE_STATUS_VULNERABILITY | SPECIAL_UNGUARDABLE;
        w->counterResistFlags = COUNTER_RESIST_ALL;
        w->targetPropertyFlags = TARGET_PROP_START_OWN_SIDE;
        w->targetWeightFlags = 0;
        w->acHelpMsg = NULL;
        w->targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT |
                              TARGET_CLASS_NO_OPPOSING_ALLIANCE | TARGET_CLASS_MAIN_PARTS | TARGET_CLASS_SINGLE;
        if (m->buff == BUFF_CHARGE_SELF) {
            w->targetClassFlags |= TARGET_CLASS_ONLY_SELF;
            w->chargeStrength = rank >= 2 ? 3 : 2;
        } else {
            w->targetClassFlags |= TARGET_CLASS_ONLY_MARIO | TARGET_CLASS_NO_SELF;
            if (m->buff == BUFF_ATK_MARIO) {
                w->atkChangeChance = 100;
                w->atkChangeTime = 3;
                w->atkChangeStrength = 1;
            } else if (m->buff == BUFF_DEF_MARIO) {
                w->defChangeChance = 100;
                w->defChangeTime = 3;
                w->defChangeStrength = 1;
            } else {
                w->dodgyChance = 100;
                w->dodgyTime = 3;
            }
        }
    }
    if (m->script != NULL && m->buff == BUFF_NONE) {
        /* PM64's move has only what its row says (fx, damage, the script's own copies): nothing of the borrowed
           TTYD weapon's statuses (Mini-Egg's Tiny, Ground Pound's, Fiery Jinx's burn...), element, defense piercing,
           flipping, grounding, diminishing or always-hitting is kept; the fx flags below put back PM64's own */
        memset(&w->sleepChance, 0, (size_t)((char*)w->pad_AE - (char*)&w->sleepChance));
        w->element = ELEMENT_NORMAL;
        w->special &= SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_ATTACK_FX |
                      SPECIAL_USABLE_IF_CONFUSED | SPECIAL_UNGUARDABLE;
    }
    if (m->script != NULL) {
        /* the port's choreography; Shell Toss's hold-and-release command and its help text */
        w->attackEvtScript = (EvtWord*)m->script;
        /* the command's help text: the TTYD move whose command kind the script sets up (its row's help) */
        if (m->help != NULL) {
            w->acHelpMsg = help;
        }
        if (m->targets == TARGETS_ALL) {
            w->targetClassFlags = (w->targetClassFlags & ~TARGET_CLASS_SINGLE) | TARGET_CLASS_MULTIPLE;
        } else if (m->targets == TARGETS_ANY_ONE) {
            /* (any one enemy: in the air or on the ceiling too) */
            w->targetClassFlags = (w->targetClassFlags & ~TARGET_CLASS_MULTIPLE) | TARGET_CLASS_SINGLE;
            w->targetPropertyFlags &= ~(TARGET_PROP_NO_CEILING | TARGET_PROP_NO_FLOATING);
        } else if (m->targets == TARGETS_NO_CEILING) {
            w->targetPropertyFlags |= TARGET_PROP_NO_CEILING;
        }
        if (m->dmg[1] != 0) {
            w->hpDamageFn = weaponGetPowerFromPartyAttackLv;
        }
    }
    if (m->fx & FX_SINGLE) {
        w->targetClassFlags = (w->targetClassFlags & ~TARGET_CLASS_MULTIPLE) | TARGET_CLASS_SINGLE;
    }
    if (m->fx & FX_GROUND) {
        w->targetPropertyFlags |= TARGET_PROP_NO_FLOATING | TARGET_PROP_NO_CEILING;
    }
    if (m->fx & FX_FIRE) {
        w->element = ELEMENT_FIRE;
        w->burnChance = 0;
        w->burnTime = 0;
    }
    if (m->fx & FX_DIZZY) {
        w->hpDamageFn = NULL;
        w->fpDamageFn = NULL;
        w->dizzyChance = 100;
        w->dizzyTime = 2;
    }
    if (portPm64MoveByDesc(moveFull(m->p, m->move), &pm) >= 0) {
        w->baseFpCost = (s8)pm.costFP;
    }
    if (m->dmg[1] != 0 && w->hpDamageFn == weaponGetPowerFromPartyAttackLv) {
        for (i = 0; i < 6; i++) {
            w->hpDamageWork[i] = (u32)m->dmg[i];
        }
    }
    if (m->fx & FX_SHOCK) {
        w->element = ELEMENT_ELECTRIC;
        w->special |= SPECIAL_PIERCES_DEFENSE;
    }
    if (m->fx & FX_CONTACT) {
        w->element = ELEMENT_NORMAL;
        w->special &= ~(SPECIAL_SHATTERS_FREEZE | SPECIAL_FLIPS_BOMB_FLIPPABLE | SPECIAL_GROUNDS_WINGED);
        w->counterResistFlags = COUNTER_RESIST_TOP_SPIKY;
    }
    if (m->fx & FX_JUMP) {
        w->special |= SPECIAL_FLIPS_SHELLED | SPECIAL_GROUNDS_WINGED;
    }
    if (m->fx & FX_BLAST) {
        w->element = ELEMENT_EXPLOSION;
        w->special |= SPECIAL_FLIPS_BOMB_FLIPPABLE;
    }
    if (m->fx & FX_WATER) {
        w->element = ELEMENT_NORMAL;
        w->burnChance = 0;
        w->burnTime = 0;
    }
    if (m->fx & FX_PARALYZE) {
        w->stopChance = w->dizzyChance;
        w->stopTime = 3;
        w->dizzyChance = 0;
        w->dizzyTime = 0;
    }
    wearBow(m, w);
    if (sHeldDmg.w == w) {
        /* (the meter's damage this use, kept over the per-frame re-wear until the move ends) */
        for (i = 0; i < 6; i++) {
            w->hpDamageWork[i] = sHeldDmg.v[i];
        }
    }
}

/* Tests and the battle animation survey (TTYD_PM64_SPRITE_DUMP=<folder>): each partner's field and battle
 * sprite, every animation as a row of 10 pictures 6 frames apart (parts at their places, unrotated, facing
 * left as PM64 draws them), written once the ROM is linked as <folder>/<name>_<field|battle>.pam (PAM,
 * RGBA; tools turn it into a PNG). Nothing of it is kept in the game. */
static void spriteDump(void) {
    enum { CELL = 112, COLS = 10, STEP = 6 };
    static int sDone;
    static Pm64Anim a;
    static uint8_t pixels[192 * 192 * 4];
    const char* dir = getenv("TTYD_PM64_SPRITE_DUMP");
    int p, battle;
    if (sDone || dir == NULL || dir[0] == '\0' || portPm64Linked() == PM64_NONE) {
        return;
    }
    sDone = 1;
    for (p = 1; p < PM64_PARTNER_COUNT; p++) {
        for (battle = 0; battle < 2; battle++) {
            const Pm64Sprite* s = portPm64NpcSprite(kWho[p].sprite + (battle ? 8 : 0));
            const int anims = s != NULL ? portPm64SpriteAnimCount(s) : 0;
            const int w = CELL * COLS, h = CELL * (anims > 0 ? anims : 1);
            uint8_t* sheet;
            char path[512];
            FILE* f;
            int an, col, i;
            if (anims <= 0 || (sheet = (uint8_t*)calloc((size_t)w * h, 4)) == NULL) {
                continue;
            }
            for (an = 0; an < anims; an++) {
                portPm64AnimStart(&a, s, an);
                for (col = 0; col < COLS; col++) {
                    for (i = 0; i < a.count; i++) {
                        int rw, rh, x, y, left, top;
                        if (a.part[i].raster < 0 || !portPm64SpriteRaster(s, a.part[i].raster, &rw, &rh, NULL) ||
                            rw <= 0 || rh <= 0 || rw * rh > 192 * 192 ||
                            !portPm64SpriteRGBA(s, a.part[i].raster, a.part[i].palette, pixels)) {
                            continue;
                        }
                        left = col * CELL + CELL / 2 + (int)floorf(a.part[i].x) - rw / 2;
                        top = an * CELL + CELL - 8 - ((int)floorf(a.part[i].y) + rh);
                        for (y = 0; y < rh; y++) {
                            for (x = 0; x < rw; x++) {
                                const uint8_t* px = pixels + ((size_t)y * rw + x) * 4;
                                if (px[3] != 0 && left + x >= col * CELL && left + x < (col + 1) * CELL &&
                                    top + y >= an * CELL && top + y < (an + 1) * CELL) {
                                    memcpy(sheet + ((size_t)(top + y) * w + left + x) * 4, px, 4);
                                }
                            }
                        }
                    }
                    portPm64AnimStep(&a, (float)STEP);
                }
            }
            snprintf(path, sizeof(path), "%s/%s_%s.pam", dir, kWho[p].english, battle ? "battle" : "field");
            if ((f = portFopen(path, "wb")) != NULL) {
                fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", w, h);
                fwrite(sheet, 4, (size_t)w * h, f);
                fclose(f);
                printf("[pm64] sprite dump: %s, %d animations\n", path, anims);
            }
            free(sheet);
        }
    }
    fflush(stdout);
}

/* Kooper's Fire Shell and Dizzy Shell (PM64), drawn on the host's Shell Slam: flames on the shell while it
 * spins out, and the dizzy stars over each enemy it leaves dizzy. Looks only (the game's state untouched). */
static void shellFx(void) {
    static signed char sDizzy[BATTLE_UNIT_MAX];
    BattleWork* wp = _battleWorkPointer;
    const BattleWeapon* w = NULL;
    const BattleUnit* shell = NULL;
    int i, fire, dizzy;
    if (wp == NULL || gp == NULL || !gp->inBattle || portPm64PartnerOn(2) != PM64_PARTNER_KOOPER) {
        return;
    }
    /* Kooper spinning in his shell (his host's A_1): only then is the menu's weapon his move's (the menu's
       cursor isn't set before the first turn) */
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = wp->units[i];
        if (u != NULL && u->currentKind == UNIT_KOOPS && u->parts != NULL && strstr(u->parts->poseName, "A_1") != NULL) {
            shell = u;
        }
    }
    if (shell != NULL) {
        w = BattleGetSelectWeapon(wp);
    }
    fire = shell != NULL && w == &partyWeapon_NokotarouTsuranukiKoura;
    dizzy = shell != NULL && w == &partyWeapon_NokotarouKouraGuard;
    if (fire && (gp->retraceCount & 3) == 0) {
        effFlameN64Entry(0, shell->position.x, shell->position.y + 8.0f, shell->position.z + 6.0f, 0.5f);
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = wp->units[i];
        if (u == NULL) {
            sDizzy[i] = 0;
            continue;
        }
        if (dizzy && u->alliance == 1 && u->dizzyTurns > 0 && sDizzy[i] <= 0) {
            effGuruguruN64Entry(0, 5, u->position.x, u->position.y + (float)u->height + 4.0f, u->position.z + 5.0f,
                                (float)u->width * 0.5f + 6.0f);
        }
        sDizzy[i] = u->dizzyTurns;
    }
}

/* The hosts' own battle rules that Paper Mario 64's partners don't have (local MATCH.md, "Host rules"), each frame
 * in battle while the PM64 partner is worn, and put back when it isn't:
 * - Koops is shelled (PART_ATTR_SHELLED): a stomp flips him on his back for two turns, and he has 1 DEF. PM64's
 *   partners are never flipped and have no defense: Kooper takes a stomp as an ordinary hit, at 0 DEF.
 * - Vivian and Flurrie float (UNIT_ATTR_FLOATING): TTYD's ground-only attacks (quakes) miss them. PM64 has no such
 *   rule for a partner: Sushie (on the ground), Bow and Lakilester are reached as the others are.
 * Everything else of the hosts' is the same for all of them (no HP bar, guard cover) or belongs to their moves
 * (Shell Shield, Veil), which the PM64 moves replace. Draw-free; the battle's own state, from the partner
 * setting every game of a session shares (online too). */
static void hostRules(void) {
    static BattleUnitDefense sNoDefense; /* (all 0) */
    static BattleUnitDefense* sKoopsDefense;
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (wp == NULL || gp == NULL || !gp->inBattle) {
        return;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* unit = BattleGetUnitPtr(wp, i);
        BattleUnitPart* part;
        if (unit == NULL || unit->alliance != 0) {
            continue;
        }
        if (unit->currentKind == UNIT_KOOPS) {
            const int pm64 = portPm64PartnerOn(2) == PM64_PARTNER_KOOPER;
            for (part = unit->parts; part != NULL; part = part->nextPart) {
                if (part->kindPartParams == NULL || !(part->kindPartParams->attributes & PART_ATTR_SHELLED)) {
                    continue; /* (only the part that is shelled of its own) */
                }
                if (pm64) {
                    part->attributes &= ~PART_ATTR_SHELLED;
                    if (part->defense != &sNoDefense) {
                        sKoopsDefense = part->defense;
                        part->defense = &sNoDefense;
                    }
                } else {
                    part->attributes |= PART_ATTR_SHELLED;
                    if (part->defense == &sNoDefense) {
                        part->defense = sKoopsDefense != NULL ? sKoopsDefense : part->kindPartParams->defense;
                    }
                }
            }
        } else if (unit->currentKind == UNIT_VIVIAN || unit->currentKind == UNIT_FLURRIE) {
            if (portPm64PartnerOn(unit->currentKind == UNIT_VIVIAN ? 6 : 5) != 0) {
                unit->attributeFlags &= ~UNIT_ATTR_FLOATING;
            } else if (unit->kindParams != NULL && (unit->kindParams->attributeFlags & UNIT_ATTR_FLOATING)) {
                unit->attributeFlags |= UNIT_ATTR_FLOATING;
            }
        }
    }
}

/* Each frame: the moves of the partners now worn are PM64's (above), the rest
 * the game's own. Game state (it changes what a move does), from GSW and
 * settings every game of a session shares. */
void portPm64BattleCosts(void) {
    static unsigned sSig;
    unsigned sig = 0;
    int i, j;
    spriteDump();
    shellFx();
    backdropFrame();
    pm64MovesFrame();
    shockFrame();
    hostRules();
    for (i = 0; i < MOVES; i++) {
        if (!sMoves[i].kept) {
            /* (all at once on the first frame, before any is worn: the game's own) */
            sMoves[i].own = *sMoves[i].weapon;
            if (sMoves[i].base != NULL) {
                sMoves[i].baseOwn = *sMoves[i].base;
            }
            sMoves[i].kept = 1;
        }
    }
    for (i = 0; i < MOVES; i++) {
        MoveMap* m = &sMoves[i];
        /* a weapon two PM64 partners' maps share (Vivian's: Sushie's and Bow's) goes by the one worn */
        for (j = 0; j < MOVES; j++) {
            if (sMoves[j].weapon == m->weapon && portPm64MoveWorn(j)) {
                break;
            }
        }
        if (j < MOVES) {
            if (j == i) {
                *m->weapon = m->own;
                wear(m);
            }
        } else if (memcmp(m->weapon, &m->own, sizeof(BattleWeapon)) != 0) {
            *m->weapon = m->own;
        }
        sig = sig * 31u + (unsigned)(j == i);
    }
    /* tests (TTYD_PM64_LOG): each move worn as it changes, its FP and damage by rank */
    if (sig != sSig && pm64Log()) {
        sSig = sig;
        for (i = 0; i < MOVES; i++) {
            const BattleWeapon* w = sMoves[i].weapon;
            char dmg[48] = "";
            if (!portPm64MoveWorn(i) || (i > 0 && sMoves[i - 1].p == sMoves[i].p && sMoves[i - 1].move == sMoves[i].move)) {
                continue; /* (Love Slap's four weapons: one line) */
            }
            if (sMoves[i].dmg[1] != 0) {
                snprintf(dmg, sizeof(dmg), ", damage by rank %u/%u %u/%u %u/%u", w->hpDamageWork[0],
                         w->hpDamageWork[1], w->hpDamageWork[2], w->hpDamageWork[3], w->hpDamageWork[4],
                         w->hpDamageWork[5]);
            }
            printf("[pm64] %s's move %d: %d FP%s%s%s\n", kWho[sMoves[i].p].english, sMoves[i].move, w->baseFpCost, dmg,
                   w->element == ELEMENT_ELECTRIC ? ", electric" : "",
                   (w->special & SPECIAL_PIERCES_DEFENSE) ? ", through defense" : "");
            printf("[pm64]   %s%s%s%s%s%s%s\n", (w->targetClassFlags & TARGET_CLASS_SINGLE) ? "one target" : "all targets",
                   w->element == ELEMENT_FIRE ? ", fire" : "", w->stopTime > 0 ? ", paralysis" : "",
                   w->dizzyChance > 0 ? ", dizzy" : "", w->chargeStrength > 0 ? ", charge" : "",
                   w->atkChangeStrength > 0 ? ", attack up" : "", w->dodgyChance > 0 ? ", dodgy" : "");
            {
                /* every status the worn weapon carries, its element, special and counter-resist flags: what PM64's
                   move has and nothing the borrowed TTYD weapon brought (tests: the pm64_choreo runs' logs) */
                static const char* const kSt[] = {"sleep", "stop", "dizzy", "poison", "confuse", "electric", "dodgy",
                                                  "burn", "freeze", "size", "atk", "def", "allergic", "ohko",
                                                  "charge", "fast", "slow", "fright", "gale"};
                const unsigned char* b = (const unsigned char*)w;
                /* (offsetof: the PC layout, not the comments' GameCube offsets) */
                static const unsigned short kOff[] = {
                    offsetof(BattleWeapon, sleepChance),    offsetof(BattleWeapon, stopChance),
                    offsetof(BattleWeapon, dizzyChance),    offsetof(BattleWeapon, poisonChance),
                    offsetof(BattleWeapon, confuseChance),  offsetof(BattleWeapon, electricChance),
                    offsetof(BattleWeapon, dodgyChance),    offsetof(BattleWeapon, burnChance),
                    offsetof(BattleWeapon, freezeChance),   offsetof(BattleWeapon, sizeChangeChance),
                    offsetof(BattleWeapon, atkChangeChance), offsetof(BattleWeapon, defChangeChance),
                    offsetof(BattleWeapon, allergicChance), offsetof(BattleWeapon, ohkoChance),
                    offsetof(BattleWeapon, chargeStrength), offsetof(BattleWeapon, fastChance),
                    offsetof(BattleWeapon, slowChance),     offsetof(BattleWeapon, frightChance),
                    offsetof(BattleWeapon, galeForceChance)};
                char st[160] = "";
                size_t k, n = 0;
                for (k = 0; k < sizeof(kOff) / sizeof(kOff[0]); k++) {
                    if (b[kOff[k]] != 0 && n + 16 < sizeof(st)) {
                        n += (size_t)snprintf(st + n, sizeof(st) - n, " %s %d", kSt[k], (int)(signed char)b[kOff[k]]);
                    }
                }
                printf("[pm64]   weapon: element %d, special %05x, counter-resist %05x, statuses:%s\n", w->element,
                       (unsigned)w->special, (unsigned)w->counterResistFlags, n ? st : " none");
            }
        }
        fflush(stdout);
    }
}

/* tests (TTYD_PM64_LOG): a worn move's hit as the game dealt it (btlevtcmd_CheckDamage's), the damage it did (what
 * its total of damage taken grew by: past what HP was left), the HP and the statuses the target has after it */
/* the worn PM64 move (its sMoves row) the battle menu chose, -1 none */
static int selectedMove(void) {
    const BattleWeapon* sel = _battleWorkPointer != NULL ? BattleGetSelectWeapon(_battleWorkPointer) : NULL;
    int i;
    for (i = 0; sel != NULL && i < MOVES; i++) {
        if (sMoves[i].weapon == sel && portPm64MoveWorn(i)) {
            return i;
        }
    }
    return -1;
}

void portPm64HitLog(BattleUnit* attacker, BattleUnit* target, int hpBefore, int totalBefore, int result) {
    static const char* const kSt[] = {"sleep", "stop", "dizzy", "poison", "confuse", "electric", "dodgy", "burn",
                                      "freeze", "atk",   "def",  "charge"};
    char st[96] = "";
    size_t n = 0;
    int i, k;
    if (!pm64Log() || attacker == NULL || target == NULL || _battleWorkPointer == NULL || !pm64MoveUser(attacker)) {
        return;
    }
    i = selectedMove();
    if (i < 0) {
        return;
    }
    {
        /* (turns, or the strength for attack, defense and charge) */
        const int v[] = {target->sleepTurns,   target->stopTurns,         target->dizzyTurns,
                         target->poisonTurns,  target->confusionTurns,    target->electricTurns,
                         target->dodgyTurns,   target->burnTurns,         target->freezeTurns,
                         target->atkChangeStrength, target->defChangeStrength, target->chargeStrength};
        for (k = 0; k < (int)(sizeof(v) / sizeof(v[0])); k++) {
            if (v[k] != 0 && n + 16 < sizeof(st)) {
                n += (size_t)snprintf(st + n, sizeof(st) - n, ", %s %d", kSt[k], v[k]);
            }
        }
    }
    printf("[pm64] %s's move %d hit %s: %d damage, HP %d->%d (result %d%s)\n", kWho[sMoves[i].p].english,
           sMoves[i].move, target->kindParams != NULL && target->kindParams->name != NULL ? target->kindParams->name : "?",
           (int)target->totalHpDamageTaken - totalBefore, hpBefore, target->currentHp, result, st);
    fflush(stdout);
}

/* Tease's weapon is made as it's used (unit_party_chuchurina.c, its dizzy
 * chance the action command's), a copy of the move's weapon: Mega Shock's
 * (Tease) or Power Shock's (Kiss Thief on Tease's script) paralysis in its place. */
void portPm64MadowaseWeapon(BattleWeapon* w) {
    for (int i = 0; i < MOVES; i++) {
        if (sMoves[i].weapon == &partyWeapon_ChuchurinaMadowaseAttack && portPm64MoveWorn(i)) {
            const int one = (w->targetClassFlags & TARGET_CLASS_SINGLE) != 0;
            w->stopChance = w->dizzyChance;
            w->stopTime = 3;
            w->dizzyChance = 0;
            w->dizzyTime = 0;
            if (pm64Log()) {
                printf("[pm64] %s: stop chance %d, %d turns\n", one ? "Power Shock" : "Mega Shock", w->stopChance,
                       w->stopTime);
                fflush(stdout);
            }
            return;
        }
    }
}
