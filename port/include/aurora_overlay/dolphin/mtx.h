/* Aurora build: Aurora's <dolphin/mtx.h>, plus the 4x4 matrix API the 2004
 * SDK's mtx.h declares. Aurora declares C_MTX44* (mtx44ext.h) without
 * defining most of them; port/src/aurora/mtx44_aurora.c defines the ones the
 * game uses. The game calls the paired-single names directly. */
#include_next <dolphin/mtx.h>

#ifndef PORT_AURORA_OVERLAY_MTX_H
#define PORT_AURORA_OVERLAY_MTX_H
#include <dolphin/mtx/mtx44ext.h>

#define PSMTX44Identity C_MTX44Identity
#define PSMTX44Copy C_MTX44Copy
#define PSMTX44Concat C_MTX44Concat
#define PSMTX44Trans C_MTX44Trans
#define PSMTX44Scale C_MTX44Scale
#define PSMTX44MultVec C_MTX44MultVec
#endif
