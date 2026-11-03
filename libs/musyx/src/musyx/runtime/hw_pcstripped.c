/*
 * MUSY_TARGET_PC only: MusyX functions that TTYD's linker stripped from the DOL
 * (so the decomp has no source for them) but that other MusyX objects still
 * reference. The PC build links every MusyX object, so they are provided here,
 * written from what their callers expect.
 */
#include "musyx/platform.h"

#if MUSY_TARGET == MUSY_TARGET_PC
#include "musyx/dspvoice.h"
#include "musyx/hardware.h"
#include "musyx/sal.h"

extern s16* dspHrtfHistoryBuffer;
extern s32* dspSurround;
extern s16* dspITDBuffer;

static u16 inputVol(u8 v) { return v >= 127 ? 0x7FFF : (u16)((v * 0x7FFF) / 127); }

void salDeactivateStudio(u8 studio) { dspStudio[studio].state = 0; }

bool salAddStudioInput(DSPstudioinfo* stp, SND_STUDIO_INPUT* desc) {
  DSPinput* in;
  if (stp->numInputs >= 7) {
    return FALSE;
  }
  in = &stp->in[stp->numInputs++];
  in->studio = desc->srcStudio;
  in->vol = inputVol(desc->vol);
  in->volA = inputVol(desc->volA);
  in->volB = inputVol(desc->volB);
  in->desc = desc;
  return TRUE;
}

bool salRemoveStudioInput(DSPstudioinfo* stp, SND_STUDIO_INPUT* desc) {
  u32 i;
  for (i = 0; i < stp->numInputs; ++i) {
    if (stp->in[i].desc == desc) {
      for (; i + 1 < stp->numInputs; ++i) {
        stp->in[i] = stp->in[i + 1];
      }
      --stp->numInputs;
      return TRUE;
    }
  }
  return FALSE;
}

/* Moves a voice to another studio. A voice already mixing in the old studio is
 * queued on its alien list so the old studio fades out its last output (depop). */
void salReconnectVoice(DSPvoice* dsp_vptr, u8 studio) {
  if (dsp_vptr->state != 0) {
    if (dsp_vptr->prev != NULL) {
      dsp_vptr->prev->next = dsp_vptr->next;
    } else {
      dspStudio[dsp_vptr->studio].voiceRoot = dsp_vptr->next;
    }
    if (dsp_vptr->next != NULL) {
      dsp_vptr->next->prev = dsp_vptr->prev;
    }
    if (dsp_vptr->state == 2) {
      dsp_vptr->nextAlien = dspStudio[dsp_vptr->studio].alienVoiceRoot;
      dspStudio[dsp_vptr->studio].alienVoiceRoot = dsp_vptr;
    }
    if ((dsp_vptr->next = dspStudio[studio].voiceRoot) != NULL) {
      dsp_vptr->next->prev = dsp_vptr;
    }
    dsp_vptr->prev = NULL;
    dspStudio[studio].voiceRoot = dsp_vptr;
  }
  dsp_vptr->studio = studio;
}

bool salExitDspCtrl() {
  u32 i;
  for (i = 0; i < salMaxStudioNum; ++i) {
    salFree(dspStudio[i].spb);
    salFree(dspStudio[i].main[0]);
    dspStudio[i].state = 0;
  }
  for (i = 0; i < salNumVoices; ++i) {
    salFree(dspVoice[i].pb);
    salFree(dspVoice[i].patchData);
  }
  salFree(dspHrtfHistoryBuffer);
  salFree(dspITDBuffer);
  salFree(dspVoice);
  salFree(dspSurround);
  salFree(dspCmdList);
  dspVoice = NULL;
  return TRUE;
}

u32 hwGetStreamARAMAddr(u8 hwStreamHandle) {
  return (u32)aramGetStreamBufferAddress(hwStreamHandle, NULL);
}

void s3dExit() {}
#endif
