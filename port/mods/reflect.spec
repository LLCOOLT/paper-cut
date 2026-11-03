# The game data mods' patches can change (tools/port/gen_reflect.py,
# docs/MODDING.md "patches/"). Regenerate after editing:
#   python tools/port/gen_reflect.py
#
# externs <header glob>   every extern variable those headers declare
# symbol <name> <header>  a variable defined in src/ that no header declares;
#                         the header has its type

# Items and badges: prices, BP cost, HP/FP restored, icon, sort order
symbol itemDataTable data/item_data.h
# Item, badge and move attacks: damage, FP cost, targets, status effects
externs battle/battle_item_data.h
externs battle/battle_mario.h
# Mario's and the partners' battle units: HP, defense, weapons
externs unit/unit_mario.h
externs unit/party/*.h
