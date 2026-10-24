/* Big-endian disc data -> native structs. See port/include/port/bedecode.h. */
#include "port/bedecode.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PORT_BEDECODE_STANDALONE
void OSPanic(const char* file, int line, const char* msg, ...);
#endif

/* ---------------------------------------------------------------- errors */

void portDecodeFail(PortDecodeCtx* ctx, const char* fmt, ...) {
    char msg[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(msg, sizeof(msg), fmt, va);
    va_end(va);
    if (ctx->failed) {
        return; /* report the first error only */
    }
    ctx->failed = 1;
    if (ctx->onError) {
        ctx->onError(ctx, msg);
        return;
    }
#ifdef PORT_BEDECODE_STANDALONE
    fprintf(stderr, "bedecode: %s: %s\n", ctx->name ? ctx->name : "?", msg);
    abort();
#else
    OSPanic(ctx->name ? ctx->name : "bedecode", 0, "bedecode: %s\n", msg);
#endif
}

int portDecodeCheck(PortDecodeCtx* ctx, uint32_t offset, uint32_t len, const char* what) {
    if (ctx->failed) {
        return 0;
    }
    if (offset > ctx->size || len > ctx->size - offset) {
        portDecodeFail(ctx, "%s: range 0x%X+0x%X outside blob of 0x%X bytes", what, (unsigned)offset,
                       (unsigned)len, (unsigned)ctx->size);
        return 0;
    }
    return 1;
}

uint32_t portDecodeRead32(PortDecodeCtx* ctx, uint32_t o) {
    return portDecodeCheck(ctx, o, 4, "read32") ? portBE32(ctx->blob + o) : 0;
}
uint16_t portDecodeRead16(PortDecodeCtx* ctx, uint32_t o) {
    return portDecodeCheck(ctx, o, 2, "read16") ? portBE16(ctx->blob + o) : 0;
}
uint8_t portDecodeRead8(PortDecodeCtx* ctx, uint32_t o) {
    return portDecodeCheck(ctx, o, 1, "read8") ? ctx->blob[o] : 0;
}

/* ----------------------------------------------------------------- arena */

struct PortDecodeArenaChunk {
    PortDecodeArenaChunk* next;
    size_t cap;
    size_t used;
    /* data follows, max_align_t aligned */
};

#define PORT_ARENA_HDR ((sizeof(PortDecodeArenaChunk) + 15) & ~(size_t)15)
#define PORT_ARENA_CHUNK 4096

void* portDecodeArenaAlloc(PortDecodeArena* arena, size_t size, size_t align) {
    PortDecodeArenaChunk* c = arena->head;
    size_t at;
    if (align < 8) {
        align = 8;
    }
    if (c) {
        at = (c->used + align - 1) & ~(align - 1);
        if (at + size <= c->cap) {
            c->used = at + size;
            arena->used += size;
            return (uint8_t*)c + PORT_ARENA_HDR + at;
        }
    }
    {
        size_t cap = size + align > PORT_ARENA_CHUNK ? size + align : PORT_ARENA_CHUNK;
        c = (PortDecodeArenaChunk*)malloc(PORT_ARENA_HDR + cap);
        if (!c) {
            return NULL;
        }
        c->next = arena->head;
        c->cap = cap;
        c->used = size;
        arena->head = c;
        arena->used += size;
        return (uint8_t*)c + PORT_ARENA_HDR; /* 16-aligned */
    }
}

void portDecodeArenaFree(PortDecodeArena* arena) {
    PortDecodeArenaChunk* c = arena->head;
    while (c) {
        PortDecodeArenaChunk* n = c->next;
        free(c);
        c = n;
    }
    arena->head = NULL;
    arena->used = 0;
}

static void* ctxAlloc(PortDecodeCtx* ctx, size_t size, size_t align) {
    void* p;
    if (size == 0) {
        size = 1;
    }
    p = ctx->alloc ? ctx->alloc(ctx->allocUser, size, align) : portDecodeArenaAlloc(&ctx->arena, size, align);
    if (!p) {
        portDecodeFail(ctx, "out of memory (%u bytes)", (unsigned)size);
        return NULL;
    }
    memset(p, 0, size);
    return p;
}

/* ------------------------------------------------------------------ memo */

static uint32_t memoHash(uint32_t off, const PortType* t) {
    uint32_t h = off * 2654435761u ^ (uint32_t)((uintptr_t)t >> 4) * 40503u;
    return h ^ (h >> 15);
}

static void* memoGet(PortDecodeCtx* ctx, uint32_t off, const PortType* t) {
    uint32_t i;
    if (!ctx->memoCap) {
        return NULL;
    }
    for (i = memoHash(off, t) & (ctx->memoCap - 1);; i = (i + 1) & (ctx->memoCap - 1)) {
        PortDecodeMemo* m = &ctx->memo[i];
        if (!m->type) {
            return NULL;
        }
        if (m->offset == off && m->type == t) {
            return m->native;
        }
    }
}

static int memoPut(PortDecodeCtx* ctx, uint32_t off, const PortType* t, void* native) {
    uint32_t i;
    if ((ctx->memoCount + 1) * 2 > ctx->memoCap) {
        uint32_t oldCap = ctx->memoCap, j;
        PortDecodeMemo* old = ctx->memo;
        uint32_t cap = oldCap ? oldCap * 2 : 64;
        ctx->memo = (PortDecodeMemo*)calloc(cap, sizeof(PortDecodeMemo));
        if (!ctx->memo) {
            ctx->memo = old;
            portDecodeFail(ctx, "out of memory (memo)");
            return 0;
        }
        ctx->memoCap = cap;
        ctx->memoCount = 0;
        for (j = 0; j < oldCap; j++) {
            if (old[j].type) {
                memoPut(ctx, old[j].offset, old[j].type, old[j].native);
            }
        }
        free(old);
    }
    for (i = memoHash(off, t) & (ctx->memoCap - 1);; i = (i + 1) & (ctx->memoCap - 1)) {
        PortDecodeMemo* m = &ctx->memo[i];
        if (!m->type) {
            m->offset = off;
            m->type = t;
            m->native = native;
            ctx->memoCount++;
            return 1;
        }
    }
}

/* ------------------------------------------------------------- worklist */
/* Pointed-to structs are allocated and memoized when first seen and decoded
 * from a worklist, so long linked lists do not recurse. */

typedef struct Work {
    const PortType* type;
    uint32_t off;
    void* dst;
} Work;

typedef struct WorkList {
    Work* items;
    uint32_t count, cap;
} WorkList;

static int workPush(PortDecodeCtx* ctx, WorkList* wl, const PortType* t, uint32_t off, void* dst) {
    if (wl->count == wl->cap) {
        uint32_t cap = wl->cap ? wl->cap * 2 : 32;
        Work* n = (Work*)realloc(wl->items, cap * sizeof(Work));
        if (!n) {
            portDecodeFail(ctx, "out of memory (worklist)");
            return 0;
        }
        wl->items = n;
        wl->cap = cap;
        ctx->workItems = n; /* keep the ctx's copy current for portDecodeEnd */
        ctx->workCap = cap;
    }
    wl->items[wl->count].type = t;
    wl->items[wl->count].off = off;
    wl->items[wl->count].dst = dst;
    wl->count++;
    return 1;
}

/* ---------------------------------------------------------------- decode */

static void putNativeInt(uint8_t* dst, uint32_t size, int64_t v) {
    switch (size) {
    case 1: { uint8_t x = (uint8_t)v; memcpy(dst, &x, 1); break; }
    case 2: { uint16_t x = (uint16_t)v; memcpy(dst, &x, 2); break; }
    case 4: { uint32_t x = (uint32_t)v; memcpy(dst, &x, 4); break; }
    case 8: { uint64_t x = (uint64_t)v; memcpy(dst, &x, 8); break; }
    default: break;
    }
}

static void putPtr(uint8_t* dst, const void* p) {
    memcpy(dst, &p, sizeof(p));
}

/* Pointer word at off: 0 = NULL (also when ctx->isPointer rejects the word). */
static uint32_t readPtr(PortDecodeCtx* ctx, uint32_t off) {
    uint32_t o = portDecodeRead32(ctx, off);
    if (o && ctx->isPointer && !ctx->isPointer(ctx, off)) {
        return 0;
    }
    return o;
}

static uint32_t readWidth(PortDecodeCtx* ctx, uint32_t off, uint32_t width) {
    switch (width) {
    case 1: return portDecodeRead8(ctx, off);
    case 2: return portDecodeRead16(ctx, off);
    default: return portDecodeRead32(ctx, off);
    }
}

/* Elements of an inline array field: fixed (inlineArray 1), flexible array
 * member counted by a sibling field (inlineArray 2), or 1 (not an array). */
static uint32_t inlineCount(PortDecodeCtx* ctx, const PortField* f, uint32_t structOff) {
    if (f->inlineArray == 2) {
        return readWidth(ctx, structOff + f->countOffset, f->countWidth);
    }
    return f->inlineArray ? f->count : 1;
}

/* Native bytes for one node of t at off: types ending in a flexible array
 * member are allocated with their elements. */
static size_t nodeSize(PortDecodeCtx* ctx, const PortType* t, uint32_t off, const PortField** flexOut) {
    uint32_t i;
    for (i = 0; i < t->fieldCount; i++) {
        const PortField* f = &t->fields[i];
        if (f->inlineArray == 2) {
            uint32_t n = inlineCount(ctx, f, off);
            size_t sz = f->nativeOffset + (size_t)n * f->nativeStride;
            if ((uint64_t)n * f->gcStride > ctx->size || !portDecodeCheck(ctx, off + f->gcOffset, n * f->gcStride, f->name)) {
                portDecodeFail(ctx, "%s.%s: %u elements at 0x%X run past the blob", t->name, f->name, (unsigned)n,
                               (unsigned)(off + f->gcOffset));
                return 0;
            }
            if (flexOut) {
                *flexOut = f;
            }
            return sz > t->nativeSize ? sz : t->nativeSize;
        }
    }
    return t->nativeSize;
}

/* Allocates (or finds) count elements of t at off; queues new ones. */
static void* reserveArray(PortDecodeCtx* ctx, WorkList* wl, const PortType* t, uint32_t off, uint32_t count) {
    uint8_t* arr;
    uint32_t i;
    void* hit;
    const PortField* flex = NULL;
    size_t flexSize;

    if (count == 0) {
        return NULL;
    }
    if (!portDecodeCheck(ctx, off, t->gcSize, t->name)) {
        return NULL;
    }
    if ((uint64_t)t->gcSize * count > ctx->size - off) {
        portDecodeFail(ctx, "%s[%u] at 0x%X runs past the blob (0x%X bytes)", t->name, (unsigned)count,
                       (unsigned)off, (unsigned)ctx->size);
        return NULL;
    }
    hit = memoGet(ctx, off, t);
    if (hit) {
        /* Already decoded: every element must be in the same native block. */
        for (i = 1; i < count; i++) {
            if (memoGet(ctx, off + i * t->gcSize, t) != (uint8_t*)hit + (size_t)i * t->nativeSize) {
                portDecodeFail(ctx, "%s at 0x%X decoded with conflicting element counts", t->name, (unsigned)off);
                return NULL;
            }
        }
        return hit;
    }
    flexSize = nodeSize(ctx, t, off, &flex);
    if (ctx->failed) {
        return NULL;
    }
    if (flex && count != 1) {
        portDecodeFail(ctx, "%s[%u] at 0x%X: arrays of a type with a flexible array member", t->name, (unsigned)count,
                       (unsigned)off);
        return NULL;
    }
    arr = (uint8_t*)ctxAlloc(ctx, flex ? flexSize : (size_t)t->nativeSize * count, t->nativeAlign);
    if (!arr) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        uint32_t eo = off + i * t->gcSize;
        uint8_t* ed = arr + (size_t)i * t->nativeSize;
        if (memoGet(ctx, eo, t)) {
            portDecodeFail(ctx, "%s[%u] at 0x%X overlaps an earlier decode", t->name, (unsigned)i, (unsigned)eo);
            return NULL;
        }
        if (!memoPut(ctx, eo, t, ed) || !workPush(ctx, wl, t, eo, ed)) {
            return NULL;
        }
    }
    return arr;
}

