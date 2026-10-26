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
