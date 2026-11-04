// Music shuffle and your own music (port/include/port/music.h): the Audio
// tab's toggles, and the music folder: where it is, the files found in it,
// what plays now, and the formats this build reads.
#include "port/music.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <string>

namespace {

int sFound = -1; // files in the music folder at the last look, -1 not looked yet

void folderSection(int inGame) {
  const char* folder = portMusicFolder();
  if (sFound < 0) {
    sFound = portMusicRescan();
  }
  ImGui::TextWrapped("%s", folder);
  if (ImGui::Button(portTrLabel("Open the music folder"))) {
    SDL_OpenURL((std::string("file:///") + folder).c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Look for files again"))) {
    sFound = portMusicRescan();
  }
  ImGui::Text(portTr(sFound == 1 ? "%d file found" : "%d files found"), sFound);
  if (inGame) {
    const char* now = portMusicPlaying();
    if (now != nullptr) {
      ImGui::Text(portTr("Playing: %s"), now);
    }
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  const int formats = portMusicFormats();
  ImGui::TextWrapped("%s", portTr(formats == 7 ? "Reads WAV, OGG, MP3 and FLAC files."
                                               : formats == 0 ? "Reads WAV files." : "Reads WAV files and some others."));
  ImGui::TextWrapped("%s", portTr("Name a file after the game's track, as tracks.txt in the folder lists them: "
                                  "BGM_STG1_NOK1.wav, for example. A file loops at its LOOPSTART and "
                                  "LOOPLENGTH tags, or as a whole."));
  ImGui::PopStyleColor();
}

const bool kRegistered = [] {
  portSettingsDeclare("music_shuffle", 0, 0, 1);
  portSettingsDeclare("custom_music", 1, 0, 1);
  portSettingsDeclare("music_shuffle_custom", 0, 0, 1);
  portSettingsAddToggle("Audio", "Music shuffle",
                        "Each room's music is another of the game's: field music for field music, battle music "
                        "for battle music. Scenes, jingles and fanfares stay as they are. Only you hear it.",
                        0, "music_shuffle", "music shuffle random bgm songs tracks soundtrack");
  portSettingsAddToggle("Audio", "Your music",
                        "Your files in the music folder play in place of the game's tracks they are named after.",
                        0, "custom_music", "custom music replace own files ogg mp3 flac wav songs bgm");
  portSettingsAddToggle("Audio", "Shuffle includes your music",
                        "Music shuffle also picks the music folder's files.", 0, "music_shuffle_custom",
                        "music shuffle custom own files random");
  portSettingsAddSection("Audio", "Music folder", nullptr, 0, folderSection,
                         "music folder custom files tracks names ogg mp3 flac wav");
  return true;
}();

} // namespace