static uint32_t fieldCount(PortDecodeCtx* ctx, const PortField* f, uint32_t structOff, uint32_t target) {
    switch (f->countKind) {
    case PORT_CNT_ONE: return 1;
    case PORT_CNT_FIXED: return f->count;
    case PORT_CNT_FIELD: return readWidth(ctx, structOff + f->countOffset, f->countWidth);
    case PORT_CNT_CALLBACK: return f->countFn ? f->countFn(ctx, structOff) : 0;
    case PORT_CNT_UNTIL: {
        uint32_t n = 0, o = target;
        const PortType* t = f->target;
        for (;;) {
            if (!portDecodeCheck(ctx, o, t->gcSize, "terminated array")) {
                return 0;
            }
            n++;
            if ((int64_t)readWidth(ctx, o + f->countOffset, f->countWidth) == f->value) {
                return n; /* the terminator is part of the array */
            }
            o += t->gcSize;
        }
    }
    default:
        portDecodeFail(ctx, "%s: bad count kind %u", f->name, (unsigned)f->countKind);
        return 0;
    }
}

static void decodeInto(PortDecodeCtx* ctx, WorkList* wl, const PortType* t, uint32_t off, uint8_t* dst);

static void decodeField(PortDecodeCtx* ctx, WorkList* wl, const PortField* f, uint32_t structOff, uint32_t src,
                        uint8_t* dst) {
    uint32_t o;
    switch (f->kind) {
    case PORT_FK_U8: putNativeInt(dst, f->nativeSize, portDecodeRead8(ctx, src)); break;
    case PORT_FK_S8: putNativeInt(dst, f->nativeSize, (int8_t)portDecodeRead8(ctx, src)); break;
    case PORT_FK_U16: putNativeInt(dst, f->nativeSize, portDecodeRead16(ctx, src)); break;
    case PORT_FK_S16: putNativeInt(dst, f->nativeSize, (int16_t)portDecodeRead16(ctx, src)); break;
    case PORT_FK_U32: putNativeInt(dst, f->nativeSize, portDecodeRead32(ctx, src)); break;
    case PORT_FK_S32: putNativeInt(dst, f->nativeSize, (int32_t)portDecodeRead32(ctx, src)); break;
    case PORT_FK_U64:
    case PORT_FK_S64:
        if (portDecodeCheck(ctx, src, 8, f->name)) {
            putNativeInt(dst, f->nativeSize, (int64_t)portBE64(ctx->blob + src));
        }
        break;
    case PORT_FK_F32:
        if (portDecodeCheck(ctx, src, 4, f->name)) {
            float v = portBEF32(ctx->blob + src);
            memcpy(dst, &v, 4);
        }
        break;
    case PORT_FK_F64:
        if (portDecodeCheck(ctx, src, 8, f->name)) {
            double v = portBEF64(ctx->blob + src);
            memcpy(dst, &v, 8);
        }
        break;
    case PORT_FK_CONST: putNativeInt(dst, f->nativeSize, f->value); break;
    case PORT_FK_PTR_NULL: putPtr(dst, NULL); break;
    case PORT_FK_PTR_RAW: {
        uintptr_t v = portDecodeRead32(ctx, src);
        memcpy(dst, &v, sizeof(v));
        break;
    }
    case PORT_FK_PTR_PAYLOAD:
        o = readPtr(ctx, src);
        /* 'payload if count': with a zero count the word is unused (often stale).
         * value 1: an inline array whose element i is gated by count[i]. */
        if (f->countKind == PORT_CNT_FIELD) {
            uint32_t at = structOff + f->countOffset;
            if (f->value == 1 && f->inlineArray && f->gcStride) {
                at += (src - structOff - f->gcOffset) / f->gcStride * f->countWidth;
            }
            if (readWidth(ctx, at, f->countWidth) == 0) {
                o = 0;
            }
        }
        if (o == 0) {
            putPtr(dst, NULL);
        } else if (portDecodeCheck(ctx, o, 1, f->name)) {
            putPtr(dst, ctx->blob + o);
        }
        break;
    case PORT_FK_PTR_STRING:
        o = readPtr(ctx, src);
        if (o == 0) {
            putPtr(dst, NULL);
        } else if (portDecodeCheck(ctx, o, 1, f->name)) {
            if (!memchr(ctx->blob + o, 0, ctx->size - o)) {
                portDecodeFail(ctx, "%s: string at 0x%X is not terminated inside the blob", f->name, (unsigned)o);
                return;
            }
            putPtr(dst, ctx->blob + o);
        }
        break;
    case PORT_FK_PTR_STRUCT: {
        uint32_t n;
        o = readPtr(ctx, src);
        if (o == 0 || ctx->failed) {
            putPtr(dst, NULL);
            break;
        }
        /* elements of an inline pointer array point to one struct each */
        n = f->inlineArray ? 1 : fieldCount(ctx, f, structOff, o);
        /* an empty table's offset word is unused (often stale): not checked */
        putPtr(dst, ctx->failed || n == 0 ? NULL : reserveArray(ctx, wl, f->target, o, n));
        break;
    }
    case PORT_FK_EMBED:
        if (!memoGet(ctx, src, f->target)) {
            memoPut(ctx, src, f->target, dst);
        }
        decodeInto(ctx, wl, f->target, src, dst);
        break;
    default:
        portDecodeFail(ctx, "%s: bad field kind %u", f->name, (unsigned)f->kind);
        break;
    }
}

