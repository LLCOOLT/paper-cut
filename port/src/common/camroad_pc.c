/* Camera road decoding on PC; see port/include/port/camroad_pc.h. */
#include "port/camroad_pc.h"
#include "port/bedecode.h"
#include "cam_road.h"

#include <stdlib.h>
#include <string.h>
#ifdef TTYD_SAVE_STATES
/* the game keeps pointers into these allocations: on the save-state heap */
#define PORT_SNAPHEAP_REDIRECT
#include "port/snapheap.h"
#endif

extern const PortType portBE_camroad_CamRoadMap;

void (*portCamRoadErrorHook)(struct PortDecodeCtx* ctx, const char* msg);

#define CR_HEADER_SIZE 0x10C
#define CR_CURVE_SIZE 0xB8
#define CR_CURVE_NAME 0x20 /* curve name bytes; numeric fields from 0x20 */
#define CR_GROUP_SIZE 0x68
#define CR_GROUP_PAD 0x40  /* group bytes kept as is; read fields at 0x58..0x67 */
#define CR_LIVE_MAX 16

static const uint8_t kMarker[8] = {'P', 'C', 'C', 'A', 'M', 'R', 'D', 0};

typedef struct CamRoadLive {
    void* blob;
    CamRoadMap* map;
} CamRoadLive;

static CamRoadLive sLive[CR_LIVE_MAX];
static uint32_t sLiveNext;

static CamRoadLive* findLive(void* blob) {
    uint32_t i;
    for (i = 0; i < CR_LIVE_MAX; i++) {
        if (sLive[i].blob == blob && sLive[i].map) {
            return &sLive[i];
        }
    }
    return NULL;
}

enum { R_E8, R_CURVES, R_F0, R_F4, R_F8, R_FC, R_100, R_104, R_108, R_COUNT };

static void swapWords(uint8_t* p, uint32_t len) {
    portSwap32Run(p, len / 4);
}

static void swapRecords(uint8_t* p, uint32_t len, uint32_t stride, uint32_t from, uint32_t count) {
    uint32_t i;
    if (len / stride < count) {
        count = len / stride;
    }
    for (i = 0; i < count; i++) {
        swapWords(p + i * stride + from, stride - from);
    }
}

CamRoadMap* portCamRoadDecode(void* blob, uint32_t size, const char* name) {
    uint8_t* b = (uint8_t*)blob;
    PortDecodeCtx ctx;
    CamRoadLive* live;
    CamRoadMap* map;
    uint32_t off[R_COUNT], end[R_COUNT], i, j, curveNum;

    portDecodeBegin(&ctx, blob, size, name ? name : "camroad");
    ctx.onError = portCamRoadErrorHook;
    if (!blob || size < CR_HEADER_SIZE) {
        portDecodeFail(&ctx, "camera road file %p (0x%X bytes) is too small", blob, (unsigned)size);
        return NULL;
    }
    live = findLive(blob);
    if (memcmp(b, kMarker, sizeof(kMarker)) == 0) {
        if (live) {
            return live->map; /* already decoded and swapped */
        }
        portDecodeFail(&ctx, "camera road file was swapped but its native header is gone");
        return NULL;
    }

    for (i = 0; i < R_COUNT; i++) {
        off[i] = portBE32(b + 0xE8 + 4 * i);
        /* tables after f_e8 have a count (curveNum at 0xC8, then f_f0Num...);
         * an empty table's offset word is stale: treat it as absent. */
        if (i > R_E8 && portBE32(b + 0xC8 + 4 * (i - R_CURVES)) == 0) {
            off[i] = 0;
        }
        if (off[i] > size) {
            portDecodeFail(&ctx, "table %u at 0x%X outside the file (0x%X bytes)", (unsigned)i, (unsigned)off[i],
                           (unsigned)size);
            return NULL;
        }
    }
    /* each table runs to the next table (or the end of the file) */
    for (i = 0; i < R_COUNT; i++) {
        end[i] = size;
        for (j = 0; j < R_COUNT; j++) {
            if (off[j] > off[i] && off[j] < end[i]) {
                end[i] = off[j];
            }
        }
        if (off[i] < CR_HEADER_SIZE && off[i] != 0) {
            portDecodeFail(&ctx, "table %u at 0x%X overlaps the header", (unsigned)i, (unsigned)off[i]);
            return NULL;
        }
    }
    curveNum = portBE32(b + 0xC8);

    if (!live) {
        live = &sLive[sLiveNext];
        sLiveNext = (sLiveNext + 1) % CR_LIVE_MAX;
        live->blob = blob;
        if (!live->map) {
            live->map = (CamRoadMap*)malloc(sizeof(CamRoadMap));
        }
    }
    map = live->map;
    if (!map) {
        portDecodeFail(&ctx, "out of memory");
        return NULL;
    }
    if (!portDecodeInto(&ctx, &portBE_camroad_CamRoadMap, 0, map)) {
        portDecodeEnd(&ctx);
        live->blob = NULL;
        return NULL;
    }
    portDecodeEnd(&ctx);

    for (i = 0; i < R_COUNT; i++) {
        uint8_t* p = b + off[i];
        uint32_t len = end[i] - off[i];
        if (off[i] == 0) {
            continue;
        }
        switch (i) {
        case R_CURVES:
            swapRecords(p, len, CR_CURVE_SIZE, CR_CURVE_NAME, curveNum);
            break;
        case R_F0:
        case R_104:
            swapRecords(p, len, CR_GROUP_SIZE, CR_GROUP_PAD, len / CR_GROUP_SIZE);
            break;
        case R_108:
            break; /* not read by the game code */
        default: /* Vec / s32 tables */
            swapWords(p, len);
            break;
        }
    }
    memcpy(b, kMarker, sizeof(kMarker));
    return map;
}
