#ifndef PORT_PATH_LINK_H
#define PORT_PATH_LINK_H

/* A path that is a link (a symlink; on Windows also a junction or any other
 * reparse point): the mods' folder walks (mods.cpp, lua_host.cpp,
 * mods_install.cpp) skip these, so a link loop can't recurse until the stack
 * runs out, a link to a big folder isn't copied whole, and a removal never
 * deletes through one. port/src/common/path_link_pc.c. UTF-8 path; 1 if a link. */

#ifdef __cplusplus
extern "C" {
#endif

int portPathIsLink(const char* path);

#ifdef __cplusplus
}
#endif

#endif
