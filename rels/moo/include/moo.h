#pragma once

#include "dolphin/types.h"
#include "driver/npcdrv.h"
#include "evt/evt_word.h"

extern NpcTerritoryInfo btl_moo_setup_no_tbl[];
extern NpcSetupBattleInfo btlsetup_moo_tbl[];
extern EvtWord moo_00_init_evt[];
extern EvtWord moo_01_init_evt[];
extern EvtWord moo_02_init_evt[];
extern EvtWord moo_03_init_evt[];
extern EvtWord moo_04_init_evt[];
extern EvtWord moo_05_init_evt[];
extern EvtWord moo_06_init_evt[];
extern EvtWord moo_07_init_evt[];
extern BattleUnitKind unit_kuriboo;
extern BattleUnitKind unit_sinemon;

#if !defined(TARGET_PC) || defined(VERSION_G8ME01) /* JP: every file defines its own static copy (static after extern: MSVC-only extension); US: moo_00's, shared */
extern EvtWord rock_main[];
#endif
