/*
 * MUSY_TARGET_PC: a software mixer that executes the DSP command list built by
 * salBuildCommandList (hw_dspctrl.c), replacing the MusyX DSP microcode.
 *
 * The microcode is derived from Nintendo's AX. This file is written from the
 * behaviour MusyX expects of it (what the host code writes into the command list
 * and parameter blocks, and what it reads back), not from any other emulator's
 * source. Summary of the model:
 *
 *  - One command list renders one 5 ms frame: 160 samples at 32 kHz, split into
 *    five 1 ms sub-frames of 32 samples. Parameter-block (PB) updates queued by
 *    the host (_PBUPDATE: per sub-frame count, then (word offset, value) pairs
 *    in the voice's patch buffer) are applied at the start of their sub-frame.
 *  - Nine 32-bit mix buses per studio: main L/R/S, AuxA L/R/S, AuxB L/R/S.
 *    SETUP fills them with the studio's depop ramps (start Hi/Lo, delta per
 *    sample); DL_AND_VOL_MIX adds another studio's previous output; PROCESS mixes
 *    the voices; MIX_AUXA/B hand the aux buses to the host effect callbacks
 *    (triple-buffered, read back two frames later) and add the processed result
 *    to main; UPLOAD_LRS stores the studio output; SET_OPPOSITE_LR seeds the final
 *    mix with the previous frame's surround as L=-S, R=+S (matrix surround);
 *    MIX_AUXB_NOWRITE adds each master studio; COMPRESSOR limits; OUTPUT writes
 *    the surround for the next frame and the clamped stereo s16 result.
 *  - Per voice: source fetch (ADPCM nibble addresses / PCM16 word addresses /
 *    PCM8 byte addresses, with loop and end
 *    handling; sample data is big-endian, stream buffers (top of ARAM, see
 *    aramPcStreamBase) native), sample-rate conversion (16.16 ratio; 4-tap polyphase, linear,
 *    or none), one-pole low-pass, envelope (volume + per-sample delta),
 *    interaural time delay, and per-bus volume ramps. The last contribution to
 *    each bus is written to the PB depop fields for the host's depop fades.
 *
 * SRC coefficients: generated at start-up from a Hann-windowed sinc (4 taps,
 * 64 phases, unity DC gain per phase). The cut-off for the three coefficient
 * sets selected by _PB.coefSelect is our own choice (0.95, 0.80 and 0.62 of
 * the input Nyquist frequency); no table was copied from elsewhere.
 */
#include "musyx/platform.h"

#if MUSY_TARGET == MUSY_TARGET_PC
#include "musyx/dspvoice.h"
#include "musyx/hw_pcmixer.h"
#include "musyx/synth.h"
#include "musyx/voice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u8* portAramPtr(u32 aramAddr);
u32 ARGetSize(void);
u32 aramPcStreamBase(void);

#define FRAME_LEN 160
#define SUB_LEN 32
#define NUM_SUB 5

#ifndef PORT_ENUM_BASE
#define PORT_ENUM_BASE(type)
#endif
enum PORT_ENUM_BASE(int) {
  BUS_L, BUS_R, BUS_S, BUS_AL, BUS_AR, BUS_AS, BUS_BL, BUS_BR, BUS_BS, NUM_BUS
};

/* 5.1 and 7.1 (salPcSetDiscreteRear): the surround (SET_OPPOSITE_LR's) and
 * Pro Logic II's rear pair (MIX_AUXB_LR's), gathered over a frame instead of
 * folded into L/R, then handed over at OUTPUT */
static int sDiscreteRear;
static s32 sRear[3][160], sRearOut[3][160];

void salPcSetDiscreteRear(int on) { sDiscreteRear = on; }

const s32* salPcRear(int which) {
  return sDiscreteRear && which >= 0 && which < 3 ? sRearOut[which] : NULL;
}

/* ------------------------------------------------------------------------- */
/* Pointer handles                                                            */

#define PTR_TABLE_SIZE 8192 /* power of two; handles fit in the low u16 */

