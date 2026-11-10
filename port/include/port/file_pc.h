#ifndef PORT_FILE_PC_H
#define PORT_FILE_PC_H

/* The player's files by UTF-8 path (port/src/common/file_pc.c). Every user folder path is UTF-8
 * (portSettingsUserPath comes from SDL_GetPrefPath), and on Windows the C library's fopen, remove and
 * rename read a narrow path in the ANSI code page: an account named "José" or "Łukasz", or a CJK name,
 * and nothing was ever written. These take UTF-8 everywhere (the wide calls on Windows, the plain ones
 * elsewhere): use them for every file the port opens, removes or renames, never the C library's own.
 * Records written whole or not at all go through port/file_write.h, which uses these. */

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE* portFopen(const char* path, const char* mode);
int portRemove(const char* path);                 /* 0 removed (as remove) */
int portRename(const char* from, const char* to); /* 0 renamed (as rename), over an existing to too */
/* the same without waiting for the disk (Windows: no MOVEFILE_WRITE_THROUGH), for files written often */
int portRenameQuick(const char* from, const char* to);
int portMkdir(const char* path);                  /* 0 made or already there */
int portPathKind(const char* path);               /* 0 missing, 1 a file, 2 a folder */
long long portFileSize(const char* path);         /* -1 when it can't be read */
/* Windows: the variables that carry a path ($TTYD_USER_DIR, $TTYD_CACHE_DIR, $TTYD_LOAD_SAVE, $TTYD_DISC,
 * $TTYD_SHOT_DIR, $TTYD_DATA_DIR, $TTYD_ISO) read as UTF-8 by getenv from here on; the OS environment (what a
 * restarted or updated Paper Cut inherits) keeps the right wide value */
void portUtf8EnvInit(void);
/* name=utf8 for getenv (UTF-8 on Windows too) and in the OS environment child processes inherit (the wide
 * value on Windows: never the narrow _putenv_s alone, which stores UTF-8 bytes read as ANSI there). Use it
 * for every variable that carries a path. SDL's own copy (SDL_getenv) is the caller's to update. */
void portSetEnvUtf8(const char* name, const char* utf8);


#ifdef __cplusplus
}
#endif

#endif
