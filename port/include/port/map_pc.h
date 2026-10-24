#ifndef PORT_MAP_PC_H
#define PORT_MAP_PC_H
/* Map 'd' files (./m/<map>/d) on PC. See docs/PC_PORT.md, "Loader design",
 * and port/bedecode/map.spec.
 *
 * File layout (big-endian): a 0x20-byte MapFileHeader {fileSize, dataSize,
 * relCount, chunkCount, unusedCount, reserved[0xC]}, then dataSize bytes of
 * data, relCount u32 relocation offsets (data-relative positions of pointer
 * words), chunkCount {dataOffset, nameOffset} pairs, unusedCount 8-byte
 * entries and the chunk-name string table.
 *
 * On the GameCube mapLoadReloc_ adds the data address to every relocated
 * word in place. portMapDecode does the PC equivalent:
 *   1. relocates in place once: every relocated word becomes file-relative
 *      (+0x20), so a pointer to data+0 is not mistaken for NULL; a marker in
 *      the header's reserved bytes makes this idempotent (archive-resident
 *      maps are loaded more than once);
 *   2. decodes each known chunk by name into native structs
 *      (include/driver/mapfile.h, descriptors from map.spec) in a per-map
 *      arena: "information" (joint tree, materials, samplers, textures,
 *      draw modes), "texture_table", "material_name_table", "light_table",
 *      "fog_table", "vcd_table", "animation_table";
 *   3. decodes meshes (MapFileJointPart.mesh) by hand: packed GX display
 *      lists stay in the file (payload + byte size), unpacked vertex records
 *      (MapRawDL) and the vcd table become native;
 *   4. checks that every relocated word was read as a pointer by some
 *      decoded struct (portMapUncoveredRelocs: a nonzero count means a
 *      chunk or field the specs do not describe yet).
 * GX payloads stay big-endian in the file (vertex arrays: register the file
 * with portGXRegisterDiscBlob; display lists).
 *
 * The decoded graph does not depend on the file's address except for its
 * payload/string pointers: after the file is copied elsewhere (bmapLoad /
 * bmapUnLoad ARAM round trip) call portMapRebase(d, newFile). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_MAP_HEADER_SIZE 0x20u
#define PORT_MAP_RELOC_MARK 0x50435231u /* "PCR1" at header +0x14 once relocated */

typedef struct PortMapData PortMapData;
struct PortDecodeCtx;

/* Returns NULL (after reporting through portMapErrorHook / OSPanic) when the
 * file is malformed. A header fileSize that differs from fileSize returns
 * NULL without an error (the GameCube code then skips the header too). */
PortMapData* portMapDecode(void* file, uint32_t fileSize, const char* name);
/* Native root of a chunk ("information" -> MapFileInfo*, "texture_table" ->
 * MapTexTable*, ...), or NULL when the file has no such chunk. */
void* portMapChunk(const PortMapData* d, const char* chunkName);
void portMapRelease(PortMapData* d);
void portMapRebase(PortMapData* d, void* newFile);
void* portMapFile(const PortMapData* d);
/* Relocated words no decoded field read as a pointer; fills up to max
 * file offsets of them. */
uint32_t portMapUncoveredRelocs(const PortMapData* d, uint32_t* offsets, uint32_t max);
/* Relocated words that decoded fields read (diagnostics, tests). */
uint32_t portMapRelocCount(const PortMapData* d);

/* Error handler for tests (NULL: OSPanic / abort when standalone). */
extern void (*portMapErrorHook)(struct PortDecodeCtx* ctx, const char* msg);

/* ---- CPU reads of geometry the GPU also reads (hitdrv collision) ----
 * Vertex arrays in a map file are big-endian; arrays built in code (mobj
 * collision boxes) are native. portMapIsDiscData tells them apart through
 * the GX array registry (port/gx_arrays.h). */
int portMapIsDiscData(const void* p);

static inline uint16_t portMapLoadU16(const void* p, int be) {
    const uint8_t* b = (const uint8_t*)p;
    uint16_t v;
    if (be) {
        return (uint16_t)((b[0] << 8) | b[1]);
    }
    __builtin_memcpy(&v, p, 2);
    return v;
}

static inline int16_t portMapLoadS16(const void* p, int be) {
    return (int16_t)portMapLoadU16(p, be);
}

static inline float portMapLoadF32(const void* p, int be) {
    const uint8_t* b = (const uint8_t*)p;
    union { uint32_t u; float f; } v;
    if (be) {
        v.u = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    } else {
        __builtin_memcpy(&v.u, p, 4);
    }
    return v.f;
}

/* Reads f32[3] element idx of a position array (returns the 3 floats). */
static inline void portMapLoadVec(const void* base, int32_t idx, int be, float out[3]) {
    const uint8_t* p = (const uint8_t*)base + (intptr_t)idx * 12;
    out[0] = portMapLoadF32(p, be);
    out[1] = portMapLoadF32(p + 4, be);
    out[2] = portMapLoadF32(p + 8, be);
}

#ifdef __cplusplus
}
#endif
#endif
