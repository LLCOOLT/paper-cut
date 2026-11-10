// Remake rules and the players' rule wishes (port/include/port/rules.h): the
// settings page's toggles, each its own setting and off by default, all
// marked "changes play" (Console-accurate mode locks them).
#include "port/gameplus.h"
#include "port/rules.h"
#include "port/settings.h"

namespace {

constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;
constexpr const char* kTab = "Enhancements";

struct Toggle {
  const char* label;
  const char* note;
  const char* key;
  const char* keywords;
};

// the remake's item and badge data
constexpr Toggle kRemakeData[] = {
    {"Remake: Sleepy Sheep", "Sleepy Sheep's sleep lasts 3 turns (was 5).", "rules_sleepy_sheep",
     "remake sleepy sheep sleep turns item"},
    {"Remake: Power Lift", "Power Lift raises Attack or Defense by 1 for every 4 arrows hit (was 5).",
     "rules_power_lift", "remake power lift special star arrows"},
    {"Remake: Sweet Treat", "Sweet Treat drops 2 Poison Mushrooms (was 3).", "rules_sweet_treat",
     "remake sweet treat poison mushroom special"},
    {"Remake: Mystery", "Mystery picks from a better pool: no Dried Shroom, more attack items, and stronger ones "
                        "after the fourth Crystal Star.",
     "rules_mystery_box", "remake mystery box item pool roulette"},
    {"Remake: lottery prizes", "The lottery's top prize is Lucky Day and the second Power Plus (swapped).",
     "rules_lottery", "remake lottery prize lucky day power plus"},
    {"Remake: Pit shop", "Charlieton in the Pit of 100 Trials also sells Ultra Shrooms and Jammin' Jelly.",
     "rules_pit_shop", "remake charlieton pit 100 trials shop ultra shroom jammin jelly"},
    {"Remake: Trouble rewards", "A Trouble that pays 20 coins pays 30.", "rules_trouble_coins",
     "remake trouble center reward coins"},
    {"Remake: boss coins", "Bosses pay coins: 50 in chapters 1 and 2, 100 in 3 to 5, 200 after (Bonetail 999).",
     "rules_boss_coins", "remake boss coins reward"},
    {"Remake: keep coins when fleeing", "Running away from a battle drops no coins.", "rules_flee_coins",
     "remake flee run away escape coins lose"},
    {"Remake: hazards pierce", "Stage hazards (fire jets, falling scenery, fog and the like) ignore defense.",
     "rules_hazard_pierce", "remake stage hazard defense pierce"},
    {"Remake: deep sleep", "A hit wakes a sleeping enemy or ally 20 % of the time (was 50 %).",
     "rules_deep_sleep", "remake sleep wake hit chance"},
    // the remake's enemy rules
    {"Remake: Bob-omb blasts", "A Bob-omb that blows up, on its own or in a chain, gives no Star Points (knocked "
                               "out, it still does).",
     "rules_bomb_no_sp", "remake bob-omb bomb chain explode star points exp"},
    {"Remake: Buzzies resist blasts", "Buzzy Beetles, Spike Tops and their kin are immune to explosions.",
     "rules_buzzy_blast", "remake buzzy beetle spike top explosion bobbery immune"},
    {"Remake: Flower Fuzzies", "Flower Fuzzies go for Mario's FP only, never the partner.", "rules_flower_fuzzy",
     "remake flower fuzzy fp drain target"},
    {"Remake: Bandit loot", "What a Bandit stole comes back to Mario when it runs off or the battle ends.",
     "rules_bandit_returns", "remake bandit steal item coins return"},
    {"Remake: First Strike on spiders", "Piders and Arantulas are easier to hit first in the field.",
     "rules_spider_strike", "remake pider arantula first strike field"},
    {"Remake: Swoopula Swoop", "Swoopulas sometimes Swoop: a plain hit that drains nothing.", "rules_swoopula_swoop",
     "remake swoopula swoop drain bite"},
    {"Remake: still Chain Chomps", "Chain Chomps stop hopping while a jump is aimed at them.", "rules_chomp_still",
     "remake chain chomp bounce hop jump timing"},
    {"Remake: retattle second fights", "A boss fought a second time hides its HP until Goombella tattles it "
                                       "again in that battle.",
     "rules_retattle", "remake boss second fight hp tattle hidden"},
    // the remake's battle pacing
    {"Remake: menu waits for throws", "The battle menu takes no presses while something the audience threw is "
                                      "in the air.",
     "rules_audience_pause", "remake audience throw menu input pause wait"},
    {"Remake: wait for Star Power", "The enemies' turn starts only once the Star Power sparkles reach the gauge.",
     "rules_sp_fill_wait", "remake star power sp gauge fill enemy turn wait"},
    {"Remake: Doopliss", "Running away from Doopliss is easier, and in his chapter 4 fight he holds a Life Shroom.",
     "rules_doopliss", "remake doopliss run away flee escape life shroom revive"},
    {"Remake: Doopliss's surprise attack", "In the Twilight Trail shed, Doopliss's surprise attack takes 1 HP from "
                                           "Mario before the fight.",
     "rules_doopliss_shed", "remake doopliss shed twilight trail surprise attack damage"},
    {"Remake: Glitz Pit conditions", "The Iron Adonises' match always asks for a partner's FP move, Rawk Hawk's has "
                                     "no condition, and the first cake in the locker room gives no Star Power.",
     "rules_glitz_conditions", "remake glitz pit grubba condition iron adonis armored harriers rawk hawk cake"},
    {"Remake: stagehands", "When an enemy's quake would hit a partner who can't move (asleep, stopped, frozen or "
                           "flipped), a stagehand carries them off-stage until Mario's next turn.",
     "rules_tremor_stagehand", "remake stagehand quake earthquake partner asleep flipped carry"},
};

// the players' wishes that change play
constexpr Toggle kWishes[] = {
    {"Balanced badges", "Only one Power Rush and one Mega Rush count, Charge stacks to +5 at most, and Power Lift "
                        "raises Attack and Defense by 3 at most.",
     "balanced_badges", "balanced badges danger mario charge power rush mega rush power lift"},
    {"No stage hazards", "Attacks never knock the scenery down, drop the stage lights or set off the stage's jets.",
     "no_stage_hazards", "stage hazards scenery fall jets fog off"},
    {"No audience throws", "The audience never throws anything, good or bad (Frankly's lessons still do).",
     "no_audience_throws", "audience throw items rocks off"},
    {"Auto-win weak fights", "Touching a field enemy at least 10 levels below Mario wins at once, with the usual "
                             "coins and drops (never bosses, scripted fights, the Pit, Master Quest, challenges or "
                             "online).",
     "auto_win_weak", "auto win weak enemy skip battle level bump attack"},
};

// what the battle screen shows and how it moves (nothing that changes play)
constexpr Toggle kDisplay[] = {
    {"FP cost preview", "The FP gauge blinks while the cursor is on a move that costs FP.", "battle_fp_preview",
     "fp cost preview drain battle menu"},
    {"Grey out Special", "The Special command is greyed when Mario hasn't the Star Power for any of them.",
     "battle_sp_grey", "special star power sp grey menu"},
    {"Spring Jump cue", "A sound as Spring Jump's timing window opens.", "spring_jump_cue",
     "spring jump cue sound timing ultra boots"},
    {"Immunity crossed out", "While picking a target, the statuses it can't take from the move show crossed out over "
                             "it.",
     "battle_immune_cross", "immune immunity status crossed out target tattle"},
    {"Less screen shake", "Screen shakes in and out of battle at a third of their size.", "less_shake",
     "screen shake camera motion accessibility"},
    {"Less flashing", "Screen flashes, lightning and bright bursts at a quarter of their strength.", "less_flashing",
     "flash flashing lightning photosensitive accessibility bright"},
};

struct Register {
  Register() {
    portGameplusDeclare();
    for (const Toggle& t : kRemakeData) {
      portSettingsAddToggle(kTab, t.label, t.note, kPlay, t.key, t.keywords);
    }
    for (const Toggle& t : kWishes) {
      portSettingsAddToggle(kTab, t.label, t.note, kPlay, t.key, t.keywords);
    }
    for (const Toggle& t : kDisplay) {
      portSettingsAddToggle(kTab, t.label, t.note, 0, t.key, t.keywords);
    }
  }
} sRegister;

} // namespace
