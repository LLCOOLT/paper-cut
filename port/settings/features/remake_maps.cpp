// The remake's map changes (port/include/port/remake_maps.h, docs/REMAKE_MAPS.md):
// a switch each, off by default, on the settings page's Enhancements tab.
#include "port/remake_maps.h"
#include "port/settings.h"

namespace {

constexpr const char* kTab = "Enhancements";
constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;

const bool sAdded = [] {
  portRemakeMap(0); // declares the settings
  portSettingsAddToggle(kTab, "Warp pipes in the sewers",
                        "The Director's Cut sewer pipe rooms open from the room before the Pit of 100 Trials, and "
                        "their last pipe asks where to: Rogueport's plaza, and each chapter's town once its Crystal "
                        "Star is in. Travel to stays as it is.",
                        kPlay, portRemakeMapKey(PORT_RM_WARP_PIPES),
                        "remake warp pipe sewers hub travel crystal star rogueport");
  portSettingsAddToggle(kTab, "The remake's shortcut pipes",
                        "A pipe where Last Stand P was in Hooktail Castle, down to the castle's gate; one in the "
                        "Creepy Steeple's basement to Twilight Town; one in the Palace of Shadow's last hall back "
                        "to its gate.",
                        kPlay, portRemakeMapKey(PORT_RM_SHORTCUT_PIPES),
                        "remake shortcut pipe hooktail castle creepy steeple twilight town palace of shadow");
  portSettingsAddToggle(kTab, "Excess Express skips Riverside",
                        "After chapter 6, going to sleep on the train asks whether to stop at Riverside Station or "
                        "ride straight through to Poshley Heights or Rogueport.",
                        kPlay, portRemakeMapKey(PORT_RM_EXPRESS),
                        "remake excess express train riverside station skip poshley heights");
  portSettingsAddToggle(kTab, "Chapter shortcuts",
                        "Pipes for the long walks back and forth: Twilight Town and the Creepy Steeple both ways "
                        "once the Steeple has been reached, and the Palace of Shadow's riddle tower back to its "
                        "gate.",
                        kPlay, portRemakeMapKey(PORT_RM_CHAPTER_SHORTCUTS),
                        "remake chapter shortcut pipe backtracking twilight trail keelhaul key fortress palace");
  portSettingsAddToggle(kTab, "Keelhaul Key and the Grotto",
                        "Jump stands between the castaways' camp and Skull Rock once Bobbery has joined, the "
                        "Pirate's Grotto's floating barrel back after chapter 5, and the jungle's Fuzzies stay.",
                        kPlay, portRemakeMapKey(PORT_RM_KEELHAUL),
                        "remake keelhaul key pirate's grotto barrels jump stand fuzzies");
  portSettingsAddToggle(kTab, "Jump down the Steeple's stairs",
                        "The stairs in the Creepy Steeple's stair room lose their side fences, so Mario can jump "
                        "down from them.", kPlay,
                        portRemakeMapKey(PORT_RM_STEEPLE_STAIRS), "remake creepy steeple stairs jump down");
  portSettingsAddToggle(kTab, "A shorter hunt for General White",
                        "In chapter 7, Petalburg's hint is enough for Twilight Town's: Glitzville and Boggly Woods "
                        "are skipped on the way back to Fahr Outpost.",
                        kPlay, portRemakeMapKey(PORT_RM_TWILIGHT),
                        "remake general white hunt twilight town petalburg fahr outpost chapter 7");
  portSettingsAddToggle(kTab, "New Save and Heart Blocks",
                        "The Save Blocks and Heart Blocks the remake added: Boggly Woods, Twilight Trail, the "
                        "Steeple, the Pirate's Grotto, the Excess Express, Riverside Station, the X-Naut Fortress "
                        "and the Palace of Shadow.",
                        kPlay, portRemakeMapKey(PORT_RM_BLOCKS),
                        "remake save block heart block recovery new");
  portSettingsAddToggle(kTab, "New ambush battles",
                        "The remake's new ambushes: on the path to Shhwonk Fortress, in the Pirate's Grotto and "
                        "an Elite X-Naut in the X-Naut Fortress.",
                        kPlay, portRemakeMapKey(PORT_RM_AMBUSHES),
                        "remake ambush battle shhwonk fortress pirate's grotto elite x-naut");
  portSettingsAddToggle(kTab, "Charlieton and a Grotto salesman",
                        "Charlieton sells in Boggly Woods by the Great Tree's pipe, and a Toad salesman waits at "
                        "the end of the Pirate's Grotto.",
                        kPlay, portRemakeMapKey(PORT_RM_MERCHANTS),
                        "remake charlieton shop boggly woods toad salesman pirate's grotto");
  return true;
}();

} // namespace
