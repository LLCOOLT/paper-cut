/* MusyX song sweep on the game's own sound data (headless; skipped without a disc):
 *
 *   musyx_seq_sweep <data/<version>/files>
 *
 * Loads sound/proj/pmario.{proj,pool,sdir,slib} the way src/sound.c does on
 * TARGET_PC (port/src/common/musyx_swap.c), pushes groups 0..11 like
 * psndPushGroup (samples uploaded from pmario.samp), then for every song of
 * the library:
 *  1. walks it natively like the sequencer will (tracks, loop jumps, pattern
 *     table, note/controller events, pitch/modulation streams, tempo track,
 *     MIDI channel table) and checks every offset stays inside the song;
 *  2. plays it through sndSeqPlayEx in the group whose MIDI setups name it,
 *     renders until it ends or has looped twice (cap: 4 minutes of song time),
 *     and after every 50 ms checks the sequencer's cursors (track, pattern,
 *     stream, tempo) still point inside the song and its event lists are sane.
 * All failures are reported together. Exit 0 = all songs pass, 77 = no data.
 */
#include "musyx/musyx.h"
#include "musyx/hw_pcmixer.h"
#include "musyx/seq.h"
#include "musyx/synthdata.h"
#include "port/musyx_swap_pc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- platform pieces the MusyX objects need (provided by port/ in the game) ---- */
#define ARAM_SIZE (16u * 1024 * 1024)
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
static int sSongFailed;
#define FAIL(...)                                                                                  \
  do {                                                                                             \
    printf("  FAIL song %d: ", sSong);                                                             \
    printf(__VA_ARGS__);                                                                           \
    printf("\n");                                                                                  \
    sFailures++;                                                                                   \
    sSongFailed = 1;                                                                               \
  } while (0)

static int sSong;

static void* hookMalloc(size_t n) { return calloc(1, n); }
static void hookFree(void* p) { free(p); }

static u8* loadFile(const char* dir, const char* ext, u32* lenOut) {
  char path[1024];
  FILE* f;
  long n;
  u8* p;
  snprintf(path, sizeof(path), "%s/sound/proj/pmario%s", dir, ext);
  if ((f = fopen(path, "rb")) == NULL) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  p = (u8*)calloc(1, ((u32)n + 31) & ~31u);
  if (p != NULL && fread(p, 1, (size_t)n, f) != (size_t)n) {
    free(p);
    p = NULL;
  }
  fclose(f);
  *lenOut = ((u32)n + 31) & ~31u; /* the game rounds reads up to 32 bytes */
  return p;
}

/* pmario.samp is uploaded to ARAM in chunks, like loadDVD_callback. */
static FILE* sSamp;
static u8 sChunk[0x20000] __attribute__((aligned(32)));
static void* sampUpload(u32 offset, u32 bytes) {
  if (bytes > sizeof(sChunk) || fseek(sSamp, (long)offset, SEEK_SET) != 0) {
    return NULL;
  }
  memset(sChunk, 0, bytes);
  fread(sChunk, 1, bytes, sSamp);
  return sChunk;
}

/* ---- 1. static walk of a native song ---- */

static int inSong(u32 len, u32 off, u32 size) { return off <= len && size <= len - off; }