static void decodeInto(PortDecodeCtx* ctx, WorkList* wl, const PortType* t, uint32_t off, uint8_t* dst) {
    uint32_t i, j;
    if (!portDecodeCheck(ctx, off, t->gcSize, t->name)) {
        return;
    }
    for (i = 0; i < t->fieldCount && !ctx->failed; i++) {
        const PortField* f = &t->fields[i];
        uint32_t n = inlineCount(ctx, f, off);
        for (j = 0; j < n && !ctx->failed; j++) {
            decodeField(ctx, wl, f, off, off + f->gcOffset + j * f->gcStride,
                        dst + f->nativeOffset + (size_t)j * f->nativeStride);
        }
    }
}

void portDecodeBegin(PortDecodeCtx* ctx, const void* blob, uint32_t size, const char* name) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->blob = (const uint8_t*)blob;
    ctx->size = size;
    ctx->name = name;
}

/* The worklist lives in the ctx so an onError that longjmps out leaks
 * nothing: portDecodeEnd frees it. Not reentrant (count callbacks must not
 * call portDecode on the same ctx). */
void* portDecodeArray(PortDecodeCtx* ctx, const PortType* type, uint32_t off, uint32_t count) {
    WorkList wl;
    void* root;
    if (ctx->failed) {
        return NULL;
    }
    wl.items = (Work*)ctx->workItems;
    wl.count = 0;
    wl.cap = ctx->workCap;
    root = reserveArray(ctx, &wl, type, off, count);
    ctx->workItems = wl.items;
    ctx->workCap = wl.cap;
    while (wl.count && !ctx->failed) {
        Work w = wl.items[--wl.count];
        decodeInto(ctx, &wl, w.type, w.off, (uint8_t*)w.dst);
        ctx->workItems = wl.items;
        ctx->workCap = wl.cap;
    }
    return ctx->failed ? NULL : root;
}

