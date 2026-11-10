/* Japanese restorations and talk blips (port/include/port/postgame_extra.h).
 *
 *   - Hooktail's frogs ("pg_jp_hooktail_frogs", US and EU): in the Japanese
 *     release Hooktail can't stand frogs, and Attack FX R's sound is a
 *     frog's; the English and European releases made it a cricket in three
 *     texts. Those three are put back to frogs, in Paper Cut's own words:
 *     her two complaints in the battle and the plaza's notice board.
 *   - The Japanese bad ending ("pg_jp_bad_ending"): las_29.c, through
 *     portPgJpBadEnding (postgame_extra_pc.c).
 *   - Talk blips ("pg_talk_blips"): while a speech bubble prints, a blip
 *     every few letters, the voice by who speaks: the game's own message
 *     sounds (SFX_MESSAGE*, the <se> sets), pitched for each kind of
 *     speaker (a Toad high, a Pianta low, a Boo higher still). Sound only. */
#include "port/statesync.h"
#include "port/postgame_extra.h"
#include "port/restorations.h"
#include "port/mod_host.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "driver/animdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/windowdrv.h"
#include "memory.h"
#include "mario/mariost.h"
#include "pmario_sound.h"

#include <stdio.h>
#include <string.h>

extern WindowEntry* WinObjects; /* windowdrv.c */

/* --- Hooktail's frogs ---------------------------------------------------------- */

static const PortPgText kFrogs[] = {
    {"stg1_gon_21",
     "<boss>BLECH! That noise!<wait 250> That's... a FROG!<wait 250> How did you know?!<p>Make it stop! Make it "
     "stop this instant!<p>Urrgh...<wait 250> Long ago I swallowed a frog by mistake,<wait 250> and it croaked "
     "all the way down. I HATE them!<p>Steady, Hooky...<wait 250> One little croak and my head spins!<k>",
     1},
    {"stg1_gon_31_04", "<boss>BLURGH!<wait 250> Why does every single croak make me feel so faint?<k>", 1},
    {"kj_omote_0003",
     "<!STG1_07><plate>Hooktail is terrorizing Petal Meadows! If she spots you, croak like a frog!<p>It might "
     "even work!<k>",
     1},
};

static void frogs(void) {
    static int sWas = -1, sLang = -1;
    int* v = portSettingsDeclare("pg_jp_hooktail_frogs", 0, 0, 1);
    const int now = v != NULL && *v != 0 && PORT_GAME_ID[3] != 'J';
    const int lang = portTrLanguage() * 2 + (portStateSyncLinked() || portReplayActive());
    size_t i;
    if (now == sWas && (!now || lang == sLang)) {
        return;
    }
    sWas = now;
    sLang = lang;
    for (i = 0; i < sizeof(kFrogs) / sizeof(kFrogs[0]); i++) {
        portModsSetMessage(kFrogs[i].tag, now ? portGameLayout(portPgTr(kFrogs[i].english), 30, 3) : NULL);
    }
    if (now) {
        printf("[postgame] Hooktail's frogs: %d texts put back\n", (int)(sizeof(kFrogs) / sizeof(kFrogs[0])));
        fflush(stdout);
    }
}

/* --- talk blips ------------------------------------------------------------------ */

/* a speaker's voice: the model's name starts with prefix; the <se> set (1-5) and the pitch */
static const struct {
    const char* prefix;
    int set;
    int pitch;
} kVoices[] = {
    {"c_kinopi", 1, 300},  /* Toads and Toadettes */
    {"c_kokinop", 1, 500}, /* Toad children */
    {"c_kinopio", 1, 200},
    {"c_kuribo", 2, -100}, /* Goombas */
    {"c_pkr", 2, 200},     /* Goombella */
    {"c_nokonoko", 3, 0},  /* Koopas */
    {"c_pnk", 3, 100},     /* Koops */
    {"c_teresa", 5, 500},  /* Boos */
    {"c_m_", 2, -400},     /* Piantas */
    {"c_bom", 3, -200},    /* Bob-ombs */
    {"c_puni", 1, 600},    /* Punies */
    {"c_jyabu", 4, -300},  /* Jabbis */
    {"c_zako", 4, -100},   /* X-Nauts */
    {"c_kanbu", 4, -300},  /* Lord Crump */
    {"c_windy", 5, -100},  /* Flurrie */
    {"c_vivian", 5, 200},  /* Vivian */
    {"c_majyorin", 5, -300},
    {"c_maririn", 5, -500},
    {"c_koopa", 3, -600},  /* Bowser */
    {"c_peach", 1, 400},
    {"c_koburon", 2, 300}, /* Whacka */
    {"c_ranpel", 5, 0},    /* Doopliss */
    {"c_mario", 2, 0},
};
static const int kSetSize[6] = {0, 2, 6, 4, 1, 5};

