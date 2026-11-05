/* Challenge rules (port/include/port/gameplus.h; the Challenges tab's
 * presets and switches: port/settings/features/challenges.cpp). Each is a
 * declared setting, off by default, checked by the battle code in small
 * TARGET_PC blocks:
 *   challenge_ohko            any enemy hit that does damage knocks Mario or
 *                             the partner out (battle_damage.c)
 *   challenge_guard           1: no Guard (a Superguard still works), 2:
 *                             neither (battle_ac.c)
 *   challenge_commands        1: attack commands' windows as with 2
 *                             Unsimplifiers, 2: as with 3 (battle_ac.c)
 *   challenge_no_battle_items the Items command is greyed out in battle
 *                             (battle_seq_command.c) */
#include "port/gameplus.h"
#include "port/lua_mods.h"
#include "port/settings.h"

static const struct {
    const char* key;
    int def, min, max;
} kChallenges[] = {
    {"challenge_ohko", 0, 0, 1},
    {"challenge_guard", 0, 0, 2},
    {"challenge_commands", 0, 0, 2},
    {"challenge_no_battle_items", 0, 0, 1},
};

void portChallengeDeclare(void) {
    static int sDone;
    size_t i;
    if (sDone) {
        return;
    }
    sDone = 1;
    for (i = 0; i < sizeof(kChallenges) / sizeof(kChallenges[0]); i++) {
        portSettingsDeclare(kChallenges[i].key, kChallenges[i].def, kChallenges[i].min, kChallenges[i].max);
    }
}

int portChallenge(const char* key) {
    int* v;
    int rule;
    portChallengeDeclare();
    if (portLuaRule(key, &rule)) {
        return rule; /* a Lua mod's rules.set (port/include/port/lua_mods.h) */
    }
    v = portSettingsFind(key);
    return v != 0 ? *v : 0;
}

int portChallengeDamage(int damage, int targetHp) {
    if (damage > 0 && portChallenge("challenge_ohko") && targetHp > damage) {
        return targetHp;
    }
    return damage;
}

int portChallengeGuard(int result) {
    const int g = portChallenge("challenge_guard");
    /* 4: a Guard, 5: a Superguard; 2: not guarded (battle_ac.c) */
    if ((g >= 1 && result == 4) || (g >= 2 && result == 5)) {
        result = 2;
    }
    return portLuaGuardResult(result); /* Lua mods' "guard_result" */
}

int portChallengeCommandLevel(int level) {
    const int c = portChallenge("challenge_commands");
    level += c == 0 ? 0 : c == 1 ? 2 : 3;
    return portLuaCommandLevel(level > 6 ? 6 : level); /* Lua mods' "command_level" */
}
