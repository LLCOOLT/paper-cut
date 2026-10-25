#include "data_tables.h"
#include "evt/evt_cmd.h"
#include "evt/evt_cam.h"
#include "evt/evt_fade.h"
#include "evt/evt_npc.h"
#include "evt/evt_snd.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/itemdrv.h"
#include "driver/npcdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "pmario_sound.h"
#include <string.h>
#include "evt/evt_kinopio.h"
#include "eff/eff_magic1_n64.h"
#include "libc_decls.h"

extern ItemEntry* itemEntry(const char* name, s32 itemId, s32 type, s32 evtId, EvtWord a5, f32 x, f32 y, f32 z);

extern USER_FUNC(evt_eff);
extern USER_FUNC(evt_eff_fukidashi);
extern USER_FUNC(evt_fade_end_wait);
extern USER_FUNC(evt_fade_set_spot_pos);
extern USER_FUNC(evt_mario_adjust_dir);
extern USER_FUNC(evt_mario_dispflag_onoff);
extern USER_FUNC(evt_mario_flag_onoff);
extern USER_FUNC(evt_mario_get_exparty);
extern USER_FUNC(evt_mario_get_party);
extern USER_FUNC(evt_mario_get_pos);
extern USER_FUNC(evt_mario_goodbye_party);
extern USER_FUNC(evt_mario_jump_pos);
extern USER_FUNC(evt_mario_key_onoff);
extern USER_FUNC(evt_mario_mov_pos2);
extern USER_FUNC(evt_mario_set_dir_npc);
extern USER_FUNC(evt_mario_set_party_pos);
extern USER_FUNC(evt_mario_set_pos);
extern USER_FUNC(evt_msg_print);
extern USER_FUNC(evt_msg_print_add);
extern USER_FUNC(evt_msg_print_insert);
extern USER_FUNC(evt_msg_select);
extern USER_FUNC(evt_npc_set_tribe);
extern USER_FUNC(evt_party_run);
extern USER_FUNC(evt_party_set_dir_npc);
extern USER_FUNC(evt_party_set_pos);
extern USER_FUNC(evt_pouch_add_coin);
extern USER_FUNC(evt_pouch_all_party_recovery);
extern USER_FUNC(evt_pouch_check_item);
extern USER_FUNC(evt_pouch_mario_recovery);
extern USER_FUNC(evt_pouch_remove_item);
extern USER_FUNC(evt_set_dir_to_target);
extern USER_FUNC(evt_snd_bgmoff);
extern USER_FUNC(evt_snd_sfxon);
extern USER_FUNC(evt_sub_get_coin);
extern USER_FUNC(evt_sub_get_mapname);
extern USER_FUNC(evt_win_coin_off);
extern USER_FUNC(evt_win_coin_on);

typedef struct KinopioParty {
	u16 flags;
	s16 maximumHP;
	s16 pad4;
	s16 currentHP;
	s16 pad8[3];
} KinopioParty;

typedef s32 (*KinopioGetter)(void);

/* evt_kinopio_setup's table (data_tables.h): its messages and bed events are
 * pointers (8 bytes on 64-bit) */
typedef KinopioData KinopioWork;

USER_FUNC(N_cameraman_on);
USER_FUNC(snd_on);
USER_FUNC(snd_off);
USER_FUNC(breakfast);
USER_FUNC(offset);
static USER_FUNC(paper_off);
static USER_FUNC(paper_on);
USER_FUNC(get_from_bed_evt);
USER_FUNC(get_to_bed_evt);
USER_FUNC(get_cam);
USER_FUNC(get_npctalk);
USER_FUNC(get_beddeg);
USER_FUNC(get_bedside);
USER_FUNC(get_bed);
USER_FUNC(get_npcname);
USER_FUNC(get_ryoukin);
USER_FUNC(coin_chk);
USER_FUNC(power_chk);
USER_FUNC(msg_no);
USER_FUNC(evt_kinopio_setup);

//.sbss
static KinopioWork* work;

//.sdata
static KinopioWork** wp = &work;

USER_FUNC(evt_kinopio_setup) {
	KinopioWork* w = (KinopioWork*)evtGetValue(event, event->args[0]);
	NpcEntry* npc = npcNameToPtr(w->npcName);
	if (!animGroupBaseAsync("p_slit", 0, NULL)) {
		return 0;
	}
	npc->talkEvt = kinopio_house_talk_evt;
	*wp = w;
	return 2;
}

