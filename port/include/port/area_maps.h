#ifndef PORT_AREA_MAPS_H
#define PORT_AREA_MAPS_H

/* Area maps (port/src/common/area_maps_pc.c): a drawn map of each area from
 * the game's own room data, the rooms and the exits between them
 * (port/src/common/area_maps_gen.c, tools/port/gen_area_maps.py, from the
 * rels' BeroEntry tables), with the rooms this save file has been in, the
 * exits it hasn't taken yet and the Star Pieces and Shine Sprites still in
 * each room (the tracker's table, port/include/port/gameplus.h).
 *
 * What a file has seen is kept beside the memory cards, not in the save:
 *   <user>/AreaMaps/<game id>/file<N>.txt   "name=<player>", "v <room>", "e <exit>"
 * Drawn on the settings page ("Area map" tab, port/settings/features/area_maps.cpp)
 * and on the second screen's Map page (port/include/port/companion.h).
 * Draw-only: nothing here changes the game or its random numbers. Plain C. */

#ifdef __cplusplus
extern "C" {
#endif

/* The generated tables. A room is a map's first six letters (rsh_03_a and
 * rsh_03_b are one room); x east and y south on the area's grid. */
typedef struct PortAreaRoom {
    const char* map;
    const char* area;
    int x, y;
} PortAreaRoom;

/* dir: the way the hit name says Mario walks out (1 north, then clockwise to
 * 8 north-west; 0 none: a pipe, a door). toRoom: the room's index, or -1
 * when it's in another area (or not a room: a cutscene). */
typedef struct PortAreaExit {
    int room;
    const char* hit;
    const char* to;
    int toRoom;
    int dir;
} PortAreaExit;

extern const PortAreaRoom portAreaRooms[];
extern const int portAreaRoomCount;
extern const PortAreaExit portAreaExits[];
extern const int portAreaExitCount;

/* One area's map as drawn (fixed sizes: the second screen's snapshot holds one). */
#define PORT_AREA_VIEW_ROOMS 48
#define PORT_AREA_VIEW_LINKS 96

typedef struct PortAreaViewRoom {
    char map[8];
    int x, y;
    int visited;     /* this file has been in it */
    int here;        /* Mario is in it now */
    int pieces;      /* Star Pieces still to find in it */
    int sprites;     /* Shine Sprites still to find in it */
    int waysUntaken; /* exits from it not taken yet (this file) */
} PortAreaViewRoom;

typedef struct PortAreaViewLink {
    int a, b;      /* rooms (view indices); b -1: out of the area, towards dir */
    int dir;       /* for b -1: PortAreaExit.dir (0: drawn to the east) */
    int taken;     /* walked through, either way */
    char to[8];    /* for b -1: the room it leads to */
} PortAreaViewLink;

typedef struct PortAreaView {
    char area[4];  /* "gor", "" for none */
    int width, height; /* the grid's size */
    int roomCount, linkCount;
    int visited;   /* rooms visited of roomCount */
    int pieces, sprites; /* still to find in the whole area */
    PortAreaViewRoom rooms[PORT_AREA_VIEW_ROOMS];
    PortAreaViewLink links[PORT_AREA_VIEW_LINKS];
} PortAreaView;

/* Once per game frame (practice_pc.c): the room Mario is in, kept. */
void portAreaMapsFrame(void);
/* A save file was loaded (practice_pc.c, as the achievements'). */
void portAreaMapsFileLoaded(void);
/* The area's map now ("" or NULL: the area Mario is in). Any thread. 0: no such area. */
int portAreaMapBuild(const char* area, PortAreaView* out);
/* The areas with rooms, in story order, and the one Mario is in ("" none). */
int portAreaCount(void);
const char* portAreaAt(int i);
const char* portAreaHere(void);
/* The area's name in the game's own text (the Journal's, UTF-8), or NULL where it has none. */
const char* portAreaName(const char* area);
/* The record's "File 1 (Mario)", "" before a save is loaded. */
const char* portAreaMapsFile(void);
/* The room counts over every area (for achievements): visited of total. */
int portAreaMapsVisited(int* total);

#ifdef __cplusplus
}
/* Dear ImGui (port/settings/features/area_maps.cpp): the map at the cursor,
 * width wide and at most maxHeight high, unit the page's pixel scale. */
void portAreaMapDraw(const PortAreaView& view, float width, float maxHeight, float unit);
#endif

#endif