static void walkSong(const u8* a, u32 len) {
  const ARR* arr = (const ARR*)a;
  const u32* tTab;
  const u32* pTab;
  u32 i, numPatterns = 0, pTabEnd;

  if (!inSong(len, 0, sizeof(ARR))) {
    FAIL("song shorter than its header (%u bytes)", len);
    return;
  }
  if (!inSong(len, arr->tTab, 64 * 4) || !inSong(len, arr->tmTab, 64) || arr->pTab >= len ||
      (arr->mTrack != 0 && arr->mTrack >= len) ||
      ((arr->info & 0x80000000) && !inSong(len, arr->tsTab, 64))) {
    FAIL("header offsets out of range: tTab %x pTab %x tmTab %x mTrack %x tsTab %x (len %x)",
         arr->tTab, arr->pTab, arr->tmTab, arr->mTrack, arr->tsTab, len);
    return;
  }
  tTab = (const u32*)(a + arr->tTab);
  for (i = 0; i < 64; i++) {
    u32 t = tTab[i], k = 0, steps = 0;
    if (t == 0) {
      continue;
    }
    if (a[arr->tmTab + i] >= 16) {
      FAIL("track %u: MIDI channel %u", i, a[arr->tmTab + i]);
    }
    if ((arr->info & 0x80000000) && a[arr->tsTab + i] >= 16) {
      FAIL("track %u: section %u", i, a[arr->tsTab + i]);
    }
    for (;;) {
      const TENTRY* e;
      if (!inSong(len, t + k * 12, 12) || ++steps > 100000) {
        FAIL("track %u: runs off the song at entry %u", i, k);
        break;
      }
      e = (const TENTRY*)(a + t + k * 12);
      if (e->pattern == 0xFFFF) {
        break;
      }
      if (e->pattern == 0xFFFE) {
        u16 target = *(const u16*)&e->transpose;
        if (target >= k) {
          FAIL("track %u: loop at entry %u jumps forward to %u", i, k, target);
        }
        break;
      }
      if (e->pattern + 1u > numPatterns) {
        numPatterns = e->pattern + 1u;
      }
      k++;
    }
  }
  pTabEnd = arr->pTab + numPatterns * 4;
  if (!inSong(len, arr->pTab, numPatterns * 4)) {
    FAIL("pattern table (%u entries) out of range", numPatterns);
    return;
  }
  pTab = (const u32*)(a + arr->pTab);
  for (i = 0; i < numPatterns; i++) {
    u32 p = pTab[i], steps = 0;
    const SEQ_PATTERN* pat;
    if (p == 0) {
      continue;
    }
    if (p < pTabEnd || !inSong(len, p, 12)) {
      FAIL("pattern %u: offset %x out of range", i, p);
      continue;
    }
    pat = (const SEQ_PATTERN*)(a + p);
    if ((pat->pitchBend != 0 && pat->pitchBend >= len) ||
        (pat->modulation != 0 && pat->modulation >= len)) {
      FAIL("pattern %u: stream offsets %x/%x out of range", i, pat->pitchBend, pat->modulation);
    }
    for (p += 12;; ) {
      const NOTE_DATA* nd;
      if (!inSong(len, p, 4) || ++steps > 1000000) {
        FAIL("pattern %u: runs off the song", i);
        break;
      }
      nd = (const NOTE_DATA*)(a + p);
      if (nd->key == 0xFF && nd->velocity == 0xFF) {
        break;
      }
      p += ((nd->key & 0x80) || (nd->key | nd->velocity) == 0) ? 4 : 6;
    }
  }
  if (arr->mTrack != 0) {
    u32 m = arr->mTrack, prev = 0;
    for (;; m += 8) {
      const MTRACK_DATA* md;
      if (!inSong(len, m, 8)) {
        FAIL("tempo track runs off the song");
        break;
      }
      md = (const MTRACK_DATA*)(a + m);
      if (md->time == 0xFFFFFFFF) {
        break;
      }
      if (md->time < prev) {
        FAIL("tempo track time goes backwards (%u after %u)", md->time, prev);
        break;
      }
      prev = md->time;
    }
  }
}

/* ---- 2. playback ---- */

static int inPtr(const void* p, const u8* a, u32 len) {
  return p == NULL || ((const u8*)p >= a && (const u8*)p < a + len);
}

static SEQ_INSTANCE* findInstance(SND_SEQID id) {
  u32 i;
  for (i = 0; i < 8; i++) {
    if (seqInstance[i].state != 0 && seqInstance[i].publicId == id) {
      return &seqInstance[i];
    }
  }
  return NULL;
}

static int checkInstance(const SEQ_INSTANCE* si, const u8* a, u32 len) {
  u32 i;
  for (i = 0; i < 64; i++) {
    const CPAT* cp = &si->pattern[i];
    if (!inPtr(si->track[i].addr, a, len) || !inPtr(si->track[i].base, a, len)) {
      FAIL("track %u cursor %p outside the song", i, (void*)si->track[i].addr);
      return 0;
    }
    /* A pattern's fields are live only while it plays (addr != NULL), and a
     * stream cursor only while it has a next event: the rest keep values
     * from earlier songs, as on the GameCube. */
    if (cp->addr == NULL) {
      continue;
    }
    if (!inPtr(cp->addr, a, len) || !inPtr(cp->patternInfo, a, len) ||
        (cp->pitchBend.nextTime != 0x7fffffff && !inPtr(cp->pitchBend.nextAddr, a, len)) ||
        (cp->modulation.nextTime != 0x7fffffff && !inPtr(cp->modulation.nextAddr, a, len))) {
      FAIL("track %u pattern cursor outside the song", i);
      return 0;
    }
  }
  for (i = 0; i < 16; i++) {
    const SEQ_SECTION* s = &si->section[i];
    const SEQ_EVENT* ev;
    u32 n = 0;
    if (s->mTrack.base != NULL && !inPtr(s->mTrack.addr, a, len)) {
      FAIL("section %u tempo cursor outside the song", i);
      return 0;
    }
    for (ev = s->globalEventRoot; ev != NULL; ev = ev->next) {
      if (ev < &si->event[0] || ev >= &si->event[64] || ++n > 64) {
        FAIL("section %u: corrupt event list", i);
        return 0;
      }
    }
  }
  return 1;
}

enum { CHUNK = 1600, MAX_SECONDS = 240 }; /* 50 ms at 32 kHz */