USER_FUNC(msg_no) {
	EvtWord* args = event->args;
	s32 idx = evtGetValue(event, args[0]);
	evtSetValue(event, args[1], (EvtWord)(*wp)->msg[idx]);
	return 2;
}

USER_FUNC(power_chk) {
	EvtWord* args = event->args;
	s32 ok;
	s32 i;
	KinopioParty* pouch = (KinopioParty*)pouchGetPtr();
	ok = 1;
	for (i = 0; i < 8; i++) {
		if ((pouch[i].flags & 1) && pouch[i].currentHP != pouch[i].maximumHP) {
			ok = 0;
			break;
		}
	}
	if (((KinopioGetter)pouchGetHP)() == ((KinopioGetter)pouchGetMaxHP)() && ((KinopioGetter)pouchGetFP)() == ((KinopioGetter)pouchGetMaxFP)() && ((KinopioGetter)pouchGetAP)() == ((KinopioGetter)pouchGetMaxAP)() && ok) {
		evtSetValue(event, args[0], 1);
	} else {
		evtSetValue(event, args[0], 0);
	}
	return 2;
}

USER_FUNC(coin_chk) {
	EvtWord* args = event->args;
	if (pouchGetCoin() < (*wp)->ryoukin) {
		evtSetValue(event, args[0], 1);
	} else {
		evtSetValue(event, args[0], 0);
	}
	return 2;
}

USER_FUNC(get_ryoukin) {
	evtSetValue(event, event->args[0], (*wp)->ryoukin);
	return 2;
}

USER_FUNC(get_npcname) {
	evtSetValue(event, event->args[0], (EvtWord)(*wp)->npcName);
	return 2;
}

USER_FUNC(get_bed) {
	EvtWord* args = event->args;
	evtSetFloat(event, args[0], (*wp)->bed.x);
	evtSetFloat(event, args[1], (*wp)->bed.y);
	evtSetFloat(event, args[2], (*wp)->bed.z);
	return 2;
}

USER_FUNC(get_bedside) {
	EvtWord* args = event->args;
	evtSetFloat(event, args[0], (*wp)->bedside.x);
	evtSetFloat(event, args[1], (*wp)->bedside.y);
	evtSetFloat(event, args[2], (*wp)->bedside.z);
	return 2;
}

USER_FUNC(get_beddeg) {
	evtSetFloat(event, event->args[0], (*wp)->bedDeg);
	return 2;
}

USER_FUNC(get_npctalk) {
	EvtWord* args = event->args;
	evtSetFloat(event, args[0], (*wp)->npcTalk.x);
	evtSetFloat(event, args[1], (*wp)->npcTalk.y);
	evtSetFloat(event, args[2], (*wp)->npcTalk.z);
	return 2;
}

USER_FUNC(get_cam) {
	EvtWord* args = event->args;
	evtSetFloat(event, args[0], (*wp)->cam[0]);
	evtSetFloat(event, args[1], (*wp)->cam[1]);
	evtSetFloat(event, args[2], (*wp)->cam[2]);
	evtSetFloat(event, args[3], (*wp)->cam[3]);
	evtSetFloat(event, args[4], (*wp)->cam[4]);
	evtSetFloat(event, args[5], (*wp)->cam[5]);
	return 2;
}

USER_FUNC(get_to_bed_evt) {
	evtSetValue(event, event->args[0], (EvtWord)(*wp)->toBedEvt);
	return 2;
}

USER_FUNC(get_from_bed_evt) {
	evtSetValue(event, event->args[0], (EvtWord)(*wp)->fromBedEvt);
	return 2;
}

