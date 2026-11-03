/* MusyX PC hardware layer tests (no disc, no audio device):
 *  1. ADPCM: the mixer's sample fetch vs a straightforward reference decoder,
 *     including the loop (history and predictor reload).
 *  2. Mixer: a PCM16 1 kHz sine at 16 kHz played with SRC ratio 0.5 through the
 *     DSP command list interpreter: frequency (zero crossings), RMS, pan, and a
 *     volume ramp.
 *  3. End to end: sndInit + sndPushGroup with a synthetic project/pool/sdir and
 *     a PCM16 sample, sndFXStart, render through hw_pc's output path. With
 *     $MUSYX_TEST_WAV set, the rendered audio is written there as a WAV file.
 */
#include "musyx/musyx.h"
#include "musyx/hw_pcmixer.h"
#include "musyx/synthdata.h"
#include "musyx/voice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- platform pieces the MusyX objects need (provided by port/ in the game) ---- */
#define ARAM_SIZE (4u * 1024 * 1024)
static u8 sAram[ARAM_SIZE];
u8* portAramPtr(u32 addr) { return sAram + addr; }
u32 ARGetSize(void) { return ARAM_SIZE; }
void portAudioMixExtra(s16* out, u32 frames) {
  (void)out;
  (void)frames;
}
/* the volume settings (audio_mix_pc.c): every kind at full volume */
void portAudioMixUpdate(void) {}
void DCFlushRange(void* p, u32 n) {
  (void)p;
  (void)n;
}
void DCFlushRangeNoSync(void* p, u32 n) {
  (void)p;
  (void)n;
}
void DCStoreRangeNoSync(void* p, u32 n) {
  (void)p;
  (void)n;
}

static int sFailures;
#define CHECK(cond, ...)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                                \
      printf(__VA_ARGS__);                                                                         \
      printf("\n");                                                                                \
      sFailures++;                                                                                 \
    }                                                                                              \
  } while (0)

static u32 sRand = 12345;
static u32 rnd(void) {
  sRand = sRand * 1103515245u + 12345u;
  return sRand >> 16;
}

/* ------------------------------------------------------------------------- */
/* 1. ADPCM                                                                   */

static s16 refClamp(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (s16)v; }

/* Reference: decode whole 8-byte frames (header byte: predictor index in the
 * high nibble, shift in the low nibble; then 14 signed 4-bit samples). */
static void refDecode(const u8* data, int frames, const s16 coef[8][2], s16* out, s16* yn1p,
                      s16* yn2p) {
  int f, i;
  s16 yn1 = *yn1p, yn2 = *yn2p;
  for (f = 0; f < frames; f++) {
    const u8* fr = data + f * 8;
    int pred = (fr[0] >> 4) & 7, shift = fr[0] & 15;
    for (i = 0; i < 14; i++) {
      int nib = (i & 1) ? (fr[1 + i / 2] & 15) : (fr[1 + i / 2] >> 4);
      int s;
      if (nib >= 8) {
        nib -= 16;
      }
      /* floor division == arithmetic shift right by 11 */
      s = (int)floor(((double)(nib * (1 << shift)) * 2048 + coef[pred][0] * yn1 +
                      coef[pred][1] * yn2 + 1024) /
                     2048.0);
      s = refClamp(s);
      yn2 = yn1;
      yn1 = (s16)s;
      *out++ = (s16)s;
    }
  }
  *yn1p = yn1;
  *yn2p = yn2;
}

