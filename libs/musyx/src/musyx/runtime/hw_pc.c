#include "musyx/platform.h"

#if MUSY_TARGET == MUSY_TARGET_PC
/*
 * MusyX hardware layer for the PC port.
 *
 * The GameCube runs MusyX from the AI DMA interrupt every 5 ms (160 stereo
 * frames at 32 kHz): build the DSP command list for the next buffer, run the
 * aux effects, then advance the sequencer/synth by 5 x 1 ms. Here the host
 * audio callback pulls frames and runs the same interrupt handler per 160-frame
 * quantum; the DSP command list is executed synchronously by the software mixer
 * (hw_pcmixer.c) into the quantum's output buffer.
 *
 * Output: an SDL3 audio stream (S16 stereo 32 kHz) on the Aurora backend. The
 * headless backend has no device: salPcHeadlessPump (called once per video
 * frame) renders the frames due in real time into a null sink, optionally
 * written to a WAV file named by $TTYD_AUDIO_WAV.
 *
 * "Interrupts" (hwDisableIrq/hwEnableIrq, hwIRQEnter/LeaveCritical) are one
 * recursive mutex shared by the game thread and the audio thread.
 */
#include "musyx/assert.h"
#include "musyx/hardware.h"
#include "musyx/hw_pcmixer.h"
#include "musyx/sal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Base address of the audio output buffers in memory. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static CRITICAL_SECTION globalMutex;
static void mutexInit(void) { InitializeCriticalSection(&globalMutex); }
static void mutexLock(void) { EnterCriticalSection(&globalMutex); }
static void mutexUnlock(void) { LeaveCriticalSection(&globalMutex); }
#else
#include <pthread.h>
static pthread_mutex_t globalMutex;
static void mutexInit(void) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&globalMutex, &attr);
  pthread_mutexattr_destroy(&attr);
}
static void mutexLock(void) { pthread_mutex_lock(&globalMutex); }
static void mutexUnlock(void) { pthread_mutex_unlock(&globalMutex); }
#endif

#ifdef TTYD_BACKEND_AURORA
#include <SDL3/SDL.h>
#include "port/ai_pc.h"
/* SDL pulls in <stdbool.h>, whose bool macro (_Bool) would shadow MusyX's
 * 4-byte bool typedef that sal.h's prototypes were declared with. */
#undef bool
static SDL_AudioStream* sStream;
#endif

#define QUANTUM_FRAMES 160
u32 salGetStartDelay(void);
#define OUTPUT_RATE 32000

static volatile int sMutexReady;
static volatile int sStarted;
static SND_SOME_CALLBACK userCallback = NULL;

static s16 sDest[QUANTUM_FRAMES * 2];  /* DSP OUTPUT target (salAiGetDest) */
static s16 sQuantum[QUANTUM_FRAMES * 2]; /* last rendered quantum */
static u32 sQuantumPos = QUANTUM_FRAMES; /* frames of sQuantum already consumed */
/** PC build version of the frame callback runner (interrupt control compiled out). */
  // OSEnableInterrupts();
  // OSDisableInterrupts();
/** PC build: advances the output buffer index and runs or defers the frame callback; AI DMA calls are commented out. */
  // AIInitDMA(OSCachedToPhysical(salAIBufferBase) + (salAIBufferIndex * DMA_BUFFER_LEN),
  //           DMA_BUFFER_LEN);
static void ensureMutex(void) {
  if (!sMutexReady) {
    mutexInit();
    sMutexReady = 1;
  }
}

/** PC build: marks the (nonexistent) DSP as booted. */
/* One 5 ms "AI interrupt": MusyX renders sDest and advances its logic. */
static void runQuantum(void) {
  if (sStarted && userCallback != NULL) {
    userCallback();
    memcpy(sQuantum, sDest, sizeof(sQuantum));
  } else {
    memset(sQuantum, 0, sizeof(sQuantum));
  }
  portAudioMixExtra(sQuantum, QUANTUM_FRAMES);
  sQuantumPos = 0;
#ifdef TTYD_BACKEND_AURORA
  /* As the AI DMA interrupt: the game's DMA callback (sound.c) sees each block. */
  portAISetDMABuffer(sQuantum, sizeof(sQuantum));
  portAIRunDMACallback();
#endif
}

/** PC build: marks the DSP done and runs a deferred frame callback. */
void salPcRender(s16* out, u32 frames) {
  while (frames != 0) {
    u32 n;
    if (sQuantumPos == QUANTUM_FRAMES) {
      runQuantum();
    }
    n = QUANTUM_FRAMES - sQuantumPos;
    if (n > frames) {
      n = frames;
    }
    memcpy(out, sQuantum + sQuantumPos * 2, n * 2 * sizeof(s16));
    sQuantumPos += n;
    out += n * 2;
    frames -= n;
  }
}