static USER_FUNC(paper_on) {
	EvtWord* args = event->args;
	f32 amount = evtGetFloat(event, *args++);
	MarioWork* mario = marioGetPtr();
	if (isFirstCall) {
		marioPaperOn("p_slit");
		marioChgPose("M_S_1");
		marioChgPaper("PM_S_3A");
		event->userdata[0] = 0;
	}
	if (event->userdata[0] < 30) {
		f32 ang;
		if (strcmp(gp->currentMapName, "pik_04") == 0) {
			ang = 6.2831855f * amount / 360.0f;
			mario->position.x -= 0.2f * (f32)cos(ang);
			mario->position.z -= 0.2f * (f32)sin(ang);
		} else {
			ang = 6.2831855f * amount / 360.0f;
			mario->position.x += 0.2f * (f32)cos(ang);
			mario->position.z += 0.2f * (f32)sin(ang);
		}
		event->userdata[0]++;
	}
	mario->displayDirectionTarget = amount;
	mario->displayDirection = amount;
	return 0;
}

static USER_FUNC(paper_off) {
	marioPaperOff();
	return 2;
}

USER_FUNC(offset) {
	EvtWord* args = event->args;
	f32 dist = evtGetFloat(event, args[0]);
	f32 ang = 6.2831855f * (90.0f + camGetPtr(CAMERA_3D)->viewYaw) / 360.0f;
	evtSetFloat(event, args[1], dist * -(f32)sin(ang));
	evtSetFloat(event, args[2], dist * (f32)cos(ang));
	return 2;
}

USER_FUNC(breakfast) {
	ItemEntry* item = itemNameToPtr("breakfast");
	if (item == NULL || !(item->flags & 1)) {
		KinopioWork* w = *wp;
		if (w->breakfastItem != 0) {
			itemEntry("breakfast", w->breakfastItem, 0x10, -1, 0, w->breakfastPos.x, w->breakfastPos.y,
			          w->breakfastPos.z);
		}
	}
	return 2;
}

USER_FUNC(snd_off) {
	psndSetFlag(0x80);
	return 2;
}

USER_FUNC(snd_on) {
	psndClearFlag(0x80);
	return 2;
}

USER_FUNC(N_cameraman_on) {
	NpcEntry* npc = npcNameToPtr("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223");
	animPoseSetMaterialFlagOn(npc->poseId, 0x1800);
	return 2;
}