static void* sPtrTable[PTR_TABLE_SIZE];
static u32 sPtrCount;

u32 salPcPtrHandle(void* p) {
  size_t h;
  if (p == NULL) {
    return 0;
  }
  h = ((size_t)p >> 3) * 0x9E3779B1u;
  h = (h ^ (h >> 15)) & (PTR_TABLE_SIZE - 1);
  while (sPtrTable[h] != NULL && sPtrTable[h] != p) {
    h = (h + 1) & (PTR_TABLE_SIZE - 1);
  }
  if (sPtrTable[h] == NULL) {
    if (sPtrCount >= PTR_TABLE_SIZE - 1) {
      fprintf(stderr, "musyx pc mixer: pointer handle table full\n");
      abort();
    }
    sPtrTable[h] = p;
    sPtrCount++;
  }
  return (u32)h + 1;
}

void* salPcPtrResolve(u32 handle) {
  if (handle == 0 || handle > PTR_TABLE_SIZE) {
    return NULL;
  }
  return sPtrTable[handle - 1];
}

static void* resolve2(u16 hi, u16 lo) { return salPcPtrResolve(((u32)hi << 16) | lo); }

/* ------------------------------------------------------------------------- */
/* SRC coefficient tables                                                     */

#define SRC_PHASES 64
static s16 sSrcCoef[3][SRC_PHASES][4];
static int sSrcReady;

static void initSrcTables(void) {
  static const double cutoff[3] = {0.95, 0.80, 0.62};
  int set, ph, k;
  for (set = 0; set < 3; set++) {
    for (ph = 0; ph < SRC_PHASES; ph++) {
      double t = (double)ph / SRC_PHASES;
      double c[4], sum = 0.0;
      int isum = 0;
      for (k = 0; k < 4; k++) {
        /* tap k sits at distance x from the interpolation point (taps 1 and 2
         * bracket it). */
        double x = t - (k - 1);
        double a = 3.14159265358979323846 * cutoff[set] * x;
        double sinc = fabs(x) < 1e-9 ? 1.0 : sin(a) / a;
        double w = fabs(x) >= 2.0 ? 0.0 : 0.5 * (1.0 + cos(3.14159265358979323846 * x / 2.0));
        c[k] = sinc * w;
        sum += c[k];
      }
      for (k = 0; k < 4; k++) {
        sSrcCoef[set][ph][k] = (s16)lrint(c[k] / sum * 32767.0);
        isum += sSrcCoef[set][ph][k];
      }
      /* keep unity gain exactly after rounding */
      sSrcCoef[set][ph][1] += (s16)(32767 - isum);
    }
  }
  sSrcReady = 1;
}

/* ------------------------------------------------------------------------- */
/* Source fetch                                                               */

static inline s16 clamp16(s32 v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (s16)v); }

static inline u8 aramByte(u32 addr) {
  static u8* base;
  static u32 size;
  if (base == NULL) {
    base = portAramPtr(0);
    size = ARGetSize();
  }
  return addr < size ? base[addr] : 0;
}

