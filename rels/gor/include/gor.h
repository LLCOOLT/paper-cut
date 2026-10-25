#pragma once

#include "dolphin/types.h"
#include "dolphin/mtx.h"
#include "driver/npcdrv.h"
#include "evt/evt_word.h"
#include "manager/evtmgr.h"

extern EvtWord all_juyoitem_lecture_rel[];
extern EvtWord all_party_lecture[];
extern NpcTerritoryInfo btl_gor_setup_no_tbl[];
extern NpcSetupBattleInfo btlsetup_gor_tbl[];
extern const char* door_data_05_npc[];
extern const char* door_data_05_npc_01[];
extern EvtWord gor_00_init_evt[];
extern EvtWord gor_01_init_evt[];
extern EvtWord gor_02_init_evt[];
extern EvtWord gor_03_init_evt[];
extern EvtWord gor_04_init_evt[];
extern EvtWord gor_10_init_evt[];
extern EvtWord gor_11_init_evt[];
extern EvtWord gor_12_init_evt[];
extern EvtWord gor_irai_init[];
extern EvtWord gor_villager_urouro[];
extern EvtWord luigi_gor_first_talk[];
extern BattleUnitKind unit_lecture_frankli_sac;
extern BattleUnitKind unit_monban;

typedef struct CookRecipe { const char* a; const char* b; const char* out; } CookRecipe;
extern CookRecipe R_cookbook_recipe_table[];
extern s32 R_mystery_cook_list[];
typedef struct SimpleRecipe { const char* ingredient; const char* result; const char* result2; } SimpleRecipe; /* naniga_okorukana lookup: results go to args[1]/args[2] */
extern SimpleRecipe R_simple_recipe_table[];
extern f32 gor_ship_puka_data[];
extern s32* irai_item_table[];
extern const char* irai_msg_client[];
extern const char* irai_msg_contents[];
extern const char* irai_msg_kakunin[];
extern s32 special_cook_list[];
extern Vec suifu100kaidanDOWN[];
extern Vec suifu100kaidanUP[];

/* the slot machine (gor_03), allocated by evt_goryuugijo.c */
typedef struct SlotWork { u32 flags; s32 phase; f32 reberAngle; f32 angle[2][3]; f32 speed[3]; s8 state[3]; s8 face[2][3]; s8 stopCount[3]; s32 wait[3]; s32 extra[3]; s32 counter; s32 neonCount; s32 monteCount; s32 nowSlot; s32 caseId[2]; EventEntry* monteEvt; s32 sfxId; s16 cursor; } SlotWork;
