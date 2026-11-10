/* A player's file written whole or not at all through SDL's paths (UTF-8 on every system, as SDL_SaveFile
 * and SDL_LoadFile take them): to <path>.tmp, then renamed over <path> (port/include/port/file_write.h; the
 * C++ side's is port/settings/file_util.h portfile::writeAll). */
#include "port/file_write.h"
#include <SDL3/SDL.h>
#include <stdio.h>

int portFileWriteAllSdl(const char* path, const void* data, size_t size) {
    char tmp[1200];
    SDL_IOStream* io;
    int ok;
    const int n = path != NULL ? snprintf(tmp, sizeof(tmp), "%s.tmp", path) : -1;
    if (n <= 0 || (size_t)n >= sizeof(tmp)) {
        return 0;
    }
    io = SDL_IOFromFile(tmp, "wb");
    if (io == NULL) {
        return 0;
    }
    ok = size == 0 || SDL_WriteIO(io, data, size) == size;
    ok = SDL_CloseIO(io) && ok;
    if (ok && SDL_RenamePath(tmp, path)) {
        return 1;
    }
    SDL_RemovePath(tmp);
    return 0;
}
