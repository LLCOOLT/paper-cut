/* Aurora build: Aurora's <dolphin/types.h>, plus the C library headers the
 * 2004 SDK's types.h includes on PC (the game uses va_start, memcpy, ...
 * having included only SDK headers). */
#include_next <dolphin/types.h>

#ifndef PORT_AURORA_OVERLAY_TYPES_H
#define PORT_AURORA_OVERLAY_TYPES_H
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#endif