void* portDecode(PortDecodeCtx* ctx, const PortType* type, uint32_t off) {
    return portDecodeArray(ctx, type, off, 1);
}

void portDecodeEnd(PortDecodeCtx* ctx) {
    free(ctx->memo);
    ctx->memo = NULL;
    free(ctx->workItems);
    ctx->workItems = NULL;
    ctx->workCount = ctx->workCap = 0;
    ctx->memoCap = ctx->memoCount = 0;
}

/* Decode one struct at off into caller memory (nativeSize bytes at dst).
 * Pointed-to structs still come from the ctx allocator. */
int portDecodeInto(PortDecodeCtx* ctx, const PortType* type, uint32_t off, void* dst) {
    WorkList wl;
    if (ctx->failed) {
        return 0;
    }
    if (!portDecodeCheck(ctx, off, type->gcSize, type->name)) {
        return 0;
    }
    if (nodeSize(ctx, type, off, NULL) != type->nativeSize) {
        portDecodeFail(ctx, "%s at 0x%X: flexible array elements do not fit caller memory", type->name, (unsigned)off);
        return 0;
    }
    memset(dst, 0, type->nativeSize);
    if (!memoPut(ctx, off, type, dst)) {
        return 0;
    }
    wl.items = (Work*)ctx->workItems;
    wl.count = 0;
    wl.cap = ctx->workCap;
    decodeInto(ctx, &wl, type, off, (uint8_t*)dst);
    ctx->workItems = wl.items;
    ctx->workCap = wl.cap;
    while (wl.count && !ctx->failed) {
        Work w = wl.items[--wl.count];
        decodeInto(ctx, &wl, w.type, w.off, (uint8_t*)w.dst);
        ctx->workItems = wl.items;
        ctx->workCap = wl.cap;
    }
    return !ctx->failed;
}