//.data
EVT_BEGIN(kinopio_house_talk_evt)
    CALL(evt_mario_key_onoff, 0)
    CALL(get_ryoukin, -29999990)
    CALL(get_npcname, -29999989)
    CALL(evt_win_coin_on, 0, -29999988)
    CALL(msg_no, 0, -30000000)
    CALL(evt_msg_print_insert, 0, -30000000, 0, -29999989, -29999990)
    CALL(msg_no, 6, -30000000)
    CALL(evt_msg_select, 0, -30000000)
    IF_INT_EQUAL(-30000000, 1)
        CALL(msg_no, 2, -30000000)
        CALL(evt_msg_print_add, 0, -30000000)
        CALL(evt_mario_key_onoff, 1)
        CALL(evt_win_coin_off, -29999988)
        RETURN
    END_IF
    CALL(power_chk, -30000000)
    IF_INT_EQUAL(-30000000, 1)
        CALL(msg_no, 1, -30000000)
        CALL(evt_msg_print_add, 0, -30000000)
        CALL(msg_no, 6, -30000000)
        CALL(evt_msg_select, 0, -30000000)
        IF_INT_EQUAL(-30000000, 1)
            CALL(msg_no, 2, -30000000)
            CALL(evt_msg_print_add, 0, -30000000)
            CALL(evt_mario_key_onoff, 1)
            CALL(evt_win_coin_off, -29999988)
            RETURN
        END_IF
    END_IF
    SET_VALUE(-70000000, 0)
    CALL(evt_pouch_check_item, 164, -30000000)
    IF_INT_GREATER(-30000000, 0)
        CALL(msg_no, 7, -30000000)
        CALL(evt_msg_print_add, 0, -30000000)
        CALL(msg_no, 8, -30000000)
        CALL(evt_msg_select, 0, -30000000)
        IF_INT_EQUAL(-30000000, 0)
            SET_VALUE(-70000000, 1)
            CALL(evt_pouch_remove_item, 164, -30000000)
        END_IF
    END_IF
    IF_INT_EQUAL(-70000000, 0)
        CALL(coin_chk, -30000000)
        IF_INT_EQUAL(-30000000, 1)
            CALL(msg_no, 4, -30000000)
            CALL(evt_msg_print_add, 0, -30000000)
            CALL(evt_mario_key_onoff, 1)
            CALL(evt_win_coin_off, -29999988)
            RETURN
        END_IF
        CALL(get_ryoukin, -29999990)
        MULTIPLY(-29999990, -1)
        CALL(evt_pouch_add_coin, -29999990)
        WAIT_MS(1000)
    END_IF
    CALL(evt_win_coin_off, -29999988)
    CALL(msg_no, 3, -30000000)
    CALL(evt_msg_print_add, 0, -30000000)
    CALL(get_npcname, -30000000)
    CALL(evt_npc_flag_onoff, 1, -30000000, 32)
    CALL(evt_mario_get_party, -29999989)
    CALL(evt_mario_get_exparty, -29999988)
    CALL(evt_mario_get_pos, 0, -29999987, -29999986, -29999985)
    CALL(evt_mario_goodbye_party, 1)
    CALL(evt_mario_goodbye_party, 0)
    CALL(get_to_bed_evt, -30000000)
    IF_INT_EQUAL(-30000000, 0)
        BEGIN_INLINE_EVENT
            WAIT_MS(1000)
            CALL(get_cam, -30000000, -29999999, -29999998, -29999997, -29999996, -29999995)
            CALL(evt_cam3d_evt_set, -30000000, -29999999, -29999998, -29999997, -29999996, -29999995, 0, 11)
        END_INLINE_EVENT
        CALL(get_bedside, -30000000, -29999999, -29999998)
        CALL(evt_mario_mov_pos2, -30000000, -29999998, -229907840)
    ELSE
        BEGIN_INLINE_EVENT_GET_ID(-30000000)
            CALL_CHILD(-30000000)
        END_INLINE_EVENT
        LOOP(0)
            IS_EVENT_RUNNING(-30000000, -29999999)
            IF_INT_EQUAL(-29999999, 0)
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
        CALL(get_cam, -30000000, -29999999, -29999998, -29999997, -29999996, -29999995)
        CALL(evt_cam3d_evt_set, -30000000, -29999999, -29999998, -29999997, -29999996, -29999995, 0, 11)
        WAIT_MS(500)
    END_IF
    CALL(evt_mario_get_pos, 0, -30000000, -29999999, -29999998)
    CALL(evt_snd_sfxon_3d, 131230, -30000000, -29999999, -29999998, 0)
    CALL(evt_mario_flag_onoff, 1, 1792)
    CALL(get_bed, -30000000, -29999999, -29999998)
    CALL(evt_mario_jump_pos, -30000000, -29999999, -29999998, -229590400, 0, -1000)
    CALL(evt_mario_set_pos, -30000000, -29999999, -29999998)
    CALL(evt_mario_adjust_dir)
    WAIT_MS(200)
    CALL(evt_mario_dispflag_onoff, 1, 4096)
    BEGIN_INLINE_EVENT_GET_ID(-29999990)
        CALL(get_beddeg, -30000000)
        CALL(paper_on, -30000000)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_mario_get_pos, 0, -30000000, -29999999, -29999998)
        CALL(evt_snd_sfxon_3d, 132026, -30000000, -29999999, -29999998, 0)
        WAIT_MS(200)
        CALL(evt_snd_sfxon_3d, 132027, -30000000, -29999999, -29999998, 0)
    END_INLINE_EVENT
    CALL(snd_off)
    CALL(evt_snd_bgmoff, 1024)
    CALL(evt_snd_bgmon, 1, STRING("BGM_FF_GO_SLEEPING1"))
#ifdef VERSION_G8ME01
    CALL(evt_fade_entry, 4, 6000, 0, 0, 0)
#else
    CALL(evt_fade_entry, 4, 3000, 0, 0, 0)