static int sBlipLetters[PORT_WINDOW_SLOTS];
static int sBlipTurn;

static void voiceOf(const MsgWinData* d, int* set, int* pitch) {
    NpcEntry* npc;
    const char* model = NULL;
    size_t i;
    *set = 1;
    *pitch = 0;
    if ((d->flags & MSG_WIN_FLAG_BATTLE_UNIT) || d->speakerName[0] == '\0') {
        return;
    }
    npc = npcNameToPtr_NoAssert(d->speakerName);
    if (npc != NULL && npc->poseId >= 0) {
        const AnimationPoseData* base = animPoseGetAnimBaseDataPtr(npc->poseId);
        model = base != NULL ? base->fileName : NULL;
    }
    if (model == NULL) {
        return;
    }
    for (i = 0; i < sizeof(kVoices) / sizeof(kVoices[0]); i++) {
        if (strncmp(model, kVoices[i].prefix, strlen(kVoices[i].prefix)) == 0) {
            *set = kVoices[i].set;
            *pitch = kVoices[i].pitch;
            return;
        }
    }
}

static void blips(void) {
    int* v = portSettingsDeclare("pg_talk_blips", 0, 0, 1);
    int w;
    if (v == NULL || *v == 0 || WinObjects == NULL) {
        return;
    }
    for (w = 0; w < PORT_WINDOW_SLOTS; w++) {
        const WindowEntry* win = &WinObjects[w];
        const MsgWinData* d;
        int e, letters = 0;
        if (!(win->flags & WINDOW_FLAG_USED) || win->userdata == NULL || win->userdata->address == NULL) {
            sBlipLetters[w] = 0;
            continue;
        }
        d = (const MsgWinData*)win->userdata->address;
        for (e = 0; e < d->charCount && e < (int)(sizeof(d->entries) / sizeof(d->entries[0])); e++) {
            if (d->entries[e].glyph < 0xFF00 && d->entries[e].glyph != ' ' && (s32)d->entries[e].time <= d->playTime) {
                letters++;
            }
        }
        if (letters < sBlipLetters[w]) {
            sBlipLetters[w] = letters; /* a new page */
        }
        if ((win->flags & WINDOW_FLAG_PRINTING) && letters >= sBlipLetters[w] + 3) {
            int set, pitch, id;
            char name[24];
            voiceOf(d, &set, &pitch);
            snprintf(name, sizeof(name), "SFX_MESSAGE%d_%d", set, 1 + (sBlipTurn++ % kSetSize[set]));
            if (sBlipTurn == 1) {
                printf("[blips] the first talk blip: %s\n", name); /* once (the tests' signal) */
            }
            id = psndSFXOn(name);
            if (id >= 0) {
                psndSFX_vol(id, 60);
                if (pitch != 0) {
                    psndSFX_pit(id, (s16)pitch);
                }
            }
            sBlipLetters[w] = letters;
        } else if (!(win->flags & WINDOW_FLAG_PRINTING)) {
            sBlipLetters[w] = letters;
        }
    }
}

void portRestorationsFrame(void) {
    portMessageDumpFrame(); /* Language > Game text packs' template, when asked for */
    if (gp == NULL) {
        return;
    }
    frogs();
    portRestorationsApply(); /* the other restorations (port/include/port/restorations.h) */
    if (seqGetSeq() == SEQ_GAME) {
        blips();
    }
}