static void testAdpcm(void) {
  enum { FRAMES = 40, BASE = 0x10000 };
  static const s16 coef[8][2] = {{0, 0},       {2048, 0},    {0, 2048},   {1024, 1024},
                                 {4096, -2048}, {3584, -1536}, {3072, -1024}, {4608, -2560}};
  u8* data = sAram + BASE;
  s16 ref[FRAMES * 14], got[FRAMES * 14], ref2[(FRAMES - 4) * 14];
  s16 yn1 = 0, yn2 = 0, lyn1, lyn2;
  _PB pb;
  u32 i, start, end, loop;
  int mismatches = 0, f;

  printf("[adpcm] decode %d frames vs reference\n", FRAMES);
  for (f = 0; f < FRAMES; f++) {
    data[f * 8] = (u8)(((rnd() % 8) << 4) | (rnd() % 12));
    for (i = 1; i < 8; i++) {
      data[f * 8 + i] = (u8)rnd();
    }
  }
  refDecode(data, FRAMES, coef, ref, &yn1, &yn2);

  memset(&pb, 0, sizeof(pb));
  for (i = 0; i < 8; i++) {
    pb.adpcm.a[i][0] = (u16)coef[i][0];
    pb.adpcm.a[i][1] = (u16)coef[i][1];
  }
  /* MusyX starts after the first header (pred_scale from the sample info) */
  start = BASE * 2 + 2;
  pb.adpcm.pred_scale = data[0];
  end = BASE * 2 + (FRAMES - 1) * 16 + 2 + 13;
  pb.addr.format = 0;
  pb.addr.currentAddressHi = start >> 16;
  pb.addr.currentAddressLo = (u16)start;
  pb.addr.endAddressHi = end >> 16;
  pb.addr.endAddressLo = (u16)end;
  /* loop back to frame 4 */
  loop = BASE * 2 + 4 * 16 + 2;
  pb.addr.loopAddressHi = loop >> 16;
  pb.addr.loopAddressLo = (u16)loop;
  pb.addr.loopFlag = 1;
  pb.loopType = 0;
  pb.adpcmLoop.loop_pred_scale = data[4 * 8];
  pb.adpcmLoop.loop_yn1 = (u16)ref[4 * 14 - 1];
  pb.adpcmLoop.loop_yn2 = (u16)ref[4 * 14 - 2];
  pb.state = 1;

  salPcPbFetch(&pb, got, FRAMES * 14);
  for (i = 0; i < FRAMES * 14; i++) {
    if (got[i] != ref[i]) {
      if (mismatches++ < 5) {
        printf("  sample %u: got %d want %d\n", i, got[i], ref[i]);
      }
    }
  }
  CHECK(mismatches == 0, "%d ADPCM samples differ", mismatches);
  CHECK(pb.streamLoopCnt == 1, "loop counter %u", pb.streamLoopCnt);

  /* second pass from the loop point */
  lyn1 = ref[4 * 14 - 1];
  lyn2 = ref[4 * 14 - 2];
  refDecode(data + 4 * 8, FRAMES - 4, coef, ref2, &lyn1, &lyn2);
  salPcPbFetch(&pb, got, (FRAMES - 4) * 14);
  mismatches = 0;
  for (i = 0; i < (FRAMES - 4) * 14; i++) {
    mismatches += got[i] != ref2[i];
  }
  CHECK(mismatches == 0, "%d ADPCM samples differ after the loop", mismatches);

  /* one-shot: stops at the end address */
  pb.addr.loopFlag = 0;
  pb.addr.currentAddressHi = end >> 16;
  pb.addr.currentAddressLo = (u16)end;
  salPcPbFetch(&pb, got, 3);
  CHECK(pb.state == 0, "one-shot voice still running after its end");
  CHECK(got[1] == 0 && got[2] == 0, "output after the end is not silent");
  printf("  ok: %d samples bit-exact, loop and end handled\n", FRAMES * 14);
}

/* ------------------------------------------------------------------------- */
/* 2. Mixer                                                                   */

#define SINE_BASE 0x20000 /* ARAM byte address */
#define SINE_LEN 1600     /* 100 periods of 1 kHz at 16 kHz */
#define SINE_AMP 16000

static void writeSine(void) {
  u32 i;
  for (i = 0; i < SINE_LEN; i++) {
    s16 v = (s16)lrint(SINE_AMP * sin(2.0 * 3.14159265358979 * 1000.0 * i / 16000.0));
    sAram[SINE_BASE + i * 2] = (u8)((u16)v >> 8); /* big-endian, as in ARAM */
    sAram[SINE_BASE + i * 2 + 1] = (u8)v;
  }
}