/** PC build: allocates the four output buffers and reports 32000 Hz, without registering an AI DMA callback. */
    // DCFlushRange(salAIBufferBase, DMA_BUFFER_LEN * 4);
    // AIRegisterDMACallback(salCallback);
    // AIInitDMA(OSCachedToPhysical(salAIBufferBase) + (salAIBufferIndex * 0x280), 0x280);
#ifdef TTYD_BACKEND_AURORA
static void SDLCALL audioCallback(void* user, SDL_AudioStream* stream, int additional, int total) {
  s16 buf[QUANTUM_FRAMES * 2];
  (void)user;
  (void)total;
  while (additional > 0) {
    salPcRender(buf, QUANTUM_FRAMES);
    SDL_PutAudioStreamData(stream, buf, sizeof(buf));
    additional -= (int)sizeof(buf);
  }

}

/** PC build: does nothing (the GameCube version starts AI DMA). */
/* No audio device: called once per video frame (vi_aurora.c), renders the
 * frames due into a null sink as the headless backend does, so MusyX and the
 * game's waits on sound still advance. */
void salPcAuroraPump(double seconds) {
  static double due;
  s16 buf[QUANTUM_FRAMES * 2];
  if (sStream != NULL || !sStarted) {
    return;
  }
  due += seconds * OUTPUT_RATE;
  if (due > OUTPUT_RATE / 4) {
    due = OUTPUT_RATE / 4; /* don't try to catch up after a stall */
  }
  while (due >= QUANTUM_FRAMES) {
    salPcRender(buf, QUANTUM_FRAMES);
    due -= QUANTUM_FRAMES;
  }
}
#else
/* Headless null sink. */
static FILE* sWav;
static u32 sWavFrames;
static double sDue;

/** PC build: frees the output buffers. */
static void wavHeader(FILE* f, u32 frames) {
  u8 h[44];
  u32 data = frames * 4;
  static const u8 tmpl[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't',
                              ' ', 16, 0, 0, 0, 1, 0, 2, 0, 0x00, 0x7D, 0, 0, 0x00, 0xF4, 1, 0,
                              4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
  memcpy(h, tmpl, sizeof(h));
  h[4] = (u8)(data + 36);
  h[5] = (u8)((data + 36) >> 8);
  h[6] = (u8)((data + 36) >> 16);
  h[7] = (u8)((data + 36) >> 24);
  h[40] = (u8)data;
  h[41] = (u8)(data >> 8);
  h[42] = (u8)(data >> 16);
  h[43] = (u8)(data >> 24);
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, sizeof(h), f);
  fseek(f, 0, SEEK_END);
}

void salPcHeadlessWrite(const s16* frames, u32 count) {
  u32 i;
  if (sWav == NULL) {
    return;
  }
  for (i = 0; i < count * 2; i++) {
    u8 b[2] = {(u8)frames[i], (u8)((u16)frames[i] >> 8)};
    fwrite(b, 1, 2, sWav);
  }
  sWavFrames += count;
  wavHeader(sWav, sWavFrames);
}

/* Call once per video frame (VIWaitForRetrace): renders the 32 kHz frames due
 * for `seconds` of elapsed time. */
void salPcHeadlessPump(double seconds) {
  s16 buf[QUANTUM_FRAMES * 2];
  if (!sStarted) {
    return;
  }
  sDue += seconds * OUTPUT_RATE;
  if (sDue > OUTPUT_RATE / 4) {
    sDue = OUTPUT_RATE / 4; /* don't try to catch up after a stall */
  }
  while (sDue >= QUANTUM_FRAMES) {
    salPcRender(buf, QUANTUM_FRAMES);
    salPcHeadlessWrite(buf, QUANTUM_FRAMES);
    sDue -= QUANTUM_FRAMES;
  }
}
#endif

/* portPause (pause_pc.c): the device stops pulling, so MusyX's sequencer and
 * voices stop where they are. Headless audio is rendered per video frame and
 * stops with the frames. */
/* The settings page's volume (port/src/aurora/settings_aurora.c): the
 * device stream's gain, so the mix itself (and its golden hashes) is the
 * same at any volume. */
#ifdef TTYD_BACKEND_AURORA
static float sGain = 1.0f;

void salPcSetVolume(float gain) {
  sGain = gain;
  if (sStream != NULL) {
    SDL_SetAudioStreamGain(sStream, gain);
  }
}
#else
void salPcSetVolume(float gain) {
  (void)gain;
}
#endif

