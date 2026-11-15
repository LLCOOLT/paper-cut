/* A model file's animations checked before the game plays them
 * (port/include/port/anim_check.h). Mirrors tools/port/ttyd_anim.py
 * check_model, which runs it over every model of the discs (none fails) and
 * over the Play as models. */
#include "port/anim_check.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* the model's tables: record size and header count word (ttyd_anim.TABLES) */
static const struct {
    uint32_t size, count;
} kTables[24] = {
    {0xA8, 0}, {8, 1}, {12, 2}, {4, 3}, {12, 4}, {4, 5}, {4, 6}, {4, 7},
    {4, 8}, {4, 9}, {4, 10}, {4, 11}, {4, 12}, {4, 13}, {4, 14}, {4, 15},
    {8, 16}, {0x18, 17}, {8, 18}, {0x40, 19}, {0x6C, 20}, {1, 21}, {4, 22}, {0x58, 23},
};
enum { T_SHAPES, T_DRAWCALLS, T_POS, T_NRM = 4, T_CLR = 6, T_TEXVTX = 16, T_TEXANIM, T_TEXDATA, T_TEXBINDS, T_DRAWS,
       T_VIS, T_NODE, T_GROUPS };
#define COUNT_ANIMS 24
#define HEADER_SIZE 0x1B0
#define NODE_SIZE 24
#define ANIM_HEADER_OLD 0x44
/* AnimData's sections: loop info, keys, vertex positions, normals, texture slots, visibility, node values,
 * value keys (their entries' sizes) */