static void setupSinePb(_PB* pb) {
  u32 start = SINE_BASE / 2, end = start + SINE_LEN - 1;
  memset(pb, 0, sizeof(*pb));
  pb->addr.format = 0xA;
  pb->addr.loopFlag = 1;
  pb->addr.currentAddressHi = start >> 16;
  pb->addr.currentAddressLo = (u16)start;
  pb->addr.loopAddressHi = start >> 16;
  pb->addr.loopAddressLo = (u16)start;
  pb->addr.endAddressHi = end >> 16;
  pb->addr.endAddressLo = (u16)end;
  pb->srcSelect = 0; /* 4-tap polyphase */
  pb->coefSelect = 0;
  pb->src.ratioHi = 0;
  pb->src.ratioLo = 0x8000; /* 0.5: 16 kHz source at 32 kHz */
  pb->ve.currentVolume = 0x7FFF;
  pb->mixerCtrl = 0x3;
  pb->mix.vL = 0x7FFF;
  pb->mix.vR = 0x4000;
  pb->state = 1;
}

typedef struct Stats {
  double rmsL, rmsR, freqL;
} Stats;

static Stats analyse(const s16* st, u32 frames) {
  Stats s;
  double sl = 0, sr = 0;
  u32 i, cross = 0, first = 0, last = 0;
  for (i = 0; i < frames; i++) {
    sl += (double)st[2 * i] * st[2 * i];
    sr += (double)st[2 * i + 1] * st[2 * i + 1];
    if (i > 0 && st[2 * (i - 1)] < 0 && st[2 * i] >= 0) {
      if (cross == 0) {
        first = i;
      }
      last = i;
      cross++;
    }
  }
  s.rmsL = sqrt(sl / frames);
  s.rmsR = sqrt(sr / frames);
  s.freqL = cross > 1 ? (cross - 1) * 32000.0 / (last - first) : 0;
  return s;
}

static void testMixer(void) {
  enum { FRAMES = 60 };
  static _PB pb;
  static u16 spb[27];
  static s32 studioMain[3 * 160], surround[160];
  static s16 dest[320], out[FRAMES * 320];
  u16 cmd[64];
  u32 n, f, i;
  Stats s;
  double expect;

  printf("[mixer] PCM16 1 kHz sine at 16 kHz, SRC ratio 0.5, via the command list\n");
  writeSine();
  setupSinePb(&pb);

  /* the same commands salBuildCommandList emits for one studio */
  n = 0;
#define H(p) (u16)(salPcPtrHandle(p) >> 16), (u16)salPcPtrHandle(p)
  {
    u16 list[] = {0, H(spb), 2, H(&pb), 3, 6, H(studioMain), 17, H(surround),
                  9, H(studioMain), 14, H(surround), H(dest), 15};
    memcpy(cmd, list, sizeof(list));
    n = sizeof(list) / sizeof(list[0]);
  }
#undef H
  (void)n;
  for (f = 0; f < FRAMES; f++) {
    salPcRunCommandList(cmd);
    memcpy(out + f * 320, dest, sizeof(dest));
  }
  /* skip the first frame (SRC history warm-up) */
  s = analyse(out + 320, (FRAMES - 1) * 160);
  expect = SINE_AMP / sqrt(2.0) * (0x7FFF / 32768.0) * (0x7FFF / 32768.0);
  printf("  L: %.1f Hz, RMS %.1f (expect %.1f); R RMS %.1f (expect %.1f)\n", s.freqL, s.rmsL,
         expect, s.rmsR, expect * 0x4000 / 0x7FFF);
  CHECK(fabs(s.freqL - 1000.0) < 5.0, "frequency %.2f Hz", s.freqL);
  CHECK(fabs(s.rmsL / expect - 1.0) < 0.03, "left RMS %.1f vs %.1f", s.rmsL, expect);
  CHECK(fabs(s.rmsR / (expect * 0x4000 / 0x7FFF) - 1.0) < 0.03, "right RMS (pan) %.1f",
        s.rmsR);

  /* volume ramp: left fades to silence over one frame */
  pb.mix.vDeltaL = (u16)(s16)(-(0x7FFF / 160));
  salPcRunCommandList(cmd);
  {
    double first = 0, lastQ = 0;
    for (i = 0; i < 40; i++) {
      first += fabs(dest[2 * i]);
      lastQ += fabs(dest[2 * (120 + i)]);
    }
    printf("  ramp: |L| first 40 = %.0f, last 40 = %.0f, vL after = 0x%x\n", first / 40,
           lastQ / 40, pb.mix.vL);
    CHECK(lastQ < first * 0.4, "ramp did not reduce the level");
    CHECK(pb.mix.vL < 0x100, "vL after the ramp 0x%x", pb.mix.vL);
  }
  pb.mix.vDeltaL = 0;
  pb.mix.vL = 0;
  salPcRunCommandList(cmd);
  s = analyse(dest, 160);
  CHECK(s.rmsL == 0.0, "left not silent at volume 0 (RMS %.1f)", s.rmsL);
  CHECK(s.rmsR > 1000.0, "right lost (RMS %.1f)", s.rmsR);
  if (sFailures == 0) {
    printf("  ok\n");
  }
}

