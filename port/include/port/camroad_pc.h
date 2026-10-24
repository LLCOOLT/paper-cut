#ifndef PORT_CAMROAD_PC_H
#define PORT_CAMROAD_PC_H
/* Camera road files on PC (camRoadSetup, src/cam_road.c).
 *
 * The GameCube code relocates the header's offsets in place. On PC the header
 * (port/bedecode/camroad.spec) is decoded into a native CamRoadMap (64-bit
 * pointers into the blob) and the tables it points to are byte-swapped in
 * place; camRoadSetup keeps the native header in wp->map.
 *
 * A blob can be set up more than once (camroad data inside a map archive stays
 * loaded across camRoadSetup calls): the first decode marks the blob (its
 * first 8 bytes, part of the header pad already copied to the native header)
 * and later calls return the same native header without swapping again.
 * Native headers are owned by a small registry keyed by blob address; a fresh
 * (unmarked) file at a known address reuses that slot. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CamRoadMap;
struct PortDecodeCtx;
struct CamRoadMap* portCamRoadDecode(void* blob, uint32_t size, const char* name);
/* Error handler for tests (NULL: OSPanic). */
extern void (*portCamRoadErrorHook)(struct PortDecodeCtx* ctx, const char* msg);

#ifdef __cplusplus
}
#endif
#endif
