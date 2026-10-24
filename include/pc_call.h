#pragma once

/* PCCALL(gc_call, pc_call): the decomp sometimes calls a function through a
 * pointer of another type to get MWCC's register use. On GameCube that is the
 * call as written; on the PC port, where such calls pass arguments in the wrong
 * registers (or truncate pointers), the same call is made with the function's
 * real prototype (tools/port/fncast_fix.py writes both). */
#ifdef TARGET_PC
#include <stdint.h>
#define PCCALL(gc_call, pc_call) (pc_call)
#else
#define PCCALL(gc_call, pc_call) (gc_call)
#endif

/* PORT_SPIN(): the body of an empty loop that waits for an interrupt to
 * change memory (an ARQ callback setting a flag). Nothing on GameCube; on PC,
 * where callbacks run from a poll, it runs them (and the call makes the
 * compiler re-read the flag). */
#ifdef TARGET_PC
void portARQPoll(void);
#define PORT_SPIN() portARQPoll()
#else
#define PORT_SPIN()
#endif
