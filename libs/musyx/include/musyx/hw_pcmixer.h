#ifndef _MUSYX_HW_PCMIXER
#define _MUSYX_HW_PCMIXER
/*
 * MUSY_TARGET_PC only: software replacement for the MusyX DSP microcode.
 * salBuildCommandList (hw_dspctrl.c) builds the same command list as on the
 * GameCube; salPcRunCommandList executes it on the CPU. Addresses in the list
 * and in parameter blocks are 32-bit handles from salPcPtrHandle, because host
 * pointers do not fit in the Hi/Lo u16 pairs on 64-bit hosts.
 */
#include "musyx/musyx.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _PB;

/* Interns a host pointer as a non-zero 32-bit handle (NULL -> 0). */
u32 salPcPtrHandle(void* p);
/* Inverse of salPcPtrHandle (0 -> NULL). */
void* salPcPtrResolve(u32 handle);

/* Runs one 5 ms (160 sample) DSP command list. */
void salPcRunCommandList(const u16* cmdList);

/* Pulls `n` raw source samples (before SRC) from a parameter block's ARAM
 * stream: ADPCM/PCM16/PCM8 decoding with loop and end handling. For tests. */
void salPcPbFetch(struct _PB* pb, s16* out, u32 n);

/* Processes one parameter block for 160 output samples into the given 9
 * bus buffers (L, R, S, AuxA L/R/S, AuxB L/R/S). For tests. */
void salPcProcessPb(struct _PB* pb, s32 (*bus)[160]);

/* Host output: renders `frames` interleaved stereo s16 frames at 32 kHz by
 * running MusyX in 160-frame quanta (hw_pc.c). */
void salPcRender(s16* out, u32 frames);

/* Port hook: extra stereo frames (e.g. the DVD stream) mixed after MusyX. */
void portAudioMixExtra(s16* out, u32 frames);

/* 5.1 and 7.1 (the Speakers setting, hw_pc.c): the surround bus and Pro Logic
 * II's rear pair kept apart, not folded into the front pair, and the last
 * frame's handed over: which 0 the surround, 1 and 2 the rear left and right
 * (160 samples, NULL when off). */
void salPcSetDiscreteRear(int on);
const s32* salPcRear(int which);
/* The speakers the output has: 2, 6 (5.1) or 8 (7.1); MusyX's stereo is its
 * surround above 2 (snd_synthapi.c), so the rear speakers have something. */
int salPcSpeakers(void);

/* The mix categories (the settings page's music, effects and ambience
 * volumes): each voice takes the category current when its sound started
 * (a sequence's voices, the sequence's), and is scaled by its category's
 * gain before it is mixed. */
enum { SAL_PC_MUSIC, SAL_PC_SFX, SAL_PC_AMBIENCE, SAL_PC_SILENT, SAL_PC_CATEGORIES };
/* Sets the current category (cat >= 0) and returns the one before. */
int salPcCategory(int cat);
void salPcSetCategoryGain(int cat, float gain);
extern u8 salPcVoiceCat[SYNTH_MAX_VOICES];
extern u8 salPcSeqCat[256];    /* by sequence instance */
extern u8 salPcStreamCat[64];  /* by stream slot */
/* Port hook, before each quantum: sets the categories' gains (salPcSetCategoryGain). */
void portAudioMixUpdate(void);

#ifdef __cplusplus
}
#endif
#endif
