/* Map 'd' file decoding on PC; see port/include/port/map_pc.h. */
#include "port/map_pc.h"
#include "port/bedecode.h"
#include "port/gx_arrays.h"
#include "driver/mapfile.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef PORT_BEDECODE_STANDALONE
#include <dolphin/os.h>
#endif

extern const PortType portBE_map_MapFileInfo;
extern const PortType portBE_map_MapFileJointPart;
extern const PortType portBE_map_MapTexTable;
extern const PortType portBE_map_MapMaterialNameTable;
extern const PortType portBE_map_MapFileLightTable;
extern const PortType portBE_map_MapFileFog;
extern const PortType portBE_map_MapVcdTable;
extern const PortType portBE_map_MapRawDL;
extern const PortType portBE_map_MapAnimTable;

void (*portMapErrorHook)(struct PortDecodeCtx* ctx, const char* msg);

/* Chunk name -> root type. */
static const struct {
    const char* name;
    const PortType* type;
} kChunks[] = {
    {"information", &portBE_map_MapFileInfo},
    {"texture_table", &portBE_map_MapTexTable},
    {"material_name_table", &portBE_map_MapMaterialNameTable},
    {"light_table", &portBE_map_MapFileLightTable},
    {"fog_table", &portBE_map_MapFileFog},
    {"vcd_table", &portBE_map_MapVcdTable},
    {"animation_table", &portBE_map_MapAnimTable},
};
#define NCHUNK (sizeof(kChunks) / sizeof(kChunks[0]))

typedef struct MeshRec {
    uint32_t off;
    MapFileMesh* native;
} MeshRec;

struct PortMapData {
    uint8_t* file;
    uint32_t size;
    PortDecodeArena arena;
    void* roots[NCHUNK];
    /* native pointer slots that point into the file (payload / string) */
    void*** slots;
    uint32_t slotCount, slotCap;
    /* relocation bookkeeping (file offsets of pointer words; not always
     * word-aligned, see portMapDecode) */
    uint32_t relocCount;
    uint32_t coveredCount;
    uint32_t* uncovered;
    uint32_t uncoveredCount;
};

/* ------------------------------------------------------------- helpers */

typedef struct Build {
    PortDecodeCtx ctx;
    PortMapData* d;
    uint32_t* relocBits;  /* bit per file byte: a relocated word starts here */
    uint32_t* coverBits;  /* bit per file byte: the word here was read as a pointer */
    MeshRec* meshes;
    uint32_t meshCount, meshCap;
} Build;

static int bitGet(const uint32_t* bits, uint32_t i) {
    return (bits[i >> 5] >> (i & 31)) & 1;
}

static void bitSet(uint32_t* bits, uint32_t i) {
    bits[i >> 5] |= 1u << (i & 31);
}

/* Only words in the relocation table are pointers (others are NULL even if
 * nonzero, as on the GameCube where they were never patched). */
static int isRelocated(PortDecodeCtx* ctx, uint32_t off) {
    Build* b = (Build*)ctx->user;
    return off + 4 <= b->d->size && bitGet(b->relocBits, off);
}

static uint32_t relocPtr(Build* b, uint32_t off) {
    uint32_t v = portDecodeRead32(&b->ctx, off);
    return v && isRelocated(&b->ctx, off) ? v : 0;
}

static void cover(Build* b, uint32_t off) {
    if (off + 4 <= b->d->size) {
        bitSet(b->coverBits, off);
    }
}

static int addSlot(Build* b, void** slot) {
    PortMapData* d = b->d;
    if (d->slotCount == d->slotCap) {
        uint32_t cap = d->slotCap ? d->slotCap * 2 : 256;
        void*** n = (void***)realloc(d->slots, cap * sizeof(void**));
        if (!n) {
            portDecodeFail(&b->ctx, "out of memory (map slots)");
            return 0;
        }
        d->slots = n;
        d->slotCap = cap;
    }
    d->slots[d->slotCount++] = slot;
    return 1;
}