#endif
    CALL(evt_mario_get_pos, 0, -30000000, -29999999, -29999998)
    CALL(evt_fade_set_spot_pos, -30000000, -29999999, -29999998)
    CALL(evt_fade_end_wait)
    CALL(evt_fade_entry, 12, 0, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_pouch_mario_recovery)
    CALL(evt_pouch_all_party_recovery)
    CALL(breakfast)
    WAIT_MS(3000)
    DELETE_EVENT(-29999990)
    CALL(paper_off)
    CALL(evt_mario_dispflag_onoff, 0, 4096)
    IF_INT_NOT_EQUAL(-29999989, 0)
        CALL(get_bed, -30000000, -29999999, -29999998)
        CALL(evt_mario_set_party_pos, 0, -29999989, -30000000, -29999999, -29999998)
    END_IF
    IF_INT_NOT_EQUAL(-29999988, 0)
        CALL(get_bed, -30000000, -29999999, -29999998)
        CALL(offset, -229969280, -29999997, -29999996)
        ADD_FLOAT(-30000000, -29999997)
        ADD_FLOAT(-29999998, -29999996)
        CALL(evt_mario_set_party_pos, 1, -29999988, -30000000, -29999999, -29999998)
    END_IF
    CALL(evt_mario_flag_onoff, 0, 1792)
    CALL(get_bedside, -30000000, -29999999, -29999998)
    CALL(evt_mario_set_pos, -30000000, -29999999, -29999998)
    CALL(get_npcname, -30000000)
    CALL(evt_set_dir_to_target, -30000000, STRING("mario"))
    CALL(evt_mario_set_dir_npc, -30000000)
    CALL(evt_party_set_dir_npc, 0, -30000000)
    CALL(evt_party_set_dir_npc, 1, -30000000)
    CALL(evt_npc_flag_onoff, 1, -30000000, 32768)
    CALL(evt_cam3d_evt_off, 0, 11)
    WAIT_FRAMES(1)
    CALL(evt_npc_flag_onoff, 0, -30000000, 32768)
    CALL(evt_snd_bgmoff, 513)
    CALL(evt_snd_bgmon, 288, 0)
    CALL(snd_on)
    ADD(-169999999, 1)
    IF_INT_GREATER(-169999999, 100)
        SET_VALUE(-169999999, 100)
    END_IF
    IF_INT_EQUAL(-169999999, 50)
        CALL_CHILD(evt_50)
        CALL(get_npcname, -30000000)
        CALL(evt_npc_flag_onoff, 0, -30000000, 32)
        RETURN
    END_IF
    CALL(evt_mario_adjust_dir)
    CALL(evt_fade_entry, 11, 1000, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(get_from_bed_evt, -30000000)
    IF_INT_NOT_EQUAL(-30000000, 0)
        BEGIN_INLINE_EVENT_GET_ID(-29999990)
            CALL_CHILD(-30000000)
        END_INLINE_EVENT
        LOOP(0)
            IS_EVENT_RUNNING(-29999990, -29999989)
            IF_INT_EQUAL(-29999989, 0)
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
    END_IF
    CALL(evt_party_run, 0)
    CALL(evt_party_run, 1)
    CALL(get_npctalk, -30000000, -29999999, -29999998)
    CALL(evt_mario_mov_pos2, -30000000, -29999998, -229907840)
    CALL(evt_fade_end_wait)
    CALL(get_npcname, -29999989)
    CALL(evt_mario_set_dir_npc, -29999989)
    CALL(evt_party_set_dir_npc, 0, -29999989)
    CALL(get_npcname, -29999989)
    CALL(msg_no, 5, -30000000)
    CALL(evt_msg_print, 0, -30000000, 0, -29999989)
    CALL(get_npcname, -30000000)
    CALL(evt_npc_flag_onoff, 0, -30000000, 32)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

EVT_BEGIN(evt_50)
    CALL(get_npcname, -29999990)
    CALL(evt_npc_get_position, -29999990, -29999987, -29999986, -29999985)
    CALL(evt_npc_entry, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), STRING("c_jugem_c"))
    CALL(evt_npc_set_tribe, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), STRING("\203J\203\201\203\211\203}\203\223"))
    CALL(N_cameraman_on)
    CALL(evt_npc_flag_onoff, 1, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), 32768)
    CALL(evt_sub_get_mapname, -30000000)
    IF_STR_EQUAL(-30000000, STRING("bom_02"))
        CALL(evt_npc_set_position, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), -365, 30, -170)
    END_IF
    IF_STR_EQUAL(-30000000, STRING("gor_01"))
        CALL(evt_npc_set_position, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), 168, 154, -229)
    END_IF
    IF_STR_EQUAL(-30000000, STRING("muj_01"))
        CALL(evt_npc_set_position, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), 270, 20, -145)
    END_IF
    IF_STR_EQUAL(-30000000, STRING("nok_00"))
        CALL(evt_npc_set_position, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), -385, 25, -145)
    END_IF
    IF_STR_EQUAL(-30000000, STRING("pik_04"))
        CALL(evt_npc_set_position, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), 310, 1329, -100)
        CALL(evt_npc_set_position, -29999990, 310, 1329, -65)
    END_IF
    IF_STR_EQUAL(-30000000, STRING("usu_00"))
        CALL(evt_npc_set_position, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), 268, 0, -210)
    END_IF
    CALL(evt_set_dir_to_target, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"), STRING("mario"))
    CALL(evt_set_dir_to_target, -29999990, STRING("mario"))
    CALL(evt_mario_set_dir_npc, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"))
    CALL(evt_party_set_dir_npc, 0, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"))
    CALL(msg_no, 9, -30000000)
    CALL(evt_msg_print, 0, -30000000, 0, -29999990)
    CALL(get_bed, -30000000, -29999999, -29999998)
    CALL(evt_mario_set_pos, -30000000, -29999999, -29999998)
    ADD(-29999998, 5)
    CALL(evt_party_set_pos, 0, -30000000, -29999999, -29999998)
    CALL(evt_fade_entry, 9, 1000, 0, 0, 0)
    CALL(evt_fade_end_wait)
    WAIT_MS(500)
    CALL(evt_mario_get_pos, 0, -30000000, -29999999, -29999998)
    ADD(-29999999, 100)
    CALL(evt_eff, 0, STRING("confetti"), 3, -30000000, -29999999, -29999998, 600, 0, 0, 0, 0, 0, 0, 0)
    BEGIN_INLINE_EVENT
        CALL(evt_mario_get_pos, 0, -30000000, -29999999, -29999998)
        CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_FIND1"), -30000000, -29999999, -29999998, 0)
    END_INLINE_EVENT
    CALL(evt_eff_fukidashi, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_eff_fukidashi, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    WAIT_MS(1000)
    CALL(msg_no, 10, -30000000)
    CALL(evt_msg_print, 0, -30000000, 0, -29999990)
    CALL(get_bedside, -30000000, -29999999, -29999998)
    CALL(evt_mario_mov_pos2, -30000000, -29999998, -229907840)
    CALL(get_npctalk, -30000000, -29999999, -29999998)
    CALL(evt_mario_mov_pos2, -30000000, -29999998, -229907840)
    CALL(evt_mario_set_dir_npc, -29999990)
    CALL(msg_no, 11, -30000000)
    CALL(evt_msg_print, 0, -30000000, 0, -29999990)
    CALL(evt_mario_set_dir_npc, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"))
    CALL(evt_party_set_dir_npc, 0, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"))
    WAIT_MS(300)
    CALL(evt_snd_sfxon, STRING("SFX_EVT_50_HOTEL_SHUTTER1"), 0)
    CALL(evt_fade_entry, 8, 150, 255, 255, 255)
    CALL(evt_mario_get_pos, 0, -30000000, -29999999, -29999998)
    ADD(-29999999, 40)
    CALL(evt_fade_set_spot_pos, -30000000, -29999999, -29999998)
    CALL(evt_fade_end_wait)
    CALL(evt_fade_entry, 7, 150, 255, 255, 255)
    CALL(evt_fade_end_wait)
    CALL(msg_no, 12, -30000000)
    CALL(evt_msg_print, 0, -30000000, 0, -29999990)
    CALL(evt_mario_set_dir_npc, -29999990)
    CALL(evt_party_set_dir_npc, 0, -29999990)
    WAIT_MS(300)
    CALL(evt_sub_get_coin, 200)
    CALL(msg_no, 13, -30000000)
    CALL(evt_msg_print, 0, -30000000, 0, -29999990)
    CALL(evt_fade_entry, 10, 1000, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_npc_check_delete, STRING("\217h\211\256\202\314\203J\203\201\203\211\203}\203\223"))
    CALL(evt_npc_set_position, -29999990, -29999987, -29999986, -29999985)
    CALL(evt_set_dir_to_target, -29999990, STRING("mario"))
    CALL(evt_fade_entry, 9, 1000, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_party_run, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

