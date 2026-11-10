# Big GameCube mods and the port's mod tools

This checks three large GameCube mods of the game against the port's tools for code mods (docs/MODDING.md, "GameCube mods"). Paper Cut never bundles, downloads or installs any of them: a player who wants one converts their own checkout of its source with the tools, as the mod's license allows. All three are GPL-3.0; a conversion stays a local build of that mod and is never shipped with Paper Cut.

The status below comes from each mod's public repository and its build set-up, not from a conversion run, which is the next step for each.

| Mod | What it is built on | Expected to run | What the tools lack |
| --- | --- | --- | --- |
| [Infinite Pit](https://github.com/jdaster64/ttyd-infinite-pit) | ttyd-tools: C++ against the US game's machine code, built into a REL loaded from a `.gci` (a hacked save or a GCI-loader code) | The C++: the pit's floors, rewards, options menu and its stats, through mod_fields / mod_headers as for the Practice Codes | Its instruction patches each need a hook slot (`port/mods/hooks.spec`); the battle code it patches inside functions (enemy stats, damage, the pit's floor logic) is the bulk. Its save layout extension at GameCube offsets must use the decomp's fields. US only: its symbol list is for G8ME. |
| [Tower of Trials](https://github.com/jdaster64/ttyd-tot) | The same framework and author; ships as an ISO patcher (Melvin patcher, auto-bootloader), new maps exported from Blender, linked RELs (elf2rel with REL linking), its own save file of 17 blocks | Its C++ as above, once converted | New maps: the port reads a map's files from a mod's `overlay/` (docs/MODDING.md), but the patcher's output (a whole patched ISO, its auto-bootloader in `main.dol`) has no route: the files would have to come from a build of the mod's own data, not the patched disc. REL-to-REL linking at GameCube addresses: the tools bind a REL's symbols by module, section and offset (`--rel-dir`), but a REL the mod adds has no module the port knows. Its separate save file: the port's card holds one file per release (`mariost_save_file`); a second file needs the card code to allow it. |
| TTYD Plus | No public repository found under this name (searched November 2026) | | Nothing to check until its source is public. |

## What would close the gaps

- **Hook slots** for the functions these mods patch most (the battle's damage and status functions, the pit's floor and reward code): each is one line in `port/mods/hooks.spec`; a list is best made from a conversion run's log, which names every skipped patch.
- **A mod's own RELs as host maps**: the mods' new maps already work for Lua and data mods (`port/mods/mods.cpp`); a converted mod's map REL could register the same way.
- **More than one file on the card** for mods with their own save (Tower of Trials).
- **Starting the mod**: a REL loader ran these once the game was up; converted, they start before the first frame, so their set-up belongs in a run-once `onFrame` (docs/MODDING.md).