static uint32_t elemCount(Build* b, const PortField* f, uint32_t off) {
    if (f->inlineArray == 2) {
        uint32_t o = off + f->countOffset;
        if (o + f->countWidth > b->d->size) {
            return 0;
        }
        return f->countWidth == 1 ? b->d->file[o] : f->countWidth == 2 ? portBE16(b->d->file + o)
                                                                        : portBE32(b->d->file + o);
    }
    return f->inlineArray ? f->count : 1;
}

/* Marks the pointer words of a decoded node (and its embedded structs) and
 * records its payload/string slots. */
static void walkNode(Build* b, const PortType* t, uint32_t off, uint8_t* native, int depth) {
    uint32_t i, j;
    if (depth > 16) {
        return;
    }
    for (i = 0; i < t->fieldCount; i++) {
        const PortField* f = &t->fields[i];
        uint32_t n = elemCount(b, f, off);
        for (j = 0; j < n; j++) {
            uint32_t src = off + f->gcOffset + j * f->gcStride;
            uint8_t* dst = native + f->nativeOffset + (size_t)j * f->nativeStride;
            switch (f->kind) {
            case PORT_FK_PTR_PAYLOAD:
            case PORT_FK_PTR_STRING:
                cover(b, src);
                addSlot(b, (void**)dst);
                break;
            case PORT_FK_PTR_STRUCT:
            case PORT_FK_PTR_NULL:
            case PORT_FK_PTR_RAW:
                cover(b, src);
                break;
            case PORT_FK_EMBED:
                walkNode(b, f->target, src, dst, depth + 1);
                break;
            default:
                break;
            }
        }
    }
}

static int cmpSlot(const void* a, const void* b) {
    uintptr_t x = (uintptr_t)*(void***)a, y = (uintptr_t)*(void***)b;
    return x < y ? -1 : x > y;
}

/* --------------------------------------------------------------- meshes */

static MapFileMesh* decodeMesh(Build* b, uint32_t m) {
    PortDecodeCtx* ctx = &b->ctx;
    const uint8_t* f = b->d->file;
    uint32_t i, n, packed, elem, vcd;
    size_t sz;
    MapFileMesh* mesh;

    for (i = 0; i < b->meshCount; i++) {
        if (b->meshes[i].off == m) {
            return b->meshes[i].native;
        }
    }
    if (!portDecodeCheck(ctx, m, 0x10, "MapFileMesh")) {
        return NULL;
    }
    packed = f[m + 3];
    n = portBE32(f + m + 4);
    elem = packed ? 8 : 4;
    if (n > (b->d->size - m - 0x10) / elem) {
        portDecodeFail(ctx, "MapFileMesh at 0x%X: %u display lists run past the file", (unsigned)m, (unsigned)n);
        return NULL;
    }
    sz = offsetof(MapFileMesh, displayLists) +
         (size_t)(n ? n : 1) * (packed ? sizeof(PackedDisplayList) : sizeof(struct MapRawDL*));
    mesh = (MapFileMesh*)portDecodeArenaAlloc(&ctx->arena, sz, 8);
    if (!mesh) {
        portDecodeFail(ctx, "out of memory (MapFileMesh)");
        return NULL;
    }
    memset(mesh, 0, sz);
    if (b->meshCount == b->meshCap) {
        uint32_t cap = b->meshCap ? b->meshCap * 2 : 64;
        MeshRec* r = (MeshRec*)realloc(b->meshes, cap * sizeof(MeshRec));
        if (!r) {
            portDecodeFail(ctx, "out of memory (mesh list)");
            return NULL;
        }
        b->meshes = r;
        b->meshCap = cap;
    }
    b->meshes[b->meshCount].off = m;
    b->meshes[b->meshCount].native = mesh;
    b->meshCount++;

    mesh->reserved_00 = f[m];
    mesh->padding[0] = f[m + 1];
    mesh->padding[1] = f[m + 2];
    mesh->isPackedDisplay = (u8)packed;
    mesh->displayListCount = (s32)n;
    mesh->elementMask = portBE32(f + m + 8);
    vcd = relocPtr(b, m + 0xC);
    cover(b, m + 0xC);
    if (vcd) {
        mesh->vcdTable = (MapVcdTable*)portDecode(ctx, &portBE_map_MapVcdTable, vcd);
    }
    for (i = 0; i < n && !ctx->failed; i++) {
        uint32_t w = m + 0x10 + i * elem;
        uint32_t o = relocPtr(b, w);
        cover(b, w);
        if (packed) {
            uint32_t size = portBE32(f + w + 4);
            mesh->displayLists.packedLists[i].size = size;
            if (o) {
                if (!portDecodeCheck(ctx, o, size ? size : 1, "packed display list")) {
                    return NULL;
                }
                mesh->displayLists.packedLists[i].offset = (AddrWord)(f + o);
                addSlot(b, (void**)&mesh->displayLists.packedLists[i].offset);
            }
        } else if (o) {
            mesh->displayLists.normalLists[i] = (MapRawDL*)portDecode(ctx, &portBE_map_MapRawDL, o);
        }
    }
    return ctx->failed ? NULL : mesh;
}