static s16 fetch1(_PB* pb) {
  u32 cur = ((u32)pb->addr.currentAddressHi << 16) | pb->addr.currentAddressLo;
  u32 end = ((u32)pb->addr.endAddressHi << 16) | pb->addr.endAddressLo;
  s16 s;

  switch (pb->addr.format) {
  case 0: { /* 4-bit ADPCM, nibble addresses, 8-byte frames (header + 14 samples) */
    s32 nib, idx, val;
    if ((cur & 15) == 0) {
      pb->adpcm.pred_scale = aramByte(cur >> 1);
      cur += 2;
    }
    nib = aramByte(cur >> 1);
    nib = (cur & 1) ? (nib & 15) : (nib >> 4);
    if (nib >= 8) {
      nib -= 16;
    }
    idx = (pb->adpcm.pred_scale >> 4) & 7;
    val = (nib << (pb->adpcm.pred_scale & 15)) << 11;
    val += (s16)pb->adpcm.a[idx][0] * (s16)pb->adpcm.yn1 +
           (s16)pb->adpcm.a[idx][1] * (s16)pb->adpcm.yn2;
    s = clamp16((val + 1024) >> 11);
    pb->adpcm.yn2 = pb->adpcm.yn1;
    pb->adpcm.yn1 = (u16)s;
    break;
  }
  case 0xA: /* PCM16, sample addresses */
    if (cur * 2 >= aramPcStreamBase()) {
      /* stream buffers: uploaded from native host s16 */
      u16 v = (u16)(aramByte(cur * 2) | (aramByte(cur * 2 + 1) << 8));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      v = (u16)((v >> 8) | (v << 8));
#endif
      s = (s16)v;
    } else {
      /* sample data from .samp files: big-endian, as on the GameCube */
      s = (s16)((aramByte(cur * 2) << 8) | aramByte(cur * 2 + 1));
    }
    break;
  case 0x19: /* PCM8, byte addresses */
    s = (s16)((s8)aramByte(cur) << 8);
    break;
  default:
    s = 0;
    break;
  }

  if (cur == end) {
    cur = ((u32)pb->addr.loopAddressHi << 16) | pb->addr.loopAddressLo;
    if (pb->addr.loopFlag) {
      if (pb->addr.format == 0) {
        pb->adpcm.pred_scale = pb->adpcmLoop.loop_pred_scale;
        if (pb->loopType == 0) {
          pb->adpcm.yn1 = pb->adpcmLoop.loop_yn1;
          pb->adpcm.yn2 = pb->adpcmLoop.loop_yn2;
        }
      }
      pb->streamLoopCnt++;
    } else {
      /* one-shot sample finished */
      pb->state = 0;
    }
  } else {
    cur++;
  }
  pb->addr.currentAddressHi = cur >> 16;
  pb->addr.currentAddressLo = (u16)cur;
  return s;
}

void salPcPbFetch(_PB* pb, s16* out, u32 n) {
  u32 i;
  for (i = 0; i < n; i++) {
    out[i] = pb->state != 0 ? fetch1(pb) : 0;
  }
}

/* Next source sample; silence once a one-shot voice has ended. */
static inline s16 nextSample(_PB* pb) { return pb->state != 0 ? fetch1(pb) : 0; }

static void resample(_PB* pb, s16* out, u32 n) {
  u32 ratio = ((u32)pb->src.ratioHi << 16) | pb->src.ratioLo;
  u32 frac = pb->src.currentAddressFrac;
  s16 h0 = (s16)pb->src.last_samples[0];
  s16 h1 = (s16)pb->src.last_samples[1];
  s16 h2 = (s16)pb->src.last_samples[2];
  s16 h3 = (s16)pb->src.last_samples[3];
  u32 i;
  u32 sel = pb->srcSelect;
  const s16(*coef)[4] = sSrcCoef[pb->coefSelect < 3 ? pb->coefSelect : 0];

  for (i = 0; i < n; i++) {
    if (sel >= 2) {
      /* no conversion: one source sample per output sample */
      h0 = h1;
      h1 = h2;
      h2 = h3;
      h3 = nextSample(pb);
      out[i] = h3;
      continue;
    }
    frac += ratio;
    while (frac >= 0x10000) {
      frac -= 0x10000;
      h0 = h1;
      h1 = h2;
      h2 = h3;
      h3 = nextSample(pb);
    }
    if (sel == 1) {
      out[i] = (s16)(h1 + (((s32)(h2 - h1) * (s32)frac) >> 16));
    } else {
      const s16* c = coef[frac >> 10];
      s32 acc = c[0] * h0 + c[1] * h1 + c[2] * h2 + c[3] * h3;
      out[i] = clamp16((acc + 0x4000) >> 15);
    }
  }
  pb->src.currentAddressFrac = (u16)frac;
  pb->src.last_samples[0] = (u16)h0;
  pb->src.last_samples[1] = (u16)h1;
  pb->src.last_samples[2] = (u16)h2;
  pb->src.last_samples[3] = (u16)h3;
}

/* ------------------------------------------------------------------------- */
/* Voice processing                                                           */