/* ---------------------------------------------------------------- encode */

static int64_t getNativeInt(const uint8_t* src, uint32_t size, int isSigned) {
    switch (size) {
    case 1: { uint8_t x; memcpy(&x, src, 1); return isSigned ? (int8_t)x : x; }
    case 2: { uint16_t x; memcpy(&x, src, 2); return isSigned ? (int16_t)x : x; }
    case 4: { uint32_t x; memcpy(&x, src, 4); return isSigned ? (int32_t)x : x; }
    case 8: { uint64_t x; memcpy(&x, src, 8); return (int64_t)x; }
    default: return 0;
    }
}

static void putBE(uint8_t* dst, uint32_t width, uint64_t v) {
    uint32_t i;
    for (i = 0; i < width; i++) {
        dst[i] = (uint8_t)(v >> (8 * (width - 1 - i)));
    }
}

static int encodeInto(const PortType* t, const uint8_t* src, uint8_t* dst, uint8_t* end);

static int encodeField(const PortField* f, const uint8_t* src, uint8_t* dst, uint8_t* end) {
    static const uint8_t widths[] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
    if (f->kind <= PORT_FK_F64) {
        uint32_t w = widths[f->kind];
        if (dst + w > end) {
            return 0;
        }
        if (f->kind == PORT_FK_F32) {
            uint32_t v;
            memcpy(&v, src, 4);
            putBE(dst, 4, v);
        } else if (f->kind == PORT_FK_F64) {
            uint64_t v;
            memcpy(&v, src, 8);
            putBE(dst, 8, v);
        } else {
            int s = f->kind == PORT_FK_S8 || f->kind == PORT_FK_S16 || f->kind == PORT_FK_S32 || f->kind == PORT_FK_S64;
            putBE(dst, w, (uint64_t)getNativeInt(src, f->nativeSize, s));
        }
        return 1;
    }
    switch (f->kind) {
    case PORT_FK_CONST:
        return 1; /* native-only value: the disc keeps whatever the caller pre-filled */
    case PORT_FK_PTR_NULL:
    case PORT_FK_PTR_PAYLOAD:
    case PORT_FK_PTR_STRING:
    case PORT_FK_PTR_STRUCT:
        /* Pointers do not survive a round trip: written as 0 (NULL). */
        if (dst + 4 > end) {
            return 0;
        }
        putBE(dst, 4, 0);
        return 1;
    case PORT_FK_PTR_RAW: {
        uintptr_t v;
        if (dst + 4 > end) {
            return 0;
        }
        memcpy(&v, src, sizeof(v));
        putBE(dst, 4, (uint32_t)v);
        return 1;
    }
    case PORT_FK_EMBED:
        return encodeInto(f->target, src, dst, end);
    default:
        return 0;
    }
}