/* ----------------------------------------------------------------- api */

static void freeData(PortMapData* d) {
    if (!d) {
        return;
    }
    portDecodeArenaFree(&d->arena);
    free(d->slots);
    free(d->uncovered);
    free(d);
}

static void freeBuild(Build* b) {
    free(b->relocBits);
    free(b->coverBits);
    free(b->meshes);
    portDecodeEnd(&b->ctx);
}

PortMapData* portMapDecode(void* file, uint32_t fileSize, const char* name) {
    Build b;
    PortMapData* d;
    PortDecodeCtx* ctx = &b.ctx;
    uint8_t* f = (uint8_t*)file;
    uint32_t dataSize, relCount, chunkCount, unusedCount, relOff, chunkOff, strOff, at, k, words;
    uint64_t end;

    memset(&b, 0, sizeof(b));
    portDecodeBegin(ctx, file, fileSize, name ? name : "map");
    ctx->onError = portMapErrorHook;
    ctx->user = &b;
    if (!file || fileSize < PORT_MAP_HEADER_SIZE || ((uintptr_t)file & 3)) {
        portDecodeFail(ctx, "map file %p (0x%X bytes) is too small or misaligned", file, (unsigned)fileSize);
        return NULL;
    }
    if (portBE32(f) != fileSize) {
        return NULL; /* the GameCube loader ignores the header too */
    }
    dataSize = portBE32(f + 4);
    relCount = portBE32(f + 8);
    chunkCount = portBE32(f + 0xC);
    unusedCount = portBE32(f + 0x10);
    at = PORT_MAP_HEADER_SIZE;
    end = (uint64_t)PORT_MAP_HEADER_SIZE + dataSize;
    if (dataSize) {
        at = (uint32_t)(end > fileSize ? fileSize : end);
    }
    relOff = at;
    end = (uint64_t)at + (uint64_t)relCount * 4;
    chunkOff = (uint32_t)(end > fileSize ? fileSize : end);
    end += (uint64_t)chunkCount * 8;
    end += (uint64_t)unusedCount * 8;
    strOff = (uint32_t)(end > fileSize ? fileSize : end);
    if (end > fileSize || (uint64_t)PORT_MAP_HEADER_SIZE + dataSize > fileSize) {
        portDecodeFail(ctx, "map header tables (data 0x%X, %u relocs, %u chunks) run past the file (0x%X bytes)",
                       (unsigned)dataSize, (unsigned)relCount, (unsigned)chunkCount, (unsigned)fileSize);
        return NULL;
    }

    d = (PortMapData*)calloc(1, sizeof(PortMapData));
    words = (fileSize + 31) / 32; /* bitmap words, one bit per file byte */
    b.relocBits = (uint32_t*)calloc(words, 4);
    b.coverBits = (uint32_t*)calloc(words, 4);
    if (!d || !b.relocBits || !b.coverBits) {
        free(d);
        b.d = NULL;
        portDecodeFail(ctx, "out of memory (map)");
        freeBuild(&b);
        return NULL;
    }
    b.d = d;
    d->file = f;
    d->size = fileSize;
    ctx->isPointer = isRelocated;

    /* 1. Relocation table: pointer words, made file-relative once.
     * Entries need not be word-aligned: m/end_00/d and m/sys_01/d (the same
     * 2003 stub plane) have their only material at data +0x47, so its name
     * and tevMode pointers sit at +0x47 / +0x157. mapLoadReloc_ patches them
     * through a plain u32* (the Gekko does misaligned integer loads/stores
     * in hardware), so they are relocated here too. */
    {
        int relocate = portBE32(f + 0x14) != PORT_MAP_RELOC_MARK;
        for (k = 0; k < relCount; k++) {
            uint32_t r = portBE32(f + relOff + k * 4);
            uint32_t w = PORT_MAP_HEADER_SIZE + r;
            uint32_t v;
            if (r >= dataSize || dataSize - r < 4) {
                portDecodeFail(ctx, "relocation %u: data offset 0x%X outside the data (0x%X bytes)", (unsigned)k,
                               (unsigned)r, (unsigned)dataSize);
                goto fail;
            }
            if (bitGet(b.relocBits, w)) {
                continue; /* listed twice: relocate once */
            }
            bitSet(b.relocBits, w);
            d->relocCount++;
            v = portBE32(f + w);
            if (relocate && v <= 0x80000000u) {
                portPutBE32(f + w, v + PORT_MAP_HEADER_SIZE);
            }
        }
        if (relocate) {
            portPutBE32(f + 0x14, PORT_MAP_RELOC_MARK);
        }
    }

    /* 2. Chunks by name. */
    for (k = 0; k < chunkCount && !ctx->failed; k++) {
        uint32_t off = portBE32(f + chunkOff + k * 8);
        uint32_t nameOff = portBE32(f + chunkOff + k * 8 + 4);
        const char* cname;
        uint32_t i;
        if (strOff + (uint64_t)nameOff >= fileSize || !memchr(f + strOff + nameOff, 0, fileSize - strOff - nameOff)) {
            portDecodeFail(ctx, "chunk %u: name at 0x%X is outside the string table", (unsigned)k, (unsigned)nameOff);
            goto fail;
        }
        cname = (const char*)f + strOff + nameOff;
        for (i = 0; i < NCHUNK; i++) {
            if (!strcmp(cname, kChunks[i].name)) {
                if (off >= dataSize) {
                    portDecodeFail(ctx, "chunk %s at 0x%X is outside the data", cname, (unsigned)off);
                    goto fail;
                }
                d->roots[i] = portDecode(ctx, kChunks[i].type, PORT_MAP_HEADER_SIZE + off);
                break;
            }
        }
    }
    if (ctx->failed) {
        goto fail;
    }

    /* 3. Meshes (MapFileJointPart.mesh, 'skip' in map.spec). */
    {
        uint32_t i, nParts = 0, cap = 0;
        struct {
            uint32_t off;
            MapFileJointPart* native;
        }* parts = NULL;
        for (i = 0; i < ctx->memoCap; i++) {
            if (ctx->memo[i].type == &portBE_map_MapFileJointPart) {
                if (nParts == cap) {
                    void* n;
                    cap = cap ? cap * 2 : 64;
                    n = realloc(parts, cap * sizeof(*parts));
                    if (!n) {
                        free(parts);
                        portDecodeFail(ctx, "out of memory (parts)");
                        goto fail;
                    }
                    parts = n;
                }
                parts[nParts].off = ctx->memo[i].offset;
                parts[nParts].native = (MapFileJointPart*)ctx->memo[i].native;
                nParts++;
            }
        }
        for (i = 0; i < nParts && !ctx->failed; i++) {
            uint32_t w = parts[i].off + 4;
            uint32_t m = relocPtr(&b, w);
            cover(&b, w);
            if (m) {
                parts[i].native->mesh = decodeMesh(&b, m);
            }
        }
        free(parts);
        if (ctx->failed) {
            goto fail;
        }
    }

    /* 4. Pointer coverage and payload slots of every decoded node. */
    {
        uint32_t i;
        for (i = 0; i < ctx->memoCap; i++) {
            if (ctx->memo[i].type) {
                walkNode(&b, ctx->memo[i].type, ctx->memo[i].offset, (uint8_t*)ctx->memo[i].native, 0);
            }
        }
        if (ctx->failed) {
            goto fail;
        }
        if (d->slotCount > 1) {
            uint32_t o = 1;
            qsort(d->slots, d->slotCount, sizeof(void**), cmpSlot);
            for (i = 1; i < d->slotCount; i++) {
                if (d->slots[i] != d->slots[o - 1]) {
                    d->slots[o++] = d->slots[i];
                }
            }
            d->slotCount = o;
        }
        for (i = 0; i < fileSize; i++) {
            if (bitGet(b.relocBits, i)) {
                if (bitGet(b.coverBits, i)) {
                    d->coveredCount++;
                } else {
                    uint32_t* n = (uint32_t*)realloc(d->uncovered, (d->uncoveredCount + 1) * sizeof(uint32_t));
                    if (n) {
                        d->uncovered = n;
                        d->uncovered[d->uncoveredCount++] = i;
                    }
                }
            }
        }
#ifndef PORT_BEDECODE_STANDALONE
        if (d->uncoveredCount) {
            OSReport("map_pc: %s: %u of %u relocated words not decoded (first at file +0x%X)\n", ctx->name,
                     (unsigned)d->uncoveredCount, (unsigned)d->relocCount, (unsigned)d->uncovered[0]);
        }
#endif
    }

    d->arena = ctx->arena;
    ctx->arena.head = NULL;
    freeBuild(&b);
    return d;

fail:
    portDecodeArenaFree(&ctx->arena);
    freeBuild(&b);
    freeData(d);
    return NULL;
}