typedef struct MixChannel {
  u16 mask;  /* mixerCtrl bit */
  u8 vol;    /* word index of the volume in _PBMIX (delta follows it) */
  u8 bus;
  u8 itd;    /* 0 none, 1 left, 2 right */
  u8 dpop;   /* word index in _PBDPOP */
} MixChannel;

static const MixChannel sChannels[9] = {
    {0x0001, 0, BUS_L, 1, 0},   {0x0002, 2, BUS_R, 2, 3},   {0x0004, 14, BUS_S, 0, 6},
    {0x0010, 4, BUS_AL, 1, 1},  {0x0020, 6, BUS_AR, 2, 4},  {0x0080, 16, BUS_AS, 0, 7},
    {0x0200, 8, BUS_BL, 1, 2},  {0x0400, 10, BUS_BR, 2, 5}, {0x1000, 12, BUS_BS, 0, 8},
};

static void applyUpdates(_PB* pb, const u16* data, u32* pos, u32 count) {
  u16* words = (u16*)pb;
  u32 i;
  if (data == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    u16 off = data[*pos];
    u16 val = data[*pos + 1];
    *pos += 2;
    if (off < sizeof(_PB) / sizeof(u16)) {
      words[off] = val;
    }
  }
}

static void mixSub(_PB* pb, const s16* smp, s32 (*bus)[FRAME_LEN], u32 at) {
  u16* mixw = (u16*)&pb->mix;
  u16* dpopw = (u16*)&pb->dpop;
  s16 left[SUB_LEN], right[SUB_LEN];
  const s16* src;
  u32 c, i;

  if (pb->itd.flag) {
    /* Interaural time delay: the left/right sides read the voice delayed by
     * shiftL/shiftR samples. The previous sub-frame is kept in the ITD buffer. */
    s16* hist = salPcPtrResolve(((u32)pb->itd.bufferHi << 16) | pb->itd.bufferLo);
    s16 ext[2 * SUB_LEN];
    u32 sl, sr;
    if (pb->itd.shiftL != pb->itd.targetShiftL) {
      pb->itd.shiftL += pb->itd.shiftL < pb->itd.targetShiftL ? 1 : -1;
    }
    if (pb->itd.shiftR != pb->itd.targetShiftR) {
      pb->itd.shiftR += pb->itd.shiftR < pb->itd.targetShiftR ? 1 : -1;
    }
    sl = pb->itd.shiftL < SUB_LEN ? pb->itd.shiftL : SUB_LEN - 1;
    sr = pb->itd.shiftR < SUB_LEN ? pb->itd.shiftR : SUB_LEN - 1;
    if (hist != NULL) {
      memcpy(ext, hist, SUB_LEN * sizeof(s16));
      memcpy(hist, smp, SUB_LEN * sizeof(s16));
    } else {
      memset(ext, 0, SUB_LEN * sizeof(s16));
    }
    memcpy(ext + SUB_LEN, smp, SUB_LEN * sizeof(s16));
    for (i = 0; i < SUB_LEN; i++) {
      left[i] = ext[SUB_LEN + i - sl];
      right[i] = ext[SUB_LEN + i - sr];
    }
  }

  for (c = 0; c < 9; c++) {
    const MixChannel* ch = &sChannels[c];
    s32 vol, delta, last = 0;
    s32* out;
    if (!(pb->mixerCtrl & ch->mask)) {
      continue;
    }
    vol = mixw[ch->vol];
    delta = (s16)mixw[ch->vol + 1];
    src = smp;
    if (pb->itd.flag && ch->itd != 0) {
      src = ch->itd == 1 ? left : right;
    }
    out = bus[ch->bus] + at;
    for (i = 0; i < SUB_LEN; i++) {
      last = (src[i] * vol) >> 15;
      out[i] += last;
      vol += delta;
      vol = vol < 0 ? 0 : (vol > 0xFFFF ? 0xFFFF : vol);
    }
    mixw[ch->vol] = (u16)vol;
    dpopw[ch->dpop] = (u16)clamp16(last);
  }
}

