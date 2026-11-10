// "Save as...": a file the player names and places (Saves > Export, a settings preset, a crash report's zip).
// Export-after-write: the bytes are made first, then the system asks where they go. A computer's save dialog
// gives a path, written then; iOS's export picker needs the file before it asks (SDL has no dialog there), so
// it is written in tmp/ first and a copy goes where the player picks (Files, iCloud Drive...). Every "save as"
// goes through here, never portShowSaveFileDialog or SDL's (testpass lint: "save and folder pickers on iOS").
#pragma once

#include "port/offscreen.h"
#include "file_util.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace portfile {

// done(userdata, files, filter) answers as SDL's save dialog does, after the write: files[0] the path the
// bytes are in now; an empty list: cancelled, nothing written; files null: failed, SDL_GetError says why (the
// dialog's or the write's). It may run on another thread (the dialog's own on Windows). Offscreen: nothing
// shown and done never called, as a picker still open.
inline void exportFile(SDL_DialogFileCallback done, void* userdata, const SDL_DialogFileFilter* filters, int nfilters,
                       const char* name, std::vector<uint8_t> bytes) {
  if (portOffscreen()) {
    std::printf("[offscreen] save picker: not shown\n");
    return;
  }
#if defined(__APPLE__) && TARGET_OS_IPHONE
  (void)filters;
  (void)nfilters;
  portIosExportFile(done, userdata, name, bytes.data(), bytes.size());
#else
  struct Job {
    std::vector<uint8_t> bytes;
    SDL_DialogFileCallback done;
    void* userdata;
  };
  Job* job = new Job{std::move(bytes), done, userdata};
  portShowSaveFileDialog(
      [](void* p, const char* const* files, int filter) {
        Job* written = static_cast<Job*>(p);
        const char* const* answer = files;
        if (files != nullptr && files[0] != nullptr &&
            !writeAll(files[0], written->bytes.data(), written->bytes.size())) {
          answer = nullptr; // (SDL_GetError: writeAll's)
        }
        written->done(written->userdata, answer, filter);
        delete written;
      },
      job, SDL_GetKeyboardFocus(), filters, nfilters, name);
#endif
}

} // namespace portfile
