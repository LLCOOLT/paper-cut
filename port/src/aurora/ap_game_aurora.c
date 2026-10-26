/* The game side of the port's Archipelago support (port/archipelago/ttyd_ap.h):
 * the game's flags and the few tables the seed changes. */
#include "../../archipelago/ttyd_ap.h"
#include "data/item_data.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "mario/mariost.h"

#include <dolphin/dvd.h>
#include <stdlib.h>

extern ItemData itemDataTable[]; /* src/data/item_data.c */

int portApGswfGet(int flag) { return swGet(flag) ? 1 : 0; }

void portApGswfSet(int flag) { swSet(flag); }

int portApGswGet(int index) { return swByteGet(index); }

void portApGswSet(int index, int value) { swByteSet(index, value); }

uint8_t* portApGswBytes(void) { return gp->mGSW; }

const char* portApMapName(void) { return gp->currentMapName; }

int portApInFile(void) { return seqGetSeq() == SEQ_GAME || seqGetSeq() == SEQ_BATTLE; }

int portApSellPrice(int item) { return itemDataTable[item].sell_price; }

void portApSetSellPrice(int item, int price) { itemDataTable[item].sell_price = (u16)price; }

void* portApReadDiscFile(const char* path, int* size) {
    DVDFileInfo info;
    void* data;
    if (!DVDOpen(path, &info)) {
        return NULL;
    }
    *size = (int)info.length;
    data = malloc((size_t)((*size + 31) & ~31));
    if (data != NULL && DVDReadPrio(&info, data, (*size + 31) & ~31, 0, 2) < 0) {
        free(data);
        data = NULL;
    }
    DVDClose(&info);
    return data;
}