/* --- Mix categories (hw_pcmixer.h) --- */
u8 salPcVoiceCat[SYNTH_MAX_VOICES];
u8 salPcSeqCat[256];
u8 salPcStreamCat[64];
static int sCategory = SAL_PC_SFX;
/* 1.15; SAL_PC_SILENT stays 0. The sliders' gains, not game state: out of save states and rewind
   (audio_mix_pc.c pushes a slider only when it moves, so a restored copy would stick) */
static s32 sCatGain[SAL_PC_CATEGORIES] PORT_HOST_DATA = {32768, 32768, 32768, 0};

int salPcCategory(int cat) {
  int prev = sCategory;
  if (cat >= 0 && cat < SAL_PC_CATEGORIES) {
    sCategory = cat;
  }
  return prev;
}

void salPcSetCategoryGain(int cat, float gain) {
  if (cat >= 0 && cat < SAL_PC_SILENT) {
    gain = gain < 0.f ? 0.f : (gain > 1.f ? 1.f : gain);
    sCatGain[cat] = (s32)(gain * 32768.f + 0.5f);
  }
}

/* the gain for a parameter block's voice (1.15) */
static s32 pbGain(const _PB* pb) {
  u32 v;
  for (v = 0; v < synthInfo.voiceNum && v < SYNTH_MAX_VOICES; v++) {
    if (dspVoice[v].pb == pb) {
      u8 cat = salPcVoiceCat[v];
      return cat < SAL_PC_CATEGORIES ? sCatGain[cat] : 32768;
    }
  }
  return 32768;
}

/* --- The null mix (salPcSetNullMix: headless test runs, nothing heard) ---
 * Everything the CPU side reads back from a parameter block, as the full mix
 * leaves it: the updates applied, the addresses stepped (loops, a one-shot's
 * end), the resampler's position, the envelope's and the mix volumes' ramps;
 * no sample is decoded, filtered or mixed (the buses stay silent, the
 * de-pop values 0), so MusyX's voices, streams and the game's waits on
 * sounds go exactly as with sound. */
static int sNullMix;

void salPcSetNullMix(int on) { sNullMix = on; }

/* fetch1's addressing without its sample */
static void fetchNull(_PB* pb) {
  u32 cur = ((u32)pb->addr.currentAddressHi << 16) | pb->addr.currentAddressLo;
  u32 end = ((u32)pb->addr.endAddressHi << 16) | pb->addr.endAddressLo;
  if (pb->addr.format == 0 && (cur & 15) == 0) {
    cur += 2; /* (an ADPCM frame's header) */
  }
  if (cur == end) {
    cur = ((u32)pb->addr.loopAddressHi << 16) | pb->addr.loopAddressLo;
    if (pb->addr.loopFlag) {
      pb->streamLoopCnt++;
    } else {
      pb->state = 0;
    }
  } else {
    cur++;
  }
  pb->addr.currentAddressHi = cur >> 16;
  pb->addr.currentAddressLo = (u16)cur;
}

