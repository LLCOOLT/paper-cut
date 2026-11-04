#ifndef PORT_SUPERLUIGI_CH03_H
#define PORT_SUPERLUIGI_CH03_H

/* Super Luigi, chapter 3 (docs/super_luigi/ch3.md): what its files share
 * (superluigi_ch03_aurora.c the rooms, superluigi_revvo_aurora.c the bouts
 * and King Revvo, superluigi_race_aurora.c the race, superluigi_ch03_text
 * the text). */
#include "evt/evt_cmd.h"
#include "port/superluigi.h"

/* its flags: the framework's bank, SL_GSWF(3, n) (GSWF 7300 + n; docs/SUPER_LUIGI.md "Flags") */
#define SL3_FLAG_BASE SL_FLAG(3, 0)
#define SL3F(n) SL_GSWF(3, n)
enum {
    F_ARRIVED, F_AWARDS, F_FAN, F_SIGNED, F_GASKET, F_TORQUE_MET,
    F_BOUT1, F_BOUT2, F_TORQUE, F_SHUTTER, F_TUTORIAL_DONE,
    F_PLUG, F_GEARBOX, F_WHEEL, F_VENT, F_LIFT, F_CRANE,
    F_BOUT3, F_BOUT4, F_HINT, F_KART, F_KING, F_LICENSE_STAMP, F_NIGHT,
    F_RACE_START, F_PITSTOP, F_RACE_WON, F_CROWN,
    F_EX1, F_EX2, F_EX3, F_GOLD, F_SILVER, F_BENCH, F_MUFFLER, F_BLASTARD,
    F_31_COIN, F_31_VOLT, F_31_PIECE, F_31_HIDDEN, F_32_TUTOR, F_33_PIECE,
    F_35_MUSH, F_36_SHROOM, F_36_SHINE, F_36_BLOWN, F_37_FPPLUS, F_37_PIECE,
    F_38_LOCKER, F_38_SPIKE, F_40_COIN1, F_40_COIN2, F_40_BLOWN, F_41_SHINE, F_41_PIECE,
    F_41_SEEN, F_42_MULTI, F_43_COIN, F_43_ECHO, F_PRACTICED, F_GOLD_SHINE,
    F_BENCH_RAGE, /* the bench's Thunder Rage picked up (it lies there till then) */
    F_NEVER = 99, /* never set: "not yet" for mobjs that ask for a flag */
};

/* the game's NPC tribes the scenes use, in Shift-JIS (see superluigi_sjis.h) */
#define SJ3_TOAD "\x83\x4C\x83\x6D\x83\x73\x83\x49"                    /* キノピオ */
#define SJ3_TOAD_G "\x83\x4C\x83\x6D\x83\x73\x83\x49\x97\xCE"          /* キノピオ緑 */
#define SJ3_TOAD_F "\x83\x4C\x83\x6D\x83\x73\x83\x49\x8F\x97"          /* キノピオ女 */
#define SJ3_KOOPA "\x83\x6D\x83\x52\x83\x6D\x83\x52"                   /* ノコノコ */
#define SJ3_PIANTA "\x83\x8D\x83\x65\x83\x93"                          /* ロテン */
#define SJ3_BOMB "\x83\x7B\x83\x80\x95\xBA"                            /* ボム兵 */
#define SJ3_CLUBBA "\x83\x7B\x83\x8D\x83\x68\x81\x5B"                  /* ボロドー */
#define SJ3_LAKITU "\x83\x57\x83\x85\x83\x51\x83\x80"                  /* ジュゲム */
#define SJ3_SHADY "\x83\x45\x83\x89\x83\x6D\x83\x52"                   /* ウラノコ */
#define SJ3_HAMMER "\x83\x6E\x83\x93\x83\x7D\x81\x5B\x83\x75\x83\x8D\x83\x58" /* ハンマーブロス */
#define SJ3_BOOMERANG "\x83\x75\x81\x5B\x83\x81\x83\x89\x83\x93\x83\x75\x83\x8D\x83\x58" /* ブーメランブロス */
#define SJ3_KOOPATROL "\x83\x67\x83\x51\x83\x6D\x83\x52\x83\x47\x81\x5B\x83\x58" /* トゲノコエース */
#define SJ3_TORQUE "\x83\x67\x83\x8B\x83\x4E"                          /* トルク */
#define SJ3_BLOOEY "\x83\x4C\x83\x62\x83\x4E"                          /* キック */
#define SJ3_JERRY "\x83\x7B\x83\x80\x95\xBA"                           /* ボム兵 */

/* superluigi_race_aurora.c: the scenes' pieces */
USER_FUNC(sl3_luigi_crown);  /* in: 1 Luigi wears the crown (c_luigi_crown), 0 off */
USER_FUNC(sl3_spotlight);    /* in: on, x, z: the tag's spotlight on the floor */
USER_FUNC(sl3_caption);      /* in: a tag (0 off): a small caption at the top left */
USER_FUNC(sl3_still);        /* in: 1 the crash, 2 Luigi's scream, 0 off: a held still, grey */
USER_FUNC(sl3_spin_landed);  /* in: x, z, radius; out: 1 if a Spin Jump just landed there */
USER_FUNC(evt_luigi_story_pose); /* port/src/common/luigi_pc.c */
void portSl3PosterSets(void);    /* the posters as the race goes */

#endif
