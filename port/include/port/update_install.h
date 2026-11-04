/* Updates installed from the game (port/settings/features/update_install.cpp,
 * docs/UPDATES.md): a release's zip for this platform, checked against the
 * release's SHA-256 sums, unpacked beside the install (<install>/update/new,
 * on macOS beside the app), then moved into place at the next start, before
 * anything else, the old files kept aside until the start after. The user
 * folder (saves, settings, mods) is elsewhere and never touched. */
#ifndef PORT_UPDATE_INSTALL_H
#define PORT_UPDATE_INSTALL_H

#ifdef __cplusplus
extern "C" {
#endif

/* At the very start of main: a staged update moved into place, then this
 * program started again from the new files (the same arguments) and this one
 * ended. Nothing when there's none, and nothing on phones. */
void portUpdateApplyStaged(int argc, char** argv);

#ifdef __cplusplus
}

#include "port/http.h"
#include <string>
#include <vector>

namespace port_update {

enum class Mode {
  Install, // the zip: downloaded and installed from here
  Flatpak, // Flathub updates it
  AppImage, // AppImageUpdate does
  Elsewhere, // not a release's install (a build folder, a phone): the release page
};
Mode mode(std::string* why = nullptr);

/* This platform's release zip: "papercut-<version>-windows-x64.zip". */
std::string assetName(const std::string& version);

/* SHA-256 of a file, lower-case hex; empty when it can't be read. */
std::string sha256File(const std::string& path);

/* Where the update is put together: <install>/update (macOS: beside the app). */
std::string stageDir();

/* zip (a downloaded release) unpacked into <stage>/new, its one top folder
 * dropped, then marked ready for the next start (version). false and why. */
bool stage(const std::string& zip, const std::string& version, std::string& why);

/* The version staged for the next start, or empty. */
std::string staged();

} // namespace port_update
#endif

#endif
