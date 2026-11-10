/* A player's file written whole or not at all (port/src/common/file_write_pc.c). A file rewritten in place
 * (truncated, then written line by line) is left empty or cut short when the process dies half way: a phone's
 * OS killing the app in the background, a flat battery, a crash on another thread. These write to
 * <path>.tmp and rename it over <path> once it is all there, so the old file stays until the new one is
 * whole, synced to the disk first (the C side of port/settings/file_util.h portfile::writeAll). Paths are
 * UTF-8 on every system (port/file_pc.h's calls):
 *
 *     FILE* f = portFileWriteOpen(path, "w");
 *     if (f != NULL) {
 *         fprintf(f, ...);
 *         portFileWriteClose(f, path);
 *     }
 */
#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* <path>.tmp opened for writing (mode "w" or "wb"); NULL when it can't be */
FILE* portFileWriteOpen(const char* path, const char* mode);

/* f (from portFileWriteOpen(path, ...)) closed and put in place over path: 1; 0 when a write, the close or
   the rename failed (the old file then stays as it was, and the .tmp is removed) */
int portFileWriteClose(FILE* f, const char* path);

/* the same, not synced to the disk: for small records written often on the game thread (the stats, every
   ten seconds of play), where a physical flush each time would hitch the frame on a slow disk or a phone's
   flash. The rename still keeps the file whole against a crash; only a power cut can lose the last write. */
int portFileWriteCloseQuick(FILE* f, const char* path);

/* the whole of data written to path the same way: 1 when it is there */
int portFileWriteAll(const char* path, const void* data, size_t size);

/* the same through SDL's file calls, for a path SDL_LoadFile reads back (UTF-8 on Windows too): the
   Aurora backend's (port/src/aurora/file_write_aurora.c) */
int portFileWriteAllSdl(const char* path, const void* data, size_t size);

#ifdef __cplusplus
}
#endif
