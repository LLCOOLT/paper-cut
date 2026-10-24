/* Aurora build: Aurora's <dolphin/dvd.h>, plus the 2004 SDK's DVDRead macro. */
#include_next <dolphin/dvd.h>

#ifndef DVDRead
#define DVDRead(fileInfo, addr, length, offset) DVDReadPrio((fileInfo), (addr), (length), (offset), 2)
#endif