static int encodeInto(const PortType* t, const uint8_t* src, uint8_t* dst, uint8_t* end) {
    uint32_t i, j;
    if (dst + t->gcSize > end) {
        return 0;
    }
    for (i = 0; i < t->fieldCount; i++) {
        const PortField* f = &t->fields[i];
        uint32_t n = f->inlineArray == 2 ? 0 : f->inlineArray ? f->count : 1; /* flexible: not encoded */
        for (j = 0; j < n; j++) {
            if (!encodeField(f, src + f->nativeOffset + (size_t)j * f->nativeStride,
                             dst + f->gcOffset + (size_t)j * f->gcStride, end)) {
                return 0;
            }
        }
    }
    return 1;
}

int portEncode(const PortType* type, const void* native, void* disc, uint32_t discSize) {
    if (discSize < type->gcSize) {
        return 0;
    }
    return encodeInto(type, (const uint8_t*)native, (uint8_t*)disc, (uint8_t*)disc + discSize);
}

/* ------------------------------------------------------- in-place swap */
/* For pointer-free formats whose native layout equals the disc layout: swap
 * every described scalar of count contiguous elements in place. */
static int swapInPlace(const PortType* t, uint8_t* p) {
    static const uint8_t widths[] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
    uint32_t i, j;
    for (i = 0; i < t->fieldCount; i++) {
        const PortField* f = &t->fields[i];
        uint32_t n = f->inlineArray ? f->count : 1;
        if (f->gcOffset != f->nativeOffset || (f->inlineArray && f->gcStride != f->nativeStride)) {
            return 0;
        }
        for (j = 0; j < n; j++) {
            uint8_t* q = p + f->gcOffset + (size_t)j * f->gcStride;
            if (f->kind <= PORT_FK_F64) {
                uint32_t w = widths[f->kind], k;
                if (w != f->nativeSize) {
                    return 0;
                }
                for (k = 0; k < w / 2; k++) {
                    uint8_t x = q[k];
                    q[k] = q[w - 1 - k];
                    q[w - 1 - k] = x;
                }
            } else if (f->kind == PORT_FK_EMBED) {
                if (!swapInPlace(f->target, q)) {
                    return 0;
                }
            } else {
                return 0; /* pointers/constants: not an in-place format */
            }
        }
    }
    return 1;
}

int portSwapInPlace(const PortType* type, void* p, uint32_t count) {
    uint32_t i;
    if (type->gcSize != type->nativeSize) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (!swapInPlace(type, (uint8_t*)p + (size_t)i * type->gcSize)) {
            return 0;
        }
    }
    return 1;
}