static const GROUP_DATA* songGroup(const u8* proj, u32 sid, int maxGroup) {
  const GROUP_DATA* g = (const GROUP_DATA*)proj;
  while (g->nextOff != 0xFFFFFFFF) {
    if (g->type == 0 && g->id <= maxGroup) {
      const MIDISETUP* ms = (const MIDISETUP*)(proj + g->data.song.midiSetupOff);
      for (; ms->songId != 0xFFFF; ms++) {
        if (ms->songId == sid) {
          return g;
        }
      }
    }
    g = (const GROUP_DATA*)(proj + g->nextOff);
  }
  return NULL;
}

static void playSong(const GROUP_DATA* g, u8* a, u32 len, const char** result, double* secs) {
  static s16 out[CHUNK * 2];
  SND_SEQID id;
  SEQ_INSTANCE* si;
  u32 frames = 0;
  u16 loops = 0;

  id = sndSeqPlayEx(g->id, (SND_SONGID)sSong, a, NULL, 0);
  if (id == SND_SEQ_ERROR_ID) {
    FAIL("sndSeqPlayEx(%u, %d) failed", g->id, sSong);
    *result = "no start";
    return;
  }
  *result = "timeout";
  while (frames < MAX_SECONDS * 32000u) {
    salPcRender(out, CHUNK);
    frames += CHUNK;
    if (!sndSeqGetValid(id)) {
      *result = "ended";
      break;
    }
    if ((si = findInstance(id)) == NULL) {
      FAIL("seq %u valid but no instance", id);
      break;
    }
    if (!checkInstance(si, a, len)) {
      *result = "bad state";
      break;
    }
    loops = si->section[0].loopCnt;
    if (loops >= 2) {
      *result = "looped";
      break;
    }
  }
  *secs = frames / 32000.0;
  if (sndSeqGetValid(id)) {
    sndSeqStop(id);
  }
  /* let the voices release before the next song */
  for (frames = 0; frames < 32000; frames += CHUNK) {
    salPcRender(out, CHUNK);
  }
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : "data/G8MJ01/files";
  u8 *proj, *pool, *sdirRaw, *sdir, *slib;
  u32 projLen, poolLen, sdirLen, slibLen, off, nativeSize;
  char sampPath[1024];
  SND_HOOKS hooks = {hookMalloc, hookFree};
  int pushed = 0, songs = 0, played = 0, noGroup = 0, gid;

  proj = loadFile(dir, ".proj", &projLen);
  pool = loadFile(dir, ".pool", &poolLen);
  sdirRaw = loadFile(dir, ".sdir", &sdirLen);
  slib = loadFile(dir, ".slib", &slibLen);
  snprintf(sampPath, sizeof(sampPath), "%s/sound/proj/pmario.samp", dir);
  sSamp = fopen(sampPath, "rb");
  if (proj == NULL || pool == NULL || sdirRaw == NULL || slib == NULL || sSamp == NULL) {
    printf("musyx_seq_sweep: no sound data under %s/sound/proj, skipped\n", dir);
    return 77;
  }
  portMusyxSwapPool(pool, poolLen);
  portMusyxSwapProj(proj, projLen);
  nativeSize = portMusyxSdirNativeSize(sdirRaw, sdirLen);
  sdir = (u8*)calloc(1, nativeSize);
  portMusyxSdirConvert(sdir, sdirRaw, sdirLen);
  portMusyxSwapSlib(slib, slibLen);

  sndSetHooks(&hooks);
  if (sndInit(0x40, 0xC, 0x2C, 1, 1, 0xBFC000) != 0) {
    printf("musyx_seq_sweep: sndInit failed\n");
    return 1;
  }
  sndSetSampleDataUploadCallback(sampUpload, 0x20000);
  for (gid = 0; gid <= 11; gid++) {
    pushed += sndPushGroup(proj, (u16)gid, NULL, sdir, pool) ? 1 : 0;
  }
  sndVolume(127, 0, 31);
  printf("musyx_seq_sweep: %s, %d groups pushed\n", dir, pushed);

  for (off = 0, sSong = 0; off + 4 <= slibLen; sSong++) {
    u32 size = *(u32*)(slib + off);
    u8* a;
    u32 len;
    const GROUP_DATA* g;
    const char* result = "";
    double secs = 0;
    if (size == 0xFFFFFFFF || size < 0x20 || size > slibLen - off) {
      break;
    }
    a = slib + off + 0x20;
    len = size - 0x20;
    songs++;
    sSongFailed = 0;
    walkSong(a, len);
    if (!sSongFailed) {
      if ((g = songGroup(proj, (u32)sSong, 11)) == NULL) {
        noGroup++;
        printf("  song %3d: %5u bytes, no group plays it\n", sSong, len);
      } else {
        playSong(g, a, len, &result, &secs);
        played++;
        printf("  song %3d: %5u bytes, group %2u: %-9s after %6.2f s\n", sSong, len, g->id, result,
               secs);
      }
    }
    off += size;
  }

  printf("musyx_seq_sweep: %d songs, %d played, %d without a group, %d failure(s)\n", songs,
         played, noGroup, sFailures);
  return sFailures != 0 || songs == 0;
}