/* ------------------------------------------------------------------------- */
/* 3. End to end                                                              */

static void* testMalloc(size_t n) { return calloc(1, n); }
static void testFree(void* p) { free(p); }

static void writeWav(const char* path, const s16* st, u32 frames) {
  FILE* f = fopen(path, "wb");
  u32 data = frames * 4, v;
  u8 h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
              1,   0,   2,   0,   0, 0, 0, 0, 0,   0,   0,   0,   4,   0,   16,  0,   'd', 'a', 't', 'a'};
  u32 i;
  if (f == NULL) {
    return;
  }
  v = data + 36;
  memcpy(h + 4, (u8[]){(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)}, 4);
  v = 32000;
  memcpy(h + 24, (u8[]){(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)}, 4);
  v = 128000;
  memcpy(h + 28, (u8[]){(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)}, 4);
  memcpy(h + 40, (u8[]){(u8)data, (u8)(data >> 8), (u8)(data >> 16), (u8)(data >> 24)}, 4);
  fwrite(h, 1, 44, f);
  for (i = 0; i < frames * 2; i++) {
    u8 b[2] = {(u8)st[i], (u8)((u16)st[i] >> 8)};
    fwrite(b, 1, 2, f);
  }
  fclose(f);
  printf("  wrote %s (%u frames)\n", path, frames);
}

/* A project with one FX group (gid 1): FX 7 -> macro 3 -> StartSample 1, wait
 * 300 ms, end. Sample 1: 1 s of PCM16 1 kHz at 16 kHz, root key 60. */
