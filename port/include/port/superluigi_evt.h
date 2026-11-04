#ifndef PORT_SUPERLUIGI_EVT_H
#define PORT_SUPERLUIGI_EVT_H

/* The game's script functions Super Luigi's scripts call, and the framework's
 * own (docs/SUPER_LUIGI.md, "The API": what each takes) */
#include "evt/evt_cmd.h"
#include "evt/evt_bero.h"
#include "evt/evt_case.h"
#include "driver/casedrv.h"
#include "evt/evt_cam.h"
#include "evt/evt_eff.h"
#include "evt/evt_fade.h"
#include "evt/evt_item.h"
#include "evt/evt_map.h"
#include "evt/evt_mario.h"
#include "evt/evt_mobj.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_party.h"
#include "evt/evt_pouch.h"
#include "evt/evt_seq.h"
#include "evt/evt_snd.h"
#include "evt/evt_story.h"
#include "evt/evt_sub.h"
#include "driver/npcdrv.h"
#include "driver/animdrv.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "party/party_move.h"
#include "party/party_hit.h"
#include "pmario_sound.h"

/* superluigi_aurora.c */
USER_FUNC(evt_sl_title_card);
USER_FUNC(evt_sl_chapter_end);
USER_FUNC(evt_sl_mode);
USER_FUNC(evt_sl_friend_join);
USER_FUNC(evt_sl_friend_slot);
USER_FUNC(evt_sl_where);
USER_FUNC(evt_sl_npc_fly);
USER_FUNC(evt_sl_enemy);
USER_FUNC(evt_sl_buttons);
USER_FUNC(evt_sl_map_look);
USER_FUNC(evt_sl_map_wash);
USER_FUNC(evt_sl_mobj_look);
USER_FUNC(evt_sl_point);
USER_FUNC(evt_sl_point_end);
USER_FUNC(evt_sl_point_off);
USER_FUNC(evt_sl_ability_done);
/* superluigi_battle_aurora.c */
USER_FUNC(evt_sl_force_command);
USER_FUNC(evt_sl_prompt);
/* sl_assets_aurora.c */
USER_FUNC(evt_sl_friend_icons);
/* superluigi_blooey_aurora.c */
USER_FUNC(evt_sl_swim_point);
USER_FUNC(evt_sl_swim_clear);
/* superluigi_statue_aurora.c */
USER_FUNC(evt_sl_lava_zone);
USER_FUNC(evt_sl_boss_arm);

#endif
