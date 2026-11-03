# The game functions mods can hook (tools/port/gen_hooks.py; docs/MODDING.md,
# "Code mods"): the ones the GameCube mods hook most (docs/MODDING_PLAN.md,
# section 2), and the ones the port's own services stand on. After editing:
#   python tools/port/gen_hooks.py --apply
# (configure both Aurora builds first: it reads their compile flags).

# Each frame, sequences, maps
marioStMain
seqSetSeq
seq_mapChangeMain
marioEntry
partyEntry2Pos

# Messages and menus
msgSearch
statusWinDisp
winItemMain
winLogMain
DrawWeaponWin

# Items, coins, badges, story flags
itemEntry
pouchGetItem
pouchRemoveItem
pouchReviseMarioParam
pouchSetCoin
pouchAddCoin
pouchAddStarPiece
pouchEquipCheckBadge
swSet
swByteSet

# Shops (the area's goods list at set-up)
evt_shop_setup

# Battles: set-up, units, damage, action commands, audience, rewards
Btl_UnitSetup
BtlUnit_Entry
npcSetupBattleInfo
btlseqFirstAct
BattleCheckConcluded
BattlePreCheckDamage
BattleCheckDamage
BattleCalculateDamage
BattleCalculateFpDamage
BattleDamageDirect
BattleSetStatusDamageFromWeapon
BtlUnit_GetWeaponCost
BtlUnit_GetCoin
BattleStoreExp
BattleActionCommandCheckDefence
BattleActionCommandSetDifficulty
BattleAudience_ApRecoveryBuild

# Sound
psndBGMOn_f_d

# The Practice Codes' hooks (docs/MODDING.md, "Porting a GameCube mod")
titleMain
seq_loadMain
winRootMain
marioKeyOn
battle_init
BattlePadManager
pouchRemoveItemIndex
viPostCallback
fbatHitCheck
psndSFXOff
psndENVOn_f_d
scissor_disp_control
scissor_timer_main
winMarioDisp
hitCheckVecFilter
npcNameToPtr
mobjNameToPtr
animPoseMain
mapDataPtr
evt_bero_get_info
relSetEvtAddr
SoundEfxPlayEx

# --- Generated sets (tools/port/gen_hooks.py): every function a mod can name
# (declared in a game header) defined in these sources, with the same
# signature in every version. Slot numbers: port/mods/hook_slots.txt.

# Battles: the sequence, units, damage, items, audience, action commands, the
# Special moves, the battle's events
@files src/battle/*.c
@files src/battle/ac/*.c
@files src/battle/sac/*.c
@files src/unit/*.c
@files src/unit/party/*.c
# The field: Mario, his motions, partners, NPCs, items and objects on the map
@files src/mario/*.c
@files src/motion/*.c
@files src/party.c
@files src/party/*.c
@files src/npc_event.c
@files src/driver/npcdrv.c
@files src/driver/itemdrv.c
@files src/driver/mobjdrv.c
@files src/driver/swdrv.c
@files src/driver/seqdrv.c
@files src/driver/msgdrv.c
@files src/driver/casedrv.c
@files src/driver/fadedrv.c
@files src/driver/envdrv.c
@files src/driver/bgdrv.c
@files src/driver/windowdrv.c
@files src/manager/winmgr.c
@files src/manager/cardmgr.c
# Events: the scripts' user functions (evt_*), the menus, the sequences
@files src/event/*.c
@files src/event.c
@files src/window/*.c
@files src/sequence/*.c
@files src/statuswindow.c
@files src/pmario_sound.c
@files src/peach.c
@files src/koopa.c
@files src/koopa_hit.c
@files src/kpa_act.c
@files src/nameent.c
@files src/countdown.c
@files src/data/*.c

# Hot paths left out (measured with PORT_HOOK_PROFILE: called many times every
# frame); docs/MODDING.md, "Hook slots", has how to measure and extend
# (US build, 6000 field frames of random play on gor_01 and 7200 in a battle
# on hei_01: average calls per frame; hand-picked slots above stay)
-marioGetPtr                # 150 (field)
-_ismbblead                 # 43, per character drawn
-mobjHitObjPtrToPtr         # 29
-partyGetPtr                # 21
-pouchGetPtr                # 13
-marioGetPartyId            # 11
-seqGetSeq                  # 11
-sinfd                      # 5
-BattleAudienceBaseGetPtr   # 596 (battle)
-BattleAudienceGetPtr       # 335
-BattleAudience_GetExist    # 250
-BattleGetUnitPtr           # 169
-BtlUnit_GetStatus          # 110
-BtlUnit_CheckStatus        # 105
-btlDispGXPoint2D           # 12
-psndSFXChk                 # 10
-BtlUnit_GetPos             # 9
-BtlUnit_GetPartsWorldPos   # 7
-BtlUnit_GetPartsOffsetPos  # 6