static void testEndToEnd(void) {
  enum { SECONDS = 2 };
  static u8 prj[256], pool[256], samples[SINE_LEN * 20 + 64];
  static SDIR_DATA sdir[2];
  static s16 out[SECONDS * 32000 * 2];
  GROUP_DATA* g = (GROUP_DATA*)prj;
  FX_DATA* fx;
  POOL_DATA* pd = (POOL_DATA*)pool;
  MEM_DATA* m;
  u16* ids;
  SND_HOOKS hooks = {testMalloc, testFree};
  SND_VOICEID vid;
  u32 i, nsmp = 16000;
  Stats s, tail;
  const char* wav = getenv("MUSYX_TEST_WAV");

  printf("[e2e] sndInit + sndPushGroup + sndFXStart, render %d s\n", SECONDS);

  /* sample data (big-endian PCM16) in main RAM; MusyX uploads it to ARAM */
  for (i = 0; i < nsmp; i++) {
    s16 v = (s16)lrint(SINE_AMP * sin(2.0 * 3.14159265358979 * 1000.0 * i / 16000.0));
    samples[i * 2] = (u8)((u16)v >> 8);
    samples[i * 2 + 1] = (u8)v;
  }
  memset(sdir, 0, sizeof(sdir));
  sdir[0].id = 1;
  sdir[0].offset = 0;
  sdir[0].header.info = (60u << 24) | 16000;
  sdir[0].header.length = (2u << 24) | nsmp; /* compType 2: PCM16 */
  sdir[1].id = 0xFFFF;

  /* project: group header, terminator, id lists, FX table */
  memset(prj, 0, sizeof(prj));
  g->id = 1;
  g->type = 1;
  g->nextOff = 0x40;
  *(u32*)(prj + 0x40) = 0xFFFFFFFF;
  ids = (u16*)(prj + 0x48); /* macros: 3 */
  ids[0] = 3;
  ids[1] = 0xFFFF;
  g->macroOff = 0x48;
  ids = (u16*)(prj + 0x50); /* samples: 1 */
  ids[0] = 1;
  ids[1] = 0xFFFF;
  g->sampleOff = 0x50;
  ids = (u16*)(prj + 0x58); /* no curves, keymaps, layers */
  ids[0] = 0xFFFF;
  g->curveOff = g->keymapOff = g->layerOff = 0x58;
  fx = (FX_DATA*)(prj + 0x60);
  fx->num = 1;
  fx->fx[0].id = 7;
  fx->fx[0].macro = 3;
  fx->fx[0].maxVoices = 1;
  fx->fx[0].priority = 0x40;
  fx->fx[0].volume = 127;
  fx->fx[0].panning = 64;
  fx->fx[0].key = 60;
  g->data.song.normpageOff = 0x60;

  /* pool: macro 3, then terminators for every list */
  memset(pool, 0, sizeof(pool));
  pd->macroOff = 0x10;
  m = (MEM_DATA*)(pool + 0x10);
  m->id = 3;
  m->data.cmd[0][0].para[0] = 0x10 | (1u << 8); /* StartSample 1, offset 0 */
  m->data.cmd[0][0].para[1] = 0;
  m->data.cmd[0][1].para[0] = 0x07;           /* WaitMs 300 */
  m->data.cmd[0][1].para[1] = 300u << 16;
  m->data.cmd[1][0].para[0] = 0x00;           /* End */
  m->data.cmd[1][0].para[1] = 0;
  m->nextOff = 0x40;
  *(u32*)(pool + 0x50) = 0xFFFFFFFF;
  pd->curveOff = pd->keymapOff = pd->layerOff = 0x50;

  sndSetHooks(&hooks);
  CHECK(sndInit(8, 2, 4, 1, SND_FLAGS_DEFAULT_STUDIO_DPL2, 1024 * 1024) == 0, "sndInit failed");
  CHECK(sndPushGroup(prj, 1, samples, sdir, pool), "sndPushGroup failed");
  CHECK((u32)(size_t)sdir[0].addr >= 0x4000, "sample not uploaded (addr 0x%x)",
        (u32)(size_t)sdir[0].addr);
  sndVolume(127, 0, 31); /* FX volume group (dataInsertFX puts every FX in group 31) */
  vid = sndFXStart(7, 127, 64);
  CHECK(vid != SND_ID_ERROR, "sndFXStart failed");

  salPcRender(out, SECONDS * 32000);
  s = analyse(out + 2 * 3200, 16000); /* 0.1 .. 0.6 s */
  tail = analyse(out + 2 * 51200, 12800);  /* 1.6 .. 2.0 s */
  printf("  playing: %.1f Hz, RMS L %.1f R %.1f; after the sample: RMS %.1f\n", s.freqL,
         s.rmsL, s.rmsR, tail.rmsL);
  CHECK(fabs(s.freqL - 1000.0) < 10.0, "frequency %.1f Hz", s.freqL);
  CHECK(s.rmsL > 1000.0 && s.rmsR > 1000.0, "too quiet (L %.1f, R %.1f)", s.rmsL, s.rmsR);
  CHECK(fabs(s.rmsL / s.rmsR - 1.0) < 0.1, "centre pan unbalanced (%.1f / %.1f)", s.rmsL,
        s.rmsR);
  CHECK(tail.rmsL < 10.0, "not silent after the sample ended (RMS %.1f)", tail.rmsL);
  if (wav != NULL) {
    writeWav(wav, out, SECONDS * 32000);
  }
}

int main(void) {
  testAdpcm();
  testMixer();
  testEndToEnd();
  if (sFailures) {
    printf("%d check(s) failed\n", sFailures);
    return 1;
  }
  printf("all MusyX PC tests passed\n");
  return 0;
}
