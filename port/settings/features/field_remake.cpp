// The remake's field changes (port/include/port/field_remake.h): the settings
// page's toggles, all off by default and marked "changes play".
#include "port/field_remake.h"
#include "port/settings.h"

namespace {

constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;
constexpr const char* kTab = "Enhancements";

struct Register {
  Register() {
    portFieldRemakeDeclare();
    portSettingsAddToggle(kTab, "Shorter enemy notice range",
                          "Enemies in the field notice Mario from about two thirds as far, as in the remake.", kPlay,
                          "remake_notice_range", "remake enemy notice sight range see chase field");
    portSettingsAddToggle(kTab, "Field projectiles hurt",
                          "A bone, hammer, Bullet Bill or beam an enemy sends at Mario in the field takes 1 HP "
                          "instead of starting a battle, as in the remake. At 1 HP the battle starts as before.",
                          kPlay, "remake_projectile_hurt", "remake projectile bone hammer bullet field damage battle");
    portSettingsAddToggle(kTab, "Pipes with A",
                          "Standing on a pipe shows the \"!\" over Mario and {A} goes down it, instead of holding "
                          "down; he comes out of a pipe facing the camera, as in the remake.",
                          kPlay, "remake_pipe_a", "remake pipe warp a button prompt enter down");
    portSettingsAddToggle(kTab, "Remake field hammer",
                          "Mario's hammer in the field also hits behind him as it swings over, shakes bushes, makes "
                          "people and the partner flinch, shows a hidden Star Piece panel it lands near, and knocks "
                          "a coin out of Rogueport's cockroaches, as in the remake.",
                          kPlay, "remake_field_hammer", "remake hammer field bush npc partner star piece panel cockroach coin");
    portSettingsAddToggle(kTab, "Koops' aim",
                          "While Koops' shell goes out or is held, a ring marks where it stops and stretches back "
                          "to him, as in the remake.",
                          kPlay, "remake_koops_aim", "remake koops shell aim ring arrow target partner");
    portSettingsAddToggle(kTab, "Steadier Paper Plane",
                          "The Paper Plane keeps its height until it is clear of the panel it took off from, as in "
                          "the remake.",
                          kPlay, "remake_plane_steady", "remake paper plane glide height panel");
    portSettingsAddToggle(kTab, "Remake Yoshi rides",
                          "Riding Yoshi: he keeps his speed off an edge, his flutter lasts longer, {A} talks to "
                          "people and opens doors without getting off, and Mario is back on him after a jump stand, "
                          "as in the remake.",
                          kPlay, "remake_yoshi", "remake yoshi kid ride flutter edge door talk jump stand spring");
    portSettingsAddToggle(kTab, "Ledge teetering",
                          "Standing at the edge of a drop, Mario flails his arms to keep his balance, as in the "
                          "remake. Luigi, Peach and Bowser do it their own way.",
                          kPlay, "remake_teeter", "remake ledge edge teeter balance flail animation");
    portSettingsAddToggle(kTab, "Guard stances",
                          "In battle, pressing {A} or {B} as an enemy's attack comes shows Mario bracing for a Guard "
                          "or thrusting out for a Superguard, as in the remake.",
                          kPlay, "remake_guard_stance", "remake guard superguard stance pose battle defend animation");
  }
} gRegister;

} // namespace