static void processPbNull(_PB* pb) {
  const u16* upd = resolve2(pb->update.dataHi, pb->update.dataLo);
  u16* mixw = (u16*)&pb->mix;
  u16* dpopw = (u16*)&pb->dpop;
  u32 pos = 0, sf, i, c;
  for (sf = 0; sf < NUM_SUB; sf++) {
    applyUpdates(pb, upd, &pos, pb->update.updNum[sf]);
    if (pb->state == 0) {
      continue;
    }
    {
      const u32 ratio = ((u32)pb->src.ratioHi << 16) | pb->src.ratioLo;
      u32 frac = pb->src.currentAddressFrac;
      for (i = 0; i < SUB_LEN; i++) {
        if (pb->srcSelect >= 2) {
          if (pb->state != 0) {
            fetchNull(pb);
          }
          continue;
        }
        frac += ratio;
        while (frac >= 0x10000) {
          frac -= 0x10000;
          if (pb->state != 0) {
            fetchNull(pb);
          }
        }
      }
      pb->src.currentAddressFrac = (u16)frac;
    }
    {
      s32 vol = pb->ve.currentVolume;
      const s32 delta = (s16)pb->ve.currentDelta;
      for (i = 0; i < SUB_LEN; i++) {
        vol += delta;
        vol = vol < 0 ? 0 : (vol > 0x7FFF ? 0x7FFF : vol);
      }
      pb->ve.currentVolume = (u16)vol;
    }
    if (pb->itd.flag) {
      if (pb->itd.shiftL != pb->itd.targetShiftL) {
        pb->itd.shiftL += pb->itd.shiftL < pb->itd.targetShiftL ? 1 : -1;
      }
      if (pb->itd.shiftR != pb->itd.targetShiftR) {
        pb->itd.shiftR += pb->itd.shiftR < pb->itd.targetShiftR ? 1 : -1;
      }
    }
    for (c = 0; c < 9; c++) {
      const MixChannel* ch = &sChannels[c];
      s32 vol;
      const s32 delta = (s16)mixw[ch->vol + 1];
      if (!(pb->mixerCtrl & ch->mask)) {
        continue;
      }
      vol = mixw[ch->vol];
      if (delta != 0) {
        for (i = 0; i < SUB_LEN; i++) {
          vol += delta;
          vol = vol < 0 ? 0 : (vol > 0xFFFF ? 0xFFFF : vol);
        }
      }
      mixw[ch->vol] = (u16)vol;
      dpopw[ch->dpop] = 0;
    }
  }
}

void salPcProcessPb(_PB* pb, s32 (*bus)[FRAME_LEN]) {
  const u16* upd;
  u32 pos = 0;
  u32 sf, i;
  s16 smp[SUB_LEN];
  s32 gain;

  if (sNullMix) {
    processPbNull(pb);
    return;
  }
  if (!sSrcReady) {
    initSrcTables();
  }
  gain = pbGain(pb);
  upd = resolve2(pb->update.dataHi, pb->update.dataLo);
  for (sf = 0; sf < NUM_SUB; sf++) {
    applyUpdates(pb, upd, &pos, pb->update.updNum[sf]);
    if (pb->state == 0) {
      continue;
    }
    resample(pb, smp, SUB_LEN);

    if (pb->lpf.on) {
      s32 y = (s16)pb->lpf.yn1;
      for (i = 0; i < SUB_LEN; i++) {
        y = clamp16(((s32)pb->lpf.a0 * smp[i] + (s32)pb->lpf.b0 * y) >> 15);
        smp[i] = (s16)y;
      }
      pb->lpf.yn1 = (u16)y;
    }

    {
      s32 vol = pb->ve.currentVolume;
      s32 delta = (s16)pb->ve.currentDelta;
      for (i = 0; i < SUB_LEN; i++) {
        smp[i] = (s16)((smp[i] * vol) >> 15);
        vol += delta;
        vol = vol < 0 ? 0 : (vol > 0x7FFF ? 0x7FFF : vol);
      }
      pb->ve.currentVolume = (u16)vol;
    }
    if (gain != 32768) { /* full volume leaves the mix as it was */
      for (i = 0; i < SUB_LEN; i++) {
        smp[i] = (s16)((smp[i] * gain) >> 15);
      }
    }

    mixSub(pb, smp, bus, sf * SUB_LEN);
  }
}

/* ------------------------------------------------------------------------- */
/* Command list                                                               */

static s32 sBus[NUM_BUS][FRAME_LEN];
static s32 sCompGain = 32768; /* compressor gain, 1.15 */

static void cmdSetup(const u16* spb) {
  u32 b, i;
  for (b = 0; b < NUM_BUS; b++) {
    s32 v = (s32)(((u32)spb[b * 3] << 16) | spb[b * 3 + 1]);
    s32 d = (s16)spb[b * 3 + 2];
    for (i = 0; i < FRAME_LEN; i++) {
      sBus[b][i] = v;
      v += d;
    }
  }
}

