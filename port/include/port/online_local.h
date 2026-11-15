/* Online play's settings that stay each player's own (by their keys' prefixes):
 * a shared battle plays on the host's settings except these, which only change what
 * that player sees or hears (statesync_battle_pc.c). */
#ifndef PORT_ONLINE_LOCAL_H
#define PORT_ONLINE_LOCAL_H

#include <string.h>

static const char* const kPortOnlineLocalKeys[] = {
    "framerate", "vsync", "fullscreen", "widescreen", "resolution", "fps", "overlay_", "no_pop_in", "tex_filter",
    "msaa", "side_bars", "wide_hud", "volume", "mute", "music_", "sfx_", "ambience_", "background_input",
    "cloud_sync", "rumble", "phone_rumble", "touch_", "auto_run", "stick_fixed", "tap_a", "orientation",
    "safe_area", "ff_", "launcher_skip", "dump_textures", "key2_", "menu_language", "speedrun_timer", "timer_",
    "split_category", "livesplit", "memory_watch", "achievements", "button_prompts", "aspect_", "hud_scale",
    "text_scale", "aniso", "lod_bias", "texture_upscale", "supersample", "post_filter", "vrr", "perf_overlay",
    "hdr", "collectible_tracker", "sell_helper", "menu_mouse", "quick_resume", "save_backups", "sharp_text",
    "update_check", "window_", "jukebox", "cosmetic", "mirror", "free_cam", "input_display", "second_screen",
    "companion", "ac_cue", "controller_haptics", "crowd_", "custom_music", "settings_hint", "game_sound_mode", "speakers",
    "controller_light", "discord_status", "tts", "captions", "colorblind", "rewind_key", "rewind_pad",
    "paper_shadows", "paper_physics", "paper_edges", "launcher_popup",
    "battery_saver", "keep_game_speed", "partner_wheel", "system_bars", "safe_graphics", "gfx_backend",
    /* the controls (each player's keys and buttons: the pads' state crosses, not the mapping),
       the free camera's mouse, the controller's speaker, the HUD's look, this computer's
       updates and save library, the what's-new page, the bingo tracker over the picture */
    "bind_", "pad2_", "mouse_camera", "dualsense", "hud_theme", "update_auto", "save_library", "whats_new",
    "bingo_tracker", "ghost_see_through",
    /* the first-time tips, the launcher's what's-new card and Paper Mario 64 card (shown once, each device's own) */
    "tip_", "launcher_news", "launcher_pm64",
    /* the fun toggles that only change the drawing or the sound (moon gravity changes play: the host's) */
    "fun_big_heads", "fun_paper_thin", "fun_spin", "fun_luigi_voice", "fun_cool",
    /* a player's comfort: flashes and screen shakes (only their size: the battle plays the same) */
    "less_flashing", "less_shake",
    /* 0.5.0's own look and sound, and this device's: the picture's bloom and ray-traced light, the sound's
       rate, the Paper Mario 64 soundtrack, the talk blips, the texture pack's memory, the touchpad, the
       handheld's one-time preset, the save before the device sleeps */
    "bloom", "rt_lighting", "audio_48k", "pm64_music", "pm64_sfx", "pm64_sfx_general", "pg_talk_blips", "texture_pack_memory", "touchpad_scroll",
    "handheld_preset", "save_before_sleep",
    /* stereoscopic 3D: the picture for two eyes (this player's screen or glasses) */
    "stereo_",
    /* 0.5.1: what a shared battle shows each player, never what it does (they took the host's in shared battles):
       the FP preview, Star Power greyed, the spring jump's cue, the immune cross, the page turn, the battle
       themes, the Japanese frog texts, the talk prompts, the lists' L/R scroll, the mail that doesn't stop the
       game, and the restorations that are only a look or a text (the X-Naut crowd changes a room: the host's) */
    "battle_fp_preview", "battle_sp_grey", "spring_jump_cue", "battle_immune_cross", "page_turn", "pg_battle_theme_",
    "pg_jp_hooktail_frogs", "talk_prompts", "list_lr_scroll", "mail_no_stop", "rs_chalk_outline", "rs_bunny_boo",
    "rs_jp_flashes",
    /* online play's direct connection (port/online_ice.h): each player's own choice, never the host's */
    "online_direct",
};

