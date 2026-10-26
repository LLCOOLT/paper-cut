/* "seqdrv" - Sequence Driver
 * Status: Mostly complete, need function pointers in seq_data and
 * proper names for seqdrv_work
 * 
 * Function: used for swapping between game states (logo, game, battle, demo/e3)
 */
#include "port_hook.h"
#include "driver/seqdrv.h"
#include "system.h"
#include <string.h> //memset prototype
#ifdef PORT_MODS
#include "port/hooks/src_driver_seqdrv.h"
#endif

extern SequenceCallback seq_data[8][3];

//.sdata
SequenceType now_seq = SEQ_UNDEFINED;
SequenceType next_seq = SEQ_UNDEFINED;
SequenceType prev_seq = SEQ_UNDEFINED;

//.sbss
SequenceWork seqWork;
const char* next_p1;
const char* next_p0;

void seqInit_MARIOSTORY(void) {
	memset(&seqWork, 0, sizeof(SequenceWork));
	now_seq = SEQ_UNDEFINED;
	next_seq = SEQ_UNDEFINED;
	prev_seq = SEQ_UNDEFINED;
}

void seqMain(void) {
	if (next_seq != now_seq) {
		if (now_seq != SEQ_UNDEFINED) {
			sysWaitDrawSync();
			seq_data[now_seq][2](&seqWork); //call Exit function
		}
		prev_seq = now_seq; //store previous
		memset(&seqWork, 0, sizeof(SequenceWork));
		now_seq = next_seq;
		seqWork.sequence = next_seq;
		seqWork.state = 0;
		seqWork.mapName = next_p0;
		seqWork.beroName = next_p1;
		seq_data[next_seq][0](&seqWork); //call Init function
	}
	seq_data[now_seq][1](&seqWork); //call Main function
}

void PORT_HOOKABLE(seqSetSeq)(SequenceType seq, const char* map, const char* bero) {
	next_seq = seq;
	next_p0 = map;
	next_p1 = bero;
}

SequenceType seqGetSeq(void) {
	return now_seq;
}

SequenceType seqGetPrevSeq(void) {
	return prev_seq;
}

SequenceType seqGetNextSeq(void) {
	return next_seq;
}

BOOL seqCheckSeq(void) {
	return now_seq != next_seq;
}

#ifdef PORT_MODS
#include "port/hooks/src_driver_seqdrv.inc"
#endif
