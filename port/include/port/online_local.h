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
    "battery_saver", "keep_game_speed", "partner_wheel", "system_bars",
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