void* portMapChunk(const PortMapData* d, const char* chunkName) {
    uint32_t i;
    if (!d || !chunkName) {
        return NULL;
    }
    for (i = 0; i < NCHUNK; i++) {
        if (!strcmp(chunkName, kChunks[i].name)) {
            return d->roots[i];
        }
    }
    return NULL;
}

void portMapRelease(PortMapData* d) {
    freeData(d);
}

void portMapRebase(PortMapData* d, void* newFile) {
    uint32_t i;
    uint8_t* oldBase;
    intptr_t delta;
    if (!d || !newFile || newFile == d->file) {
        return;
    }
    oldBase = d->file;
    delta = (uint8_t*)newFile - oldBase;
    for (i = 0; i < d->slotCount; i++) {
        uint8_t* p = (uint8_t*)*d->slots[i];
        if (p >= oldBase && p < oldBase + d->size) {
            *d->slots[i] = p + delta;
        }
    }
    d->file = (uint8_t*)newFile;
}

void* portMapFile(const PortMapData* d) {
    return d ? d->file : NULL;
}

uint32_t portMapUncoveredRelocs(const PortMapData* d, uint32_t* offsets, uint32_t max) {
    uint32_t i;
    if (!d) {
        return 0;
    }
    for (i = 0; offsets && i < max && i < d->uncoveredCount; i++) {
        offsets[i] = d->uncovered[i];
    }
    return d->uncoveredCount;
}

uint32_t portMapRelocCount(const PortMapData* d) {
    return d ? d->coveredCount : 0;
}

int portMapIsDiscData(const void* p) {
    unsigned int size;
    int le;
    return portGXArrayInfo(p, &size, &le) && !le;
}
