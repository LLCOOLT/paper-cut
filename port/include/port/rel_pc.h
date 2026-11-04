#ifndef PORT_REL_PC_H
#define PORT_REL_PC_H

#include <dolphin/types.h>

/* RELs linked into the executable (TTYD_RELS_STATIC): one row per area, from
 * the generated rel_static_table.c. Each REL's data and bss lie between the
 * markers (grouped sections), so linking restores a fresh copy of them. */
typedef struct PortRelStatic {
    const char* name;
    void (*prolog)(void);
    void (*epilog)(void);
    char* dataStart;
    char* dataEnd;
    char* bssStart;
    char* bssEnd;
} PortRelStatic;
extern const PortRelStatic portRelStaticTable[];
extern const int portRelStaticCount;

/* Module mode: locate the rel/ directory next to the executable. Static mode:
 * snapshot every REL's initial data. Call once at startup. */
void portRelInit(const char* exePath);
/* Stand-in for reading <area>.rel, OSLink and calling _prolog. */
BOOL portRelLink(const char* areaName);
/* Stand-in for calling _epilog and OSUnlink. */
void portRelUnlink(void);
/* The linked REL's _epilog now, still linked (its code and data stay until the
 * next map change unlinks it, which then doesn't run it again): a save loaded
 * in the field (portContinueGame) puts another game in gp, and the area's
 * _epilog must run in the game it belongs to, not write into that one. */
void portRelEpilogNow(void);

#endif