/* Save states (port/src/aurora/savestate_aurora.c): no audio callback runs
 * between salPcLock and salPcUnlock (SDL calls it with the stream locked). */
void salPcLock(void) {
#ifdef TTYD_BACKEND_AURORA
  if (sStream != NULL) {
    SDL_LockAudioStream(sStream);
  }
#endif
}

void salPcUnlock(void) {
#ifdef TTYD_BACKEND_AURORA
  if (sStream != NULL) {
    SDL_UnlockAudioStream(sStream);
  }
#endif
}

void salPcSetPaused(int paused) {
#ifdef TTYD_BACKEND_AURORA
  if (sStream != NULL && sStarted) {
    if (paused) {
      SDL_PauseAudioStreamDevice(sStream);
    } else {
      SDL_ResumeAudioStreamDevice(sStream);
    }
  }
#else
  (void)paused;
#endif
}

bool salInitAi(SND_SOME_CALLBACK callback, u32 unk, u32* outFreq) {
  (void)unk;
  memset(sDest, 0, sizeof(sDest));
  sStarted = FALSE;
  userCallback = callback;
  synthInfo.numSamples = 0x20;
  *outFreq = OUTPUT_RATE;
#ifdef TTYD_BACKEND_AURORA
  /* $TTYD_FAST (test runs, frames back to back): no device; salPcAuroraPump
   * renders per frame, so the sequencer keeps to the game's frames, not the
   * wall clock, and runs repeat exactly. */
  if (sStream == NULL && getenv("TTYD_FAST") == NULL) {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = OUTPUT_RATE;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      fprintf(stderr, "MusyX: SDL audio init failed: %s\n", SDL_GetError());
    } else {
      sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback,
                                          NULL);
      if (sStream == NULL) {
        fprintf(stderr, "MusyX: no audio device: %s\n", SDL_GetError());
      } else {
        SDL_SetAudioStreamGain(sStream, sGain);
      }
    }
  }
#else
  if (sWav == NULL && getenv("TTYD_AUDIO_WAV") != NULL) {
    sWav = fopen(getenv("TTYD_AUDIO_WAV"), "wb");
    if (sWav != NULL) {
      sWavFrames = 0;
      wavHeader(sWav, 0);
    }
  }
#endif
  MUSY_DEBUG("MusyX AI interface initialized.\n");
  /* Like the GameCube, sound works (silently) even without an output device. */
  return TRUE;
}

/** PC build: computes the destination index but returns NULL. */
bool salStartAi() {
  sStarted = TRUE;
#ifdef TTYD_BACKEND_AURORA
  if (sStream != NULL) {
    SDL_ResumeAudioStreamDevice(sStream);
  }
#endif
  return TRUE;
}

/** PC build: nothing to start, returns TRUE. */
bool salExitAi() {
#ifdef TTYD_BACKEND_AURORA
  if (sStream != NULL) {
    SDL_DestroyAudioStream(sStream);
    sStream = NULL;
  }
#else
  if (sWav != NULL) {
    fclose(sWav);
    sWav = NULL;
  }
#endif
  sStarted = FALSE;
  return TRUE;
}

/** PC build: empty. */
void* salAiGetDest() { return sDest; }

/** PC build: empty. */
bool salInitDsp(u32 flags) {
  (void)flags;
  return TRUE;
}

bool salExitDsp() { return TRUE; }

void salStartDsp(u16* cmdList) { salPcRunCommandList(cmdList); }

/** PC build: builds the command list but there is no DSP to send it to. */
void salCtrlDsp(s16* dest) {
  salBuildCommandList(dest, salGetStartDelay());
  salStartDsp(dspCmdList);
}

/** PC build: always 0. */
u32 salGetStartDelay(void) { return 0; }

/** PC build: sets the nesting level to 1 and creates the robust global mutex used as the "interrupt" lock. */
void hwInitIrq() {
  // oldState = OSDisableInterrupts();
  ensureMutex();
  /* As on the GameCube, initialisation runs with "interrupts" disabled until
   * hwInit's hwEnableIrq. */
  mutexLock();
}

/** PC build: empty. */
void hwExitIrq() {}

/** PC build: drops the nesting level (the interrupt restore is commented out). */
    // OSRestoreInterrupts(oldState);
void hwEnableIrq() { mutexUnlock(); }

/** PC build: raises the nesting level (the interrupt disable is commented out). */
void hwDisableIrq() {
    // oldState = OSDisableInterrupts();
  ensureMutex();
  mutexLock();
}

/** PC build: takes the global mutex. */
void hwIRQEnterCritical() {
  ensureMutex();
  mutexLock();
}

/** PC build: releases the global mutex. */
void hwIRQLeaveCritical() { mutexUnlock(); }
#endif
