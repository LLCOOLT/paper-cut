// The settings that belong to this device (key prefixes): its screen, sound, controls, files and
// accounts. Never put in a settings preset or a replay, never changed by one (presets.cpp, replay.cpp).
// Online co-op's list of the guest's own settings is online.cpp's kLocal, a list of its own: it keeps
// what only that player sees or hears, which is more than this.
#pragma once

#include <cstring>
#include <string>

namespace portdevice {

constexpr const char* kKeys[] = {
    "framerate", "vsync", "fullscreen", "resolution", "fps", "overlay_", "msaa", "tex_filter", "texture_upscale",
    "supersample", "post_filter", "vrr", "hdr", "aniso", "lod_bias", "no_pop_in", "aspect_", "side_bars", "volume",
    "mute", "music_", "sfx_", "ambience_", "background_input", "pause_unfocused", "cloud_sync", "sync_",
    "phone_rumble", "rumble", "touch_", "auto_run", "stick_fixed", "tap_a", "orientation", "safe_area", "ff_",
    "launcher_", "dump_textures", "key", "pad", "controller", "deadzone", "trigger", "second_player_port",
    "menu_language", "speedrun_timer", "timer_", "split_category", "livesplit", "memory_watch", "achievements",
    "button_prompts", "perf_overlay", "update_check", "window_", "quick_resume", "save_backups", "second_screen",
    "companion", "custom_music", "settings_hint", "online", "crowd_", "jukebox", "game_sound_mode", "speakers",
    "discord_status", "rewind_key", "rewind_pad", "bind_", "mouse_camera", "dualsense", "update_auto",
    "save_library", "whats_new", "battery_saver",
};

inline bool owns(const std::string& key) {
  for (const char* p : kKeys) {
    if (key.compare(0, std::strlen(p), p) == 0) {
      return true;
    }
  }
  return false;
}

} // namespace portdevice
