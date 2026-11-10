// Music shuffle and your own music (port/include/port/music.h): the Audio
// tab's Soundtrack row, and the music folder: where it is, the files found in it,
// what plays now, and the formats this build reads.
#include "port/music.h"
#include "port/offscreen.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int sFound = -1; // files in the music folder at the last look, -1 not looked yet
std::string sOpenError; // the folder couldn't be opened (its path, to go there by hand)

void folderSection(int inGame) {
  const char* folder = portMusicFolder();
  if (sFound < 0) {
    sFound = portMusicRescan();
  }
  ImGui::TextWrapped("%s", folder);
  if (ImGui::Button(portTrLabel("Open the music folder"))) {
    char why[1200];
    sOpenError = portOpenFolder(folder) ? "" : portOpenError(why, sizeof(why), folder, true);
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Look for files again"))) {
    sFound = portMusicRescan();
  }
  if (!sOpenError.empty()) {
    ImGui::TextWrapped("%s", sOpenError.c_str());
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

// The Start menu's form (port/include/port/pause_forms.h): where the folder is, what's in it, what plays
enum { kWhere, kLook, kOpen, kPlaying };

const char* formatsNote() {
  const int formats = portMusicFormats();
  return formats == 7 ? "Reads WAV, OGG, MP3 and FLAC files." : formats == 0 ? "Reads WAV files." : "Reads WAV files and some others.";
}

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  if (sFound < 0) {
    sFound = portMusicRescan();
  }
  PortPauseRow where = portPauseRow(nullptr,
                                    "The music folder. Name a file after the game's track, as tracks.txt in the "
                                    "folder lists them (BGM_STG1_NOK1.wav), and it plays in its place.",
                                    nullptr, kWhere);
  const char* folder = portMusicFolder();
  const size_t len = std::strlen(folder);
  // its last 57 bytes (from a letter's start), the path's UTF-8 in the game's encoding
  const char* tail = len > 60 ? folder + len - 57 : folder;
  while ((static_cast<unsigned char>(*tail) & 0xC0) == 0x80) {
    tail++;
  }
  char shown[sizeof(where.text)];
  portTextForGame(tail, shown, sizeof(shown) - 3);
  std::snprintf(where.text, sizeof(where.text), "%s%s", len > 60 ? "..." : "", shown);
  out[n++] = where;
  PortPauseRow look = portPauseRow("Look for files again", formatsNote(), nullptr, kLook);
  std::snprintf(look.value, sizeof(look.value), portTrGame(sFound == 1 ? "%d file found" : "%d files found"), sFound);
  out[n++] = look;
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  out[n++] = portPauseRow("Open the music folder", "Shows the music folder on the desktop.", nullptr, kOpen);
#endif
  if (const char* now = portMusicPlaying(); now != nullptr && n < max) {
    PortPauseRow r = portPauseRow("Playing", "Your file playing now in place of the game's track.", nullptr, kPlaying);
    portTextForGame(now, r.value, sizeof(r.value)); // (a file's name: UTF-8)
    out[n++] = r;
  }
  return n;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  if (id == kLook) {
    sFound = portMusicRescan();
  } else if (id == kOpen && !portOpenFolder(portMusicFolder())) {
    return "Couldn't open the folder: its path is shown above.";
  }
  return nullptr;
}

const PortPauseForm kForm = {"Audio", "Music folder", formRows, formAct, PORT_PAUSE_FORM_OWN};

// The Soundtrack row: one choice made of the three settings, which stay as they are (settings.txt,
// presets, online's kLocal). Read: the shuffle and your music, each on or off; "Shuffle includes
// your music" only counts with both on, so it isn't read. Written: all three, the last on only
// with both. (An old settings.txt with the shuffle on and your music on but not included, the
// shuffle's default with your music's, reads as the last: your files still play there in place of
// their tracks; it is left as it is until the row is changed.)
// The last two: the Paper Mario 64 soundtrack (port/include/port/pm64_music.h, "pm64_music" 1 or 2),
// played with a linked ROM; your files still play in place of their tracks.
enum { kGames, kYours, kGamesShuffled, kBothShuffled, kPm64, kPm64Mixed };
constexpr const char* kSoundtrack[] = {"The game's", "Your music", "The game's, shuffled",
                                       "Yours and the game's, shuffled", "Paper Mario 64's",
                                       "Paper Mario 64's and the game's, by area"};

int* sShuffle;
int* sCustom;
int* sShuffleCustom;
int* sPm64Music;

int soundtrackGet() {
  if (*sPm64Music != 0) {
    return *sPm64Music == 1 ? kPm64 : kPm64Mixed;
  }
  return (*sShuffle != 0 ? kGamesShuffled : kGames) + (*sCustom != 0 ? 1 : 0);
}

void soundtrackSet(int choice) {
  *sPm64Music = choice == kPm64 ? 1 : choice == kPm64Mixed ? 2 : 0;
  if (*sPm64Music != 0) {
    *sShuffle = 0;
    *sCustom = 1;
    *sShuffleCustom = 0;
    return;
  }
  *sShuffle = choice == kGamesShuffled || choice == kBothShuffled;
  *sCustom = choice == kYours || choice == kBothShuffled;
  *sShuffleCustom = choice == kBothShuffled;
}

const bool kRegistered = [] {
  sShuffle = portSettingsDeclare("music_shuffle", 0, 0, 1);
  sCustom = portSettingsDeclare("custom_music", 1, 0, 1);
  sShuffleCustom = portSettingsDeclare("music_shuffle_custom", 0, 0, 1);
  sPm64Music = portSettingsDeclare("pm64_music", 0, 0, 2);
  portSettingsAddChoiceOf("Audio", "Soundtrack",
                          "Your music: the music folder's files play in place of the tracks they are named after. "
                          "Shuffled: each room's music is another of the game's, field music for field music, "
                          "battle music for battle music; scenes, jingles and fanfares stay as they are. With "
                          "yours, the folder's files join in. Paper Mario 64's: with your Paper Mario ROM linked "
                          "(Enhancements > Paper Mario 64), each place and battle plays a fitting Paper Mario song, "
                          "everywhere or in about half the areas. Only you hear it.",
                          0, "soundtrack", soundtrackGet, soundtrackSet, kSoundtrack, 6,
                          "music shuffle random custom own files replace ogg mp3 flac wav songs bgm tracks paper "
                          "mario 64 n64 pm64 soundtrack");
  portSettingsAddSection("Audio", "Music folder", nullptr, 0, folderSection,
                         "music folder custom files tracks names ogg mp3 flac wav");
  portPauseAddForm(&kForm);
  return true;
}();

} // namespace
