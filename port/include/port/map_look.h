#ifndef PORT_MAP_LOOK_H
#define PORT_MAP_LOOK_H

/* Map looks (port/src/common/map_look_pc.c): a room's colour beyond what the
 * game's own blend can do. The game's map blend (evt_map_set_blend) and
 * object colour (evt_mapobj_color) only multiply, so they can darken and
 * tint but never brighten: an orange over dark water is darker water. A look
 * adds two TEV stages after an object's own material, out = colour * mul + add,
 * so it can tint (mul), glow (add) or wash toward a colour (mul = 1 - t,
 * add = colour * t: a white room). Per map object (and everything under it),
 * or the whole map; cleared on every map change. The fog still applies after. */

#ifdef __cplusplus
extern "C" {
#endif

/* name: a map object or group (everything under it), NULL or "" for the whole
 * map (objects with a look of their own keep theirs). mul and add are 0xRRGGBB;
 * mul 0xFFFFFF and add 0 removes the look. 0 when name isn't in the map. */
int portMapLookSet(const char* name, unsigned int mul, unsigned int add);
/* the same for a placed map object (a mobj, by its name): its model gets the look. The
 * whole-map look doesn't reach mobjs; only this does. 0 when there's no such mobj */
int portMapLookSetMobj(const char* name, unsigned int mul, unsigned int add);
void portMapLookClear(void);
/* $TTYD_MAPOBJ_DUMP: every map object's name (and its group) to the log once a map is up */
void portMapLookDump(void);

/* mapdrv.c (mapSetMaterialTev): the look's stages for obj after the
 * material's, from TEV stage stage on; the next free stage */
int portMapLookTev(void* obj, int stage);
/* mapdrv.c: the map object mapSetMaterialTev is drawing now (set around its map-object call), or NULL for a model or image */
extern void* portMapLookObj;
/* mobjdrv.c: the mobj being drawn (around its animPoseDraw), or NULL */
extern void* portMapLookMobj;

#ifdef __cplusplus
}
#endif

#endif