/* The session's field rules: the host's in every game for the whole session, not only in shared
 * battles (statesync_battle_pc.c), because they change what the merged progress means: how much the
 * bag holds and coins go up to, what a battle fought alone pays and costs (EXP, coins, the enemies'
 * strength), and which items, badges and recipes can be had. Each guest has its own back when the
 * session is over (settings.txt keeps them meanwhile). */
static const char* const kPortOnlineSessionKeys[] = {
    "bigger_bag", "coin_cap", "full_bag_use", "coin_multiplier", "exp_multiplier", "level_sync",
    "levelup_heal_party", "heal_all_partners", "partner_revive", "difficulty", "remake_balance",
    "balanced_badges", "auto_win_weak", "enemy_variants", "master_quest", "chaos_mode", "pc_badges",
    "pc_recipes", "pm64_badges", "pm64_recipes", "pg_buy_tokens", "pg_crane_prizes", "pg_jp_recipes", "shop_plus",
    "coin_hints", "rules_boss_coins", "rules_flee_coins", "rules_trouble_coins", "rules_lottery",
    "rules_pit_shop", "rules_bandit_returns",
    /* how high Luigi jumps (when he is the pick): a ledge he reaches is the host's rule too */
    "luigi_high_jump",
    /* how far the Paper Mario 64 partners' field abilities reach (pm64_field_pc.c): the host's rule too */
    "pm64_field",
    /* Paper Mario 64's Spin Dash in the field (pm64_spin_pc.c): a move every game of the session has, or none */
    "pm64_spin",
    /* what the field holds and pays, the same in every game (0.5.1: each was the host's only in shared battles,
       so two games of one session could differ in the field): Console-accurate's lock-out (it turns level_sync,
       chaos_mode and the fun toggles off), the Director's Cut's enemies, stock and pipe, every remake_ room and
       field change (pipes, blocks, ambushes, merchants, enemies' notice, field projectiles, the field hammer:
       "remake_" takes remake_balance in too), every rules_ rule, the daily shop specials, the Pit's pay, the
       battle grades' coins, the remix's enemies and badge draft, moon gravity's jumps (as luigi_high_jump), and
       Brothers mode's Luigi in the field */
    "console_accurate", "directors_cut", "remake_", "rules_", "daily_specials", "pit_plus", "battle_grades", "remix_",
    "fun_moon_gravity", "brothers_mode", "rs_xnaut_crowd",
    /* the scenes skipped and the fixed random numbers: every guest is pulled into the host's scenes and shared
       battles, so these match the host's (the daily specials' day is the host's too: its roster, statesync_pc.c) */
    "skip_scenes", "rng_fixed", "rng_seed",
};

/* a key that is the session's field rule (prefixes, as above: "difficulty" takes its enemy HP and
 * attack too, "master_quest" its set) */
static inline int portOnlineSessionKey(const char* key) {
    size_t i;
    for (i = 0; i < sizeof(kPortOnlineSessionKeys) / sizeof(kPortOnlineSessionKeys[0]); i++) {
        if (strncmp(key, kPortOnlineSessionKeys[i], strlen(kPortOnlineSessionKeys[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

/* a key only this player sees or hears */
static inline int portOnlineLocalKey(const char* key) {
    size_t i;
    for (i = 0; i < sizeof(kPortOnlineLocalKeys) / sizeof(kPortOnlineLocalKeys[0]); i++) {
        if (strncmp(key, kPortOnlineLocalKeys[i], strlen(kPortOnlineLocalKeys[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

#endif