static void addScaled(s32* dst, const s32* src, u16 vol) {
  u32 i;
  if (vol == 0) {
    return;
  }
  for (i = 0; i < FRAME_LEN; i++) {
    dst[i] += (s32)(((long long)src[i] * vol) >> 15);
  }
}

static void cmdDownloadMix(const s32* in, u16 vol, u16 volA, u16 volB) {
  u32 c;
  if (in == NULL) {
    return;
  }
  for (c = 0; c < 3; c++) {
    addScaled(sBus[BUS_L + c], in + c * FRAME_LEN, vol);
    addScaled(sBus[BUS_AL + c], in + c * FRAME_LEN, volA);
    addScaled(sBus[BUS_BL + c], in + c * FRAME_LEN, volB);
  }
}

static void addBuses(u32 firstBus, const s32* in, u32 count) {
  u32 c, i;
  for (c = 0; c < count; c++) {
    for (i = 0; i < FRAME_LEN; i++) {
      sBus[firstBus + c][i] += in[c * FRAME_LEN + i];
    }
  }
}

static void cmdMixAux(u32 auxBus, s32* write, const s32* read) {
  if (write != NULL) {
    memcpy(write, sBus[auxBus], 3 * FRAME_LEN * sizeof(s32));
  }
  if (read != NULL) {
    addBuses(BUS_L, read, 3);
  }
}

static void cmdCompressor(u32 threshold) {
  s32 peak = 0, target, g0, g1;
  u32 i;
  if (threshold == 0 || threshold > 32767) {
    threshold = 32767;
  }
  for (i = 0; i < FRAME_LEN; i++) {
    s32 l = sBus[BUS_L][i] < 0 ? -sBus[BUS_L][i] : sBus[BUS_L][i];
    s32 r = sBus[BUS_R][i] < 0 ? -sBus[BUS_R][i] : sBus[BUS_R][i];
    peak = l > peak ? l : peak;
    peak = r > peak ? r : peak;
  }
  target = peak > (s32)threshold ? (s32)(((long long)threshold << 15) / peak) : 32768;
  g0 = sCompGain;
  /* fast attack, release over roughly ten frames */
  g1 = target < g0 ? target : g0 + (target - g0) / 10 + (target > g0);
  for (i = 0; i < FRAME_LEN; i++) {
    s32 g = g0 + (s32)(((long long)(g1 - g0) * (s32)(i + 1)) / FRAME_LEN);
    sBus[BUS_L][i] = (s32)(((long long)sBus[BUS_L][i] * g) >> 15);
    sBus[BUS_R][i] = (s32)(((long long)sBus[BUS_R][i] * g) >> 15);
  }
  sCompGain = g1;
}