static const uint32_t kSection[8] = {12, 0x2C, 4, 4, 12, 2, 4, 8};
enum { S_LOOP, S_KEY, S_POS, S_NRM, S_TEX, S_VIS, S_NODE, S_VAL };

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static float bef(const uint8_t* p) {
    uint32_t v = be32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static int fail(char* why, size_t n, const char* fmt, ...) {
    if (why != NULL && n > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(why, n, fmt, ap);
        va_end(ap);
    }
    return 0;
}

typedef struct Model {
    const uint8_t* file;
    uint32_t size;
    uint32_t count[25], ptr[25];
} Model;

static const uint8_t* rec(const Model* m, int t, uint32_t i) {
    return m->file + m->ptr[t] + i * kTables[t].size;
}

static uint32_t cnt(const Model* m, int t) {
    return m->count[kTables[t].count];
}

/* one keyframe's deltas of one kind (4 bytes each: step, then three bytes; or 2: step, add) walked as
 * animPoseMain walks them: every entry writes at the index so far, which stays under limit */
static int walk(const uint8_t* d, uint32_t start, uint32_t n, uint32_t entry, uint32_t limit) {
    uint32_t j, idx = 0;
    for (j = 0; j < n; j++) {
        idx += d[(start + j) * entry];
        if (idx >= limit) {
            return 0;
        }
    }
    return 1;
}

static int checkAnim(const Model* m, const char* name, const uint8_t* a, uint32_t avail, int strict, char* why,
                     size_t whyLen) {
    uint32_t size, n[8], off[8], i, k, hdr;
    const uint8_t *keys, *sec[8];
    float start, prevT = 0.0f;
    if (avail < ANIM_HEADER_OLD || (size = be32(a)) < ANIM_HEADER_OLD || size > avail) {
        return fail(why, whyLen, "%s: its data runs past the file", name);
    }
    for (i = 0; i < 8; i++) {
        n[i] = be32(a + 4 + 4 * i);
        off[i] = be32(a + 0x24 + 4 * i);
        if (n[i] == 0) {
            sec[i] = NULL;
            continue;
        }
        if (off[i] > size || n[i] > (size - off[i]) / kSection[i]) {
            return fail(why, whyLen, "%s: section %u runs past its data", name, i);
        }
        sec[i] = a + off[i];
    }
    hdr = off[S_LOOP] == ANIM_HEADER_OLD ? ANIM_HEADER_OLD : 0x5C;
    if (size < hdr) {
        return fail(why, whyLen, "%s: its header runs past its data", name);
    }
    if (n[S_LOOP] == 0 || n[S_KEY] == 0 || n[S_VAL] == 0) {
        return fail(why, whyLen, "%s: no loop range, keys or value keys", name);
    }
    start = bef(sec[S_LOOP] + 4);
    if (strict) {
        float end = bef(sec[S_LOOP] + 8);
        if (!isfinite(start) || !isfinite(end) || end < start) {
            return fail(why, whyLen, "%s: its loop range isn't one", name);
        }
        if (bef(sec[S_KEY]) > start || bef(sec[S_VAL]) > start) {
            return fail(why, whyLen, "%s: its first key comes after its start", name);
        }
        for (i = 0; i < n[S_VAL]; i++) {
            float t = bef(sec[S_VAL] + 8 * i);
            if (!isfinite(t) || (i > 0 && t < prevT)) {
                return fail(why, whyLen, "%s: its value keys aren't in time order", name);
            }
            prevT = t;
        }
    }
    keys = sec[S_KEY];
    for (k = 0; k < n[S_KEY]; k++) {
        const uint8_t* kf = keys + 0x2C * k;
        float t = bef(kf);
        static const int kKinds[5] = {S_POS, S_NRM, S_TEX, S_VIS, S_NODE};
        static const int kTable[5] = {T_POS, T_NRM, T_TEXANIM, T_VIS, T_NODE};
        if (strict && (!isfinite(t) || (k > 0 && t < prevT))) {
            return fail(why, whyLen, "%s: its keys aren't in time order", name);
        }
        prevT = t;
        for (i = 0; i < 5; i++) {
            const int s = kKinds[i];
            const uint32_t first = be32(kf + 4 + 8 * i), num = be32(kf + 8 + 8 * i);
            if (num == 0) {
                continue;
            }
            if (first > n[s] || num > n[s] - first) {
                return fail(why, whyLen, "%s: key %u's deltas run past their section", name, k);
            }
            if (!walk(sec[s], first, num, kSection[s], cnt(m, kTable[i]))) {
                return fail(why, whyLen, "%s: key %u moves past the model's %s", name, k,
                            s == S_POS ? "vertices" : s == S_NRM ? "normals" : s == S_TEX ? "texture slots"
                            : s == S_VIS ? "visibility values" : "node values");
            }
            /* (a texture slot's bind past the model's: the game draws its first texture, animdrv.c) */
            if (s == S_NODE && strict && k > 0) {
                /* the tangents the game looks up between keys: its table holds -89..88 degrees */
                uint32_t j;
                for (j = 0; j < num; j++) {
                    const uint8_t* d = sec[S_NODE] + 4 * (first + j);
                    const int a = (int8_t)d[2], b = (int8_t)d[3];
                    if (b > -89 && b < 89 && (a < -89 || a > 88)) {
                        return fail(why, whyLen, "%s: key %u has a tangent past the game's table", name, k);
                    }
                }
            }
        }
    }
    return 1;
}

int portAnimCheckModel(const uint8_t* file, uint32_t size, PortAnimStrictFn strict, void* user, char* why,
                       size_t whyLen) {
    Model m;
    uint32_t i, nGroups, nAnims, tableEnd;
    uint8_t seen[4096];
    int32_t stack[4096];
    int sp = 0;
    if (why != NULL && whyLen > 0) {
        why[0] = '\0';
    }
    if (file == NULL || size < HEADER_SIZE) {
        return fail(why, whyLen, "too short for a model");
    }
    m.file = file;
    m.size = size;
    for (i = 0; i < 25; i++) {
        m.count[i] = be32(file + 0xE8 + 4 * i);
        m.ptr[i] = be32(file + 0x14C + 4 * i);
    }
    for (i = 0; i < 24; i++) {
        const uint32_t c = cnt(&m, (int)i);
        if (c != 0 && (m.ptr[i] > size || c > (size - m.ptr[i]) / kTables[i].size)) {
            return fail(why, whyLen, "table %u runs past the file", i);
        }
    }
    /* the group tree: indices in range, every group once from the root (the last) */
    nGroups = cnt(&m, T_GROUPS);
    if (nGroups == 0 || nGroups > sizeof(seen)) {
        return fail(why, whyLen, "%u groups", nGroups);
    }
    for (i = 0; i < nGroups; i++) {
        const uint8_t* g = rec(&m, T_GROUPS, i);
        const int32_t bro = (int32_t)be32(g + 0x40), child = (int32_t)be32(g + 0x44);
        const int32_t shape = (int32_t)be32(g + 0x48), vis = (int32_t)be32(g + 0x4C), node = (int32_t)be32(g + 0x50);
        if (bro < -1 || bro >= (int32_t)nGroups || child < -1 || child >= (int32_t)nGroups ||
            shape < -1 || shape >= (int32_t)cnt(&m, T_SHAPES) || vis < 0 || vis >= (int32_t)cnt(&m, T_VIS) ||
            node < 0 || (uint32_t)node + NODE_SIZE > cnt(&m, T_NODE)) {
            return fail(why, whyLen, "group %u's indices are out of range", i);
        }
    }
    memset(seen, 0, nGroups);
    stack[sp++] = (int32_t)nGroups - 1;
    while (sp > 0) {
        int32_t gi = stack[--sp];
        while (gi != -1) {
            const uint8_t* g = rec(&m, T_GROUPS, (uint32_t)gi);
            const int32_t child = (int32_t)be32(g + 0x44);
            if (seen[gi]) {
                return fail(why, whyLen, "the group tree comes back to group %d", gi);
            }
            seen[gi] = 1;
            if (child != -1) {
                if (sp >= (int)(sizeof(stack) / sizeof(stack[0]))) {
                    return fail(why, whyLen, "the group tree is too deep");
                }
                stack[sp++] = child;
            }
            gi = (int32_t)be32(g + 0x40);
        }
    }
    /* the shapes' ranges in the vertex arrays and draws */
    for (i = 0; i < cnt(&m, T_SHAPES); i++) {
        static const int kArr[3] = {T_POS, T_NRM, T_CLR};
        const uint8_t* s = rec(&m, T_SHAPES, i) + 0x40;
        uint32_t j;
        /* (first, count) of its positions, normals, colours and eight texture coordinate arrays */
        for (j = 0; j < 11; j++) {
            const uint32_t first = be32(s + 8 * j), num = be32(s + 8 * j + 4);
            const uint32_t lim = cnt(&m, j < 3 ? kArr[j] : T_TEXVTX);
            if (num != 0 && (first > lim || num > lim - first)) {
                return fail(why, whyLen, "shape %u's vertices run past the model's", i);
            }
        }
        {
            const uint32_t first = be32(s + 22 * 4), num = be32(s + 23 * 4), lim = cnt(&m, T_DRAWS);
            if (num != 0 && (first > lim || num > lim - first)) {
                return fail(why, whyLen, "shape %u's draws run past the model's", i);
            }
        }
    }
    for (i = 0; i < cnt(&m, T_DRAWS); i++) {
        const uint8_t* d = rec(&m, T_DRAWS, i);
        const uint32_t first = be32(d + 0x38), num = be32(d + 0x3C), lim = cnt(&m, T_DRAWCALLS);
        if (num != 0 && (first > lim || num > lim - first)) {
            return fail(why, whyLen, "draw %u's calls run past the model's", i);
        }
    }
    /* the animations */
    nAnims = m.count[COUNT_ANIMS];
    if (nAnims != 0 && (m.ptr[24] > size || nAnims > (size - m.ptr[24]) / 0x40)) {
        return fail(why, whyLen, "the animation table runs past the file");
    }
    tableEnd = m.ptr[24] + 0x40 * nAnims;
    for (i = 0; i < nAnims; i++) {
        const uint8_t* e = file + m.ptr[24] + 0x40 * i;
        char name[61];
        uint32_t data = be32(e + 0x3C);
        int st;
        memcpy(name, e, 60);
        name[60] = '\0';
        st = strict != NULL && strict(name, user);
        if (data < tableEnd) {
            /* (a disc test file's entry without data of its own) */
            if (st) {
                return fail(why, whyLen, "%s has no data", name);
            }
            continue;
        }
        if (data >= size || !checkAnim(&m, name, file + data, size - data, st, why, whyLen)) {
            if (data >= size) {
                return fail(why, whyLen, "%s: its data is past the file", name);
            }
            return 0;
        }
    }
    return 1;
}
