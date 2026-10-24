# Memory card save slot payload (CardSaveSlot.data, src/manager/cardmgr.c).
# The card keeps the GameCube layout (big-endian, 32-bit pointers) so saves
# move between the PC port and Dolphin/GameCube; port/src/common/cardsave_pc.c
# converts slot images both ways with these descriptors (decode on read,
# portEncode on write). Pieces the generator cannot describe are handled there:
# GlobalWork.savePlayerPos (anonymous Vec), PiantaParlorWork (2-D u32 arrays,
# bigger on US, which moves the informant block) and InformantUnknown
# (bytes only, anonymous struct).
format cardsave
header manager/cardmgr.h

struct GlobalWork
  relocationBase null      # runtime only; cardCopy2Main clears it
  mapAlloc null            # runtime only; cardCopy2Main keeps the live one
  fbatData null            # runtime only; cardCopy2Main keeps the live one
  savePlayerPos skip       # Vec (anonymous struct): cardsave_pc.c

struct CountdownWork

struct lottery_info

struct PouchData

struct PouchPartyData

struct NpcDeadInfo

struct BadgeShopWork