void salPcRunCommandList(const u16* cmd) {
  _PB* first = NULL;
  u32 guard = 0;
  u32 i;

  if (!sSrcReady) {
    initSrcTables();
  }
  while (cmd != NULL && guard++ < 4096) {
    if (sNullMix && cmd[0] < 19) {
      /* the null mix: the buses' commands skipped (their length in words; 0: run it) */
      static const u8 kSkip[19] = {3, 6, 0, 0, 5, 5, 3, 0, 0, 3, 0, 0, 0, 0, 5, 0, 5, 3, 5};
      if (kSkip[cmd[0]] != 0) {
        cmd += kSkip[cmd[0]];
        continue;
      }
    }
    switch (cmd[0]) {
    case 0: /* SETUP */
    {
      const u16* spb = resolve2(cmd[1], cmd[2]);
      if (spb != NULL) {
        cmdSetup(spb);
      } else {
        memset(sBus, 0, sizeof(sBus));
      }
      cmd += 3;
      break;
    }
    case 1: /* DL_AND_VOL_MIX */
      cmdDownloadMix(resolve2(cmd[1], cmd[2]), cmd[3], cmd[4], cmd[5]);
      cmd += 6;
      break;
    case 2: /* PB_ADDR */
      first = resolve2(cmd[1], cmd[2]);
      cmd += 3;
      break;
    case 3: /* PROCESS */
    {
      _PB* pb = first;
      if (sNullMix) {
        u32 m = 0;
        while (pb != NULL && m++ < 256) {
          processPbNull(pb);
          pb = resolve2(pb->nextHi, pb->nextLo);
        }
        cmd += 1;
        break;
      }
      u32 n = 0;
      while (pb != NULL && n++ < 256) {
        salPcProcessPb(pb, sBus);
        pb = resolve2(pb->nextHi, pb->nextLo);
      }
      cmd += 1;
      break;
    }
    case 4: /* MIX_AUXA */
      cmdMixAux(BUS_AL, resolve2(cmd[1], cmd[2]), resolve2(cmd[3], cmd[4]));
      cmd += 5;
      break;
    case 5: /* MIX_AUXB */
      cmdMixAux(BUS_BL, resolve2(cmd[1], cmd[2]), resolve2(cmd[3], cmd[4]));
      cmd += 5;
      break;
    case 6: /* UPLOAD_LRS */
    {
      s32* dst = resolve2(cmd[1], cmd[2]);
      if (dst != NULL) {
        memcpy(dst, sBus[BUS_L], 3 * FRAME_LEN * sizeof(s32));
      }
      cmd += 3;
      break;
    }
    case 9: /* MIX_AUXB_NOWRITE: add a studio's L/R/S output to the final mix */
    {
      const s32* in = resolve2(cmd[1], cmd[2]);
      if (in != NULL) {
        addBuses(BUS_L, in, 3);
      }
      cmd += 3;
      break;
    }
    case 13: /* MORE */
      cmd = resolve2(cmd[1], cmd[2]);
      break;
    case 14: /* OUTPUT */
    {
      s32* surround = resolve2(cmd[1], cmd[2]);
      s16* dest = resolve2(cmd[3], cmd[4]);
      if (surround != NULL) {
        memcpy(surround, sBus[BUS_S], FRAME_LEN * sizeof(s32));
      }
      if (sDiscreteRear) { /* this frame's rear, for the speakers (hw_pc.c) */
        memcpy(sRearOut, sRear, sizeof(sRearOut));
        memset(sRear, 0, sizeof(sRear));
      }
      if (dest != NULL) {
        for (i = 0; i < FRAME_LEN; i++) {
          dest[2 * i] = clamp16(sBus[BUS_L][i]);
          dest[2 * i + 1] = clamp16(sBus[BUS_R][i]);
        }
      }
      cmd += 5;
      break;
    }
    case 15: /* END */
      return;
    case 16: /* MIX_AUXB_LR (Dolby Pro Logic II studio): AuxB L/R are the rear
                channels; store them and fold the previous frame's into L/R. */
    {
      s32* write = resolve2(cmd[1], cmd[2]);
      const s32* read = resolve2(cmd[3], cmd[4]);
      if (write != NULL) {
        memcpy(write, sBus[BUS_BL], 2 * FRAME_LEN * sizeof(s32));
      }
      if (read != NULL && sDiscreteRear) { /* the rear speakers have them */
        for (i = 0; i < FRAME_LEN; i++) {
          sRear[1][i] += read[i];
          sRear[2][i] += read[FRAME_LEN + i];
        }
      } else if (read != NULL) {
        addBuses(BUS_L, read, 2);
      }
      cmd += 5;
      break;
    }
    case 17: /* SET_OPPOSITE_LR */
    {
      const s32* s = resolve2(cmd[1], cmd[2]);
      for (i = 0; i < FRAME_LEN; i++) {
        s32 v = s != NULL ? s[i] : 0;
        if (sDiscreteRear) { /* the surround to its own speakers, not matrixed into L/R */
          sRear[0][i] += v;
          v = 0;
        }
        sBus[BUS_L][i] = -v;
        sBus[BUS_R][i] = v;
        sBus[BUS_S][i] = 0;
      }
      cmd += 3;
      break;
    }
    case 18: /* COMPRESSOR */
      cmdCompressor(cmd[1]);
      cmd += 5;
      break;
    default:
      fprintf(stderr, "musyx pc mixer: unknown DSP command %u\n", cmd[0]);
      return;
    }
  }
}

#endif
