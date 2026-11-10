// The "Remake rules" preset (enhancement_presets.cpp): every setting that makes
// the game play as the Switch remake (2024) does, with the value that does it.
// One line per setting, REMAKE_RULE("settings key", value), the key as in
// settings.txt and its feature's portSettingsDeclare. A feature that adds a
// remake change appends its line at the end (one line each, so merges stay
// easy); the preset turns them all on together, and shows "Remake rules"
// only while every line here holds. Each setting still stands alone on its
// own page.
REMAKE_RULE("remake_balance", 1)    // Star Power, First Attack, Hammer Throw (gameplus_pc.c)
REMAKE_RULE("bigger_bag", 2)        // Bag size: Remake, 15 and 20 with the Strange Sack (gameplus_pc.c)
REMAKE_RULE("skip_scenes", 1)       // Skip repeated scenes (gameplus_pc.c, qol_pc.c)
REMAKE_RULE("full_bag_use", 1)      // A healing item used at once when the bag is full (qol_pc.c)
REMAKE_RULE("coin_cap", 1)          // Coins up to 9,999, Money Money up to 99 (qol_pc.c)
REMAKE_RULE("levelup_heal_party", 1) // A level-up heals every partner (qol_pc.c)
REMAKE_RULE("partner_hint", 1)      // The partner says where to go next (qol_pc.c)
REMAKE_RULE("talk_prompts", 1)      // "!" over things to talk to or check (qol_pc.c)
REMAKE_RULE("mail_no_stop", 1)      // Mail notices that don't stop Mario (qol_pc.c)
REMAKE_RULE("shop_plus", 1)         // Shop improvements (qol_pc.c)
REMAKE_RULE("bowser_retry", 1)      // Retry in the Bowser intermission's pause (qol_pc.c)
REMAKE_RULE("jukebox_unlock", 1)    // Jukebox: tracks unlocked by Shine Sprites (qol_pc.c)
REMAKE_RULE("coin_hints", 1)        // The Journal sells hints for coins (qol_pc.c)
REMAKE_RULE("list_lr_scroll", 1)    // Left and Right page through item and badge lists (qol_pc.c)
REMAKE_RULE("save_file_details", 1) // Partner, date, colour and medal on the file screen (qol_pc.c)
// the field rules (field_*; keys missing from a build are skipped)
REMAKE_RULE("remake_notice_range", 1)
REMAKE_RULE("remake_projectile_hurt", 1)
REMAKE_RULE("remake_pipe_a", 1)
REMAKE_RULE("remake_field_hammer", 1)
REMAKE_RULE("remake_koops_aim", 1)
REMAKE_RULE("remake_plane_steady", 1)
REMAKE_RULE("remake_yoshi", 1)
REMAKE_RULE("remake_teeter", 1)
REMAKE_RULE("remake_guard_stance", 1)
REMAKE_RULE("remake_warp_pipes", 1)  // sewer warp pipe to every hub with its Crystal Star (remake_maps.cpp)
REMAKE_RULE("remake_shortcut_pipes", 1)  // Hooktail, Steeple basement, Palace shortcut pipes (remake_maps.cpp)
REMAKE_RULE("remake_express_skip", 1)  // Excess Express rides past Riverside after chapter 6 (remake_maps.cpp)
REMAKE_RULE("remake_chapter_shortcuts", 1)  // chapter 4 and 8 shortcuts (remake_maps.cpp)
REMAKE_RULE("remake_keelhaul", 1)  // Keelhaul jump stands, Grotto barrel, Fuzzies stay (remake_maps.cpp)
REMAKE_RULE("remake_steeple_stairs", 1)  // jump down the Creepy Steeple stairs (remake_maps.cpp)
REMAKE_RULE("remake_twilight", 1)  // shorter General White hunt (remake_maps.cpp)
REMAKE_RULE("remake_blocks", 1)  // the remake's new Save and Heart Blocks (remake_maps.cpp)
REMAKE_RULE("remake_ambushes", 1)  // the remake's new ambush battles (remake_maps.cpp)
REMAKE_RULE("remake_merchants", 1)  // Charlieton in Boggly Woods, the Grotto Toad (remake_maps.cpp)
// the battle and badge rules (rules_pc.c; remake_balance is above)
REMAKE_RULE("rules_sleepy_sheep", 1)   // Sleepy Sheep 3 turns
REMAKE_RULE("rules_power_lift", 1)     // Power Lift: 4 arrows a point
REMAKE_RULE("rules_sweet_treat", 1)    // Sweet Treat: 2 Poison Mushrooms
REMAKE_RULE("rules_mystery_box", 1)    // Mystery's pool
REMAKE_RULE("rules_lottery", 1)        // Lucky Day and Power Plus swapped
REMAKE_RULE("rules_pit_shop", 1)       // Pit merchant: Ultra Shroom, Jammin' Jelly
REMAKE_RULE("rules_trouble_coins", 1)  // Troubles: 20 coins -> 30
REMAKE_RULE("rules_boss_coins", 1)     // bosses pay 50/100/200 (Bonetail 999)
REMAKE_RULE("rules_flee_coins", 1)     // no coins lost fleeing
REMAKE_RULE("rules_hazard_pierce", 1)  // stage hazards ignore defense
REMAKE_RULE("rules_deep_sleep", 1)     // sleepers harder to wake
REMAKE_RULE("rules_bomb_no_sp", 1)     // Bob-omb blasts give no Star Points
REMAKE_RULE("rules_buzzy_blast", 1)    // Buzzies immune to explosions
REMAKE_RULE("rules_flower_fuzzy", 1)   // Flower Fuzzies go for Mario
REMAKE_RULE("rules_bandit_returns", 1) // a Bandit's loot comes back
REMAKE_RULE("rules_spider_strike", 1)  // easier First Strike on Pider and Arantula
REMAKE_RULE("rules_retattle", 1)       // second boss fights hide HP until re-Tattled
REMAKE_RULE("battle_fp_preview", 1)    // FP cost shown before an FP move
REMAKE_RULE("battle_sp_grey", 1)       // Special greyed when short of Star Power
REMAKE_RULE("spring_jump_cue", 1)      // Spring Jump's audio cue
REMAKE_RULE("rules_swoopula_swoop", 1) // Swoopulas also Swoop
REMAKE_RULE("rules_chomp_still", 1)    // Chain Chomps hold still for a jump
REMAKE_RULE("battle_immune_cross", 1)  // immunity crossed out
REMAKE_RULE("rules_audience_pause", 1) // the menu waits while the audience throws
REMAKE_RULE("rules_sp_fill_wait", 1)   // the enemy turn waits for the Star Power fill
REMAKE_RULE("rules_doopliss", 1)        // Doopliss: easier Run Away, a Life Shroom in chapter 4
REMAKE_RULE("rules_doopliss_shed", 1)   // Doopliss's surprise attack in the shed takes 1 HP
REMAKE_RULE("rules_glitz_conditions", 1) // Glitz Pit: Iron Adonises want a partner move, Rawk Hawk none, no SP cake
REMAKE_RULE("rules_tremor_stagehand", 1) // a stagehand carries a helpless partner away from a quake
// (no line needed: Spinies already use items while curled, and flying shelled
// enemies already flip when a second hit grounds them, as in the remake)
