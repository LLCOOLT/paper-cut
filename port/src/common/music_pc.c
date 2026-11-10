/* Music shuffle and the player's own music (port/include/port/music.h).
 *
 * What the game plays is never changed: psbgm keeps the track asked for and
 * its stream still runs. The shuffle only swaps the disc stream heard for
 * another of the same kind, and a file from the music folder plays here, on
 * top of the mixer's output, while the stream it stands for is silenced
 * (PORT_MIX_SILENT) and goes on as before: it starts, pauses, fades and ends
 * when the game's would, so nothing the game can see differs. The shuffle's
 * random numbers are its own (never ttyd_rand).
 *
 * Files are read and decoded on the audio thread, a block at a time, and
 * resampled to the mixer's 32 kHz. WAV is read here; OGG Vorbis, MP3 and
 * FLAC need stb_vorbis.c, dr_mp3.h and dr_flac.h in libs/audio_decoders
 * (public domain / MIT-0; each is used when it is there). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "driver/npcdrv.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "port/audio_mix.h"
#include "port/gameplus.h"
#include "port/music.h"
#include "port/pm64_music.h"
#include "port/postgame_extra.h"
#include "port/practice.h"
#include "port/settings.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <wchar.h>
static SRWLOCK sLock = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&sLock)
#define UNLOCK() ReleaseSRWLockExclusive(&sLock)
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&sLock)
#define UNLOCK() pthread_mutex_unlock(&sLock)
#endif

#if defined(__has_include)
#if __has_include("../../../libs/audio_decoders/stb_vorbis.c")
#define MUSIC_OGG 1
#endif
#if __has_include("../../../libs/audio_decoders/dr_mp3.h")
#define MUSIC_MP3 1
#endif
#if __has_include("../../../libs/audio_decoders/dr_flac.h")
#define MUSIC_FLAC 1
#endif
#endif

#ifdef MUSIC_OGG
#include "../../../libs/audio_decoders/stb_vorbis.c"
#endif
#ifdef MUSIC_MP3
#define DR_MP3_IMPLEMENTATION
#include "../../../libs/audio_decoders/dr_mp3.h"
#endif
#ifdef MUSIC_FLAC
#define DR_FLAC_IMPLEMENTATION
#include "../../../libs/audio_decoders/dr_flac.h"
#endif

#define OUT_RATE 32000
#define CHANNELS 3     /* the game's stream channels (sound.c) */
#define BLOCK 1024     /* frames decoded at a time */
#define MAX_FILES 512

/* ---- Settings ---- */

static int* sShuffle;
static int* sCustom;
static int* sShuffleCustom;
/* the mixer's (audio thread): declared here, on the game thread, before anything is bound */
static int* volatile sMusicVolume;
static int* volatile sMusicMute;

static void declare(void) {
    if (sShuffle == NULL) {
        sShuffle = portSettingsDeclare("music_shuffle", 0, 0, 1);
        sCustom = portSettingsDeclare("custom_music", 1, 0, 1);
        sShuffleCustom = portSettingsDeclare("music_shuffle_custom", 0, 0, 1);
        sMusicMute = portSettingsDeclare("music_mute", 0, 0, 1);
        sMusicVolume = portSettingsDeclare("music_volume", 100, 0, 100);
    }
}

/* ---- Files ---- */

static FILE* openFile(const char* path) {
#ifdef _WIN32
    wchar_t w[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) <= 0) {
        return NULL;
    }
    return _wfopen(w, L"rb");
#else
    return fopen(path, "rb");
#endif
}

static int extOf(const char* name) {
    static const char* const kExt[] = {".wav", ".ogg", ".mp3", ".flac"};
    const char* dot = strrchr(name, '.');
    int i;
    if (dot == NULL) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
#ifdef _WIN32
        if (_stricmp(dot, kExt[i]) == 0) {
#else
        if (strcasecmp(dot, kExt[i]) == 0) {
#endif
            return i;
        }
    }
    return -1;
}

/* ---- Decoders: interleaved stereo s16 out, mono doubled, past two channels dropped ---- */

typedef struct Decoder {
    void* d;
    int (*read)(void* d, short* out, int frames); /* frames read, 0 at the end */
    int (*seek)(void* d, unsigned frame);
    void (*close)(void* d);
    int rate;
    unsigned length;    /* frames, 0 unknown */
    unsigned loopStart; /* frames */
    unsigned loopEnd;   /* frames, 0: the end */
    unsigned loopLength; /* a LOOPLENGTH tag's, made an end by loopDone */
} Decoder;

/* LOOPSTART=n, LOOPLENGTH=n or LOOPEND=n (in frames), as RPG Maker's and
 * many game rips' Vorbis comments have them */
static void loopTag(Decoder* dec, const char* tag, int len) {
    char key[16], val[24];
    int i, eq = -1;
    unsigned v;
    for (i = 0; i < len; i++) {
        if (tag[i] == '=') {
            eq = i;
            break;
        }
    }
    if (eq <= 0 || eq >= (int)sizeof(key) || len - eq - 1 >= (int)sizeof(val)) {
        return;
    }
    for (i = 0; i < eq; i++) {
        key[i] = (char)(tag[i] >= 'a' && tag[i] <= 'z' ? tag[i] - 32 : tag[i]);
    }
    key[eq] = '\0';
    memcpy(val, tag + eq + 1, (size_t)(len - eq - 1));
    val[len - eq - 1] = '\0';
    v = (unsigned)strtoul(val, NULL, 10);
    if (strcmp(key, "LOOPSTART") == 0) {
        dec->loopStart = v;
    } else if (strcmp(key, "LOOPLENGTH") == 0) {
        dec->loopLength = v;
    } else if (strcmp(key, "LOOPEND") == 0) {
        dec->loopEnd = v;
    }
}

static void loopDone(Decoder* dec) {
    if (dec->loopLength != 0) {
        dec->loopEnd = dec->loopStart + dec->loopLength;
    }
    if (dec->length != 0 && (dec->loopEnd > dec->length || dec->loopStart >= dec->length)) {
        dec->loopStart = 0;
        dec->loopEnd = 0;
    }
    if (dec->loopEnd != 0 && dec->loopEnd <= dec->loopStart) {
        dec->loopStart = 0;
        dec->loopEnd = 0;
    }
}

/* WAV: PCM 8/16/24/32-bit or 32-bit float, its sampler chunk's first loop */
typedef struct Wav {
    FILE* f;
    long data;      /* offset of the samples */
    unsigned bytes; /* their size */
    int channels, bits, isFloat, frameBytes;
    unsigned pos;
    unsigned char buf[BLOCK * 32];
} Wav;

static unsigned le32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static int wavRead(void* d, short* out, int frames) {
    Wav* w = d;
    unsigned left = w->bytes / w->frameBytes - w->pos;
    int n, i, c;
    if ((unsigned)frames > left) {
        frames = (int)left;
    }
    if (frames > (int)(sizeof(w->buf) / w->frameBytes)) {
        frames = (int)(sizeof(w->buf) / w->frameBytes);
    }
    n = (int)fread(w->buf, (size_t)w->frameBytes, (size_t)frames, w->f);
    for (i = 0; i < n; i++) {
        const unsigned char* p = w->buf + i * w->frameBytes;
        int s[2];
        for (c = 0; c < 2; c++) {
            const unsigned char* q = p + (c < w->channels ? c : 0) * (w->bits / 8);
            if (w->isFloat) {
                union {
                    unsigned u;
                    float f;
                } u;
                float v;
                u.u = le32(q);
                v = u.f * 32767.f;
                s[c] = v > 32767.f ? 32767 : v < -32768.f ? -32768 : (int)v;
            } else if (w->bits == 8) {
                s[c] = (q[0] - 128) << 8;
            } else if (w->bits == 16) {
                s[c] = (short)(q[0] | (q[1] << 8));
            } else if (w->bits == 24) {
                s[c] = (short)(q[1] | (q[2] << 8));
            } else {
                s[c] = (short)(q[2] | (q[3] << 8));
            }
        }
        out[i * 2] = (short)s[0];
        out[i * 2 + 1] = (short)s[1];
    }
    w->pos += (unsigned)n;
    return n;
}

static int wavSeek(void* d, unsigned frame) {
    Wav* w = d;
    if (frame > w->bytes / w->frameBytes) {
        frame = w->bytes / w->frameBytes;
    }
    w->pos = frame;
    return fseek(w->f, w->data + (long)frame * w->frameBytes, SEEK_SET) == 0;
}

static void wavClose(void* d) {
    fclose(((Wav*)d)->f);
    free(d);
}

static int wavOpen(const char* path, Decoder* dec) {
    unsigned char h[12], ch[8], fmt[40], smpl[60];
    Wav* w;
    FILE* f = openFile(path);
    int haveFmt = 0, format = 0;
    if (f == NULL) {
        return 0;
    }
    w = calloc(1, sizeof(Wav));
    if (w == NULL || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
        free(w);
        fclose(f);
        return 0;
    }
    w->f = f;
    while (fread(ch, 1, 8, f) == 8) {
        const unsigned size = le32(ch + 4);
        const long next = ftell(f) + (long)size + (size & 1);
        if (memcmp(ch, "fmt ", 4) == 0 && size >= 16) {
            memset(fmt, 0, sizeof(fmt));
            if (fread(fmt, 1, size < sizeof(fmt) ? size : sizeof(fmt), f) < 16) {
                break;
            }
            format = fmt[0] | (fmt[1] << 8);
            if (format == 0xFFFE && size >= 40) {
                format = fmt[24] | (fmt[25] << 8); /* WAVE_FORMAT_EXTENSIBLE: its sub-format */
            }
            w->channels = fmt[2] | (fmt[3] << 8);
            dec->rate = (int)le32(fmt + 4);
            w->bits = fmt[14] | (fmt[15] << 8);
            haveFmt = 1;
        } else if (memcmp(ch, "smpl", 4) == 0 && size >= 60) {
            if (fread(smpl, 1, 60, f) == 60 && le32(smpl + 28) > 0) {
                dec->loopStart = le32(smpl + 44);
                dec->loopEnd = le32(smpl + 48) + 1; /* its last frame is in the loop */
            }
        } else if (memcmp(ch, "data", 4) == 0) {
            w->data = ftell(f);
            w->bytes = size;
        }
        if (fseek(f, next, SEEK_SET) != 0) {
            break;
        }
    }
    w->isFloat = format == 3;
    if (!haveFmt || w->data == 0 || w->channels < 1 || dec->rate < 4000 || dec->rate > 192000 ||
        !(format == 1 || (format == 3 && w->bits == 32)) ||
        !(w->bits == 8 || w->bits == 16 || w->bits == 24 || w->bits == 32)) {
        wavClose(w);
        return 0;
    }
    w->frameBytes = w->channels * w->bits / 8;
    dec->length = w->bytes / w->frameBytes;
    dec->d = w;
    dec->read = wavRead;
    dec->seek = wavSeek;
    dec->close = wavClose;
    wavSeek(w, 0);
    return 1;
}

#ifdef MUSIC_OGG
typedef struct Ogg {
    stb_vorbis* v;
    int channels;
    short buf[BLOCK * 8];
} Ogg;

static int oggRead(void* d, short* out, int frames) {
    Ogg* o = d;
    int n, i;
    if (o->channels <= 2) {
        n = stb_vorbis_get_samples_short_interleaved(o->v, 2, out, frames * 2);
        return n;
    }
    /* stb_vorbis allows up to 16 channels; buf holds BLOCK * 8 shorts, so a 9-16 channel file
       decodes fewer frames per call instead of writing past it. */
    if (frames > BLOCK * 8 / o->channels) {
        frames = BLOCK * 8 / o->channels;
    }
    n = stb_vorbis_get_samples_short_interleaved(o->v, o->channels, o->buf, frames * o->channels);
    for (i = 0; i < n; i++) {
        out[i * 2] = o->buf[i * o->channels];
        out[i * 2 + 1] = o->buf[i * o->channels + 1];
    }
    return n;
}

static int oggSeek(void* d, unsigned frame) {
    return stb_vorbis_seek(((Ogg*)d)->v, frame);
}

static void oggClose(void* d) {
    stb_vorbis_close(((Ogg*)d)->v);
    free(d);
}

static int oggOpen(const char* path, Decoder* dec) {
    FILE* f = openFile(path);
    int err = 0, i;
    stb_vorbis_info info;
    stb_vorbis_comment com;
    Ogg* o;
    if (f == NULL) {
        return 0;
    }
    o = calloc(1, sizeof(Ogg));
    if (o == NULL || (o->v = stb_vorbis_open_file(f, 1, &err, NULL)) == NULL) {
        if (o == NULL) {
            fclose(f);
        }
        free(o);
        return 0;
    }
    info = stb_vorbis_get_info(o->v);
    o->channels = info.channels;
    dec->rate = (int)info.sample_rate;
    dec->length = stb_vorbis_stream_length_in_samples(o->v);
    com = stb_vorbis_get_comment(o->v);
    for (i = 0; i < com.comment_list_length; i++) {
        loopTag(dec, com.comment_list[i], (int)strlen(com.comment_list[i]));
    }
    dec->d = o;
    dec->read = oggRead;
    dec->seek = oggSeek;
    dec->close = oggClose;
    return 1;
}
#endif

#ifdef MUSIC_MP3
static int mp3Read(void* d, short* out, int frames) {
    drmp3* m = d;
    if (m->channels == 1) {
        int n = (int)drmp3_read_pcm_frames_s16(m, (drmp3_uint64)frames, out), i;
        for (i = n - 1; i >= 0; i--) {
            out[i * 2 + 1] = out[i * 2] = out[i];
        }
        return n;
    }
    return (int)drmp3_read_pcm_frames_s16(m, (drmp3_uint64)frames, out); /* MP3 has at most two */
}

static int mp3Seek(void* d, unsigned frame) {
    return drmp3_seek_to_pcm_frame((drmp3*)d, frame);
}

static void mp3Close(void* d) {
    drmp3_uninit((drmp3*)d);
    free(d);
}

static int mp3Open(const char* path, Decoder* dec) {
    drmp3* m = calloc(1, sizeof(drmp3));
    int ok;
#ifdef _WIN32
    wchar_t w[1024];
    ok = m != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) > 0 && drmp3_init_file_w(m, w, NULL);
#else
    ok = m != NULL && drmp3_init_file(m, path, NULL);
#endif
    if (!ok) {
        free(m);
        return 0;
    }
    dec->rate = (int)m->sampleRate;
    dec->length = 0;
    dec->d = m;
    dec->read = mp3Read;
    dec->seek = mp3Seek;
    dec->close = mp3Close;
    return 1;
}
#endif

#ifdef MUSIC_FLAC
typedef struct Flac {
    drflac* f;
    short buf[BLOCK * 8];
} Flac;

static void flacMeta(void* user, drflac_metadata* meta) {
    if (meta->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
        drflac_vorbis_comment_iterator it;
        drflac_uint32 len;
        const char* c;
        drflac_init_vorbis_comment_iterator(&it, meta->data.vorbis_comment.commentCount,
                                            meta->data.vorbis_comment.pComments);
        while ((c = drflac_next_vorbis_comment(&it, &len)) != NULL) {
            loopTag((Decoder*)user, c, (int)len);
        }
    }
}

static int flacRead(void* d, short* out, int frames) {
    Flac* fl = d;
    const int ch = fl->f->channels;
    int n, i;
    if (ch == 2) {
        return (int)drflac_read_pcm_frames_s16(fl->f, (drflac_uint64)frames, out);
    }
    if (frames > BLOCK) {
        frames = BLOCK;
    }
    n = (int)drflac_read_pcm_frames_s16(fl->f, (drflac_uint64)frames, fl->buf);
    for (i = 0; i < n; i++) {
        out[i * 2] = fl->buf[i * ch];
        out[i * 2 + 1] = fl->buf[i * ch + (ch > 1 ? 1 : 0)];
    }
    return n;
}

static int flacSeek(void* d, unsigned frame) {
    return drflac_seek_to_pcm_frame(((Flac*)d)->f, frame);
}

static void flacClose(void* d) {
    drflac_close(((Flac*)d)->f);
    free(d);
}

static int flacOpen(const char* path, Decoder* dec) {
    Flac* fl = calloc(1, sizeof(Flac));
    if (fl == NULL) {
        return 0;
    }
#ifdef _WIN32
    {
        wchar_t w[1024];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) > 0) {
            fl->f = drflac_open_file_with_metadata_w(w, flacMeta, dec, NULL);
        }
    }
#else
    fl->f = drflac_open_file_with_metadata(path, flacMeta, dec, NULL);
#endif
    if (fl->f == NULL) {
        free(fl);
        return 0;
    }
    dec->rate = (int)fl->f->sampleRate;
    dec->length = (unsigned)fl->f->totalPCMFrameCount;
    dec->d = fl;
    dec->read = flacRead;
    dec->seek = flacSeek;
    dec->close = flacClose;
    return 1;
}
#endif

int portMusicFormats(void) {
    int f = 0;
#ifdef MUSIC_OGG
    f |= 1;
#endif
#ifdef MUSIC_MP3
    f |= 2;
#endif
#ifdef MUSIC_FLAC
    f |= 4;
#endif
    return f;
}

static int decoderOpen(const char* path, Decoder* dec) {
    int ok = 0;
    memset(dec, 0, sizeof(*dec));
    switch (extOf(path)) {
    case 0:
        ok = wavOpen(path, dec);
        break;
#ifdef MUSIC_OGG
    case 1:
        ok = oggOpen(path, dec);
        break;
#endif
#ifdef MUSIC_MP3
    case 2:
        ok = mp3Open(path, dec);
        break;
#endif
#ifdef MUSIC_FLAC
    case 3:
        ok = flacOpen(path, dec);
        break;
#endif
    default:
        break;
    }
    if (ok && (dec->rate < 4000 || dec->rate > 192000)) {
        dec->close(dec->d);
        ok = 0;
    }
    if (ok) {
        loopDone(dec);
    }
    return ok;
}

/* ---- A file playing: decoded a block at a time, looped, resampled to 32 kHz ---- */

typedef struct Source {
    Decoder dec;
    int loop;          /* 0: plays once (a jingle's file) */
    int ended;
    unsigned pos;      /* the decoder's frame */
    short buf[BLOCK * 2];
    int have, at;      /* frames in buf, the next one */
    short cur[2], next[2];
    double frac, step; /* between cur and next; input frames per output frame */
    char name[64];
} Source;

static int sourceFrame(Source* s, short* out) {
    if (s->at >= s->have) {
        int want = BLOCK, n, tries;
        s->at = s->have = 0;
        for (tries = 0; tries < 2 && !s->ended; tries++) {
            want = BLOCK;
            if (s->loop && s->dec.loopEnd != 0) {
                want = s->dec.loopEnd > s->pos ? (int)(s->dec.loopEnd - s->pos < BLOCK ? s->dec.loopEnd - s->pos : BLOCK)
                                               : 0;
            }
            n = want > 0 ? s->dec.read(s->dec.d, s->buf, want) : 0;
            if (n > 0) {
                s->pos += (unsigned)n;
                s->have = n;
                break;
            }
            if (!s->loop || !s->dec.seek(s->dec.d, s->dec.loopStart)) {
                s->ended = 1;
                break;
            }
            s->pos = s->dec.loopStart;
        }
        if (s->have == 0) {
            s->ended = 1;
            out[0] = out[1] = 0;
            return 0;
        }
    }
    out[0] = s->buf[s->at * 2];
    out[1] = s->buf[s->at * 2 + 1];
    s->at++;
    return 1;
}

static Source* sourceOpen(const char* path, const char* name, int loop) {
    Source* s = calloc(1, sizeof(Source));
    if (s == NULL) {
        return NULL;
    }
    if (!decoderOpen(path, &s->dec)) {
        free(s);
        return NULL;
    }
    s->loop = loop;
    s->step = (double)s->dec.rate / OUT_RATE;
    snprintf(s->name, sizeof(s->name), "%s", name);
    sourceFrame(s, s->cur);
    sourceFrame(s, s->next);
    return s;
}

/* The Paper Mario 64 soundtrack (port/include/port/pm64_music.h): its
 * sequence player as a decoder that never seeks (a song loops by itself;
 * a jingle's ends) */
static int pm64Read(void* d, short* out, int frames) {
    return portPm64SongRender((Pm64Song*)d, out, frames);
}

static int pm64Seek(void* d, unsigned frame) {
    (void)d;
    (void)frame;
    return 0;
}

static void pm64Close(void* d) {
    portPm64SongClose((Pm64Song*)d);
}

static Source* pm64SourceOpen(int song) {
    Source* s;
    Pm64Song* p = portPm64SongOpen(song);
    if (p == NULL || (s = calloc(1, sizeof(Source))) == NULL) {
        portPm64SongClose(p);
        return NULL;
    }
    s->dec.d = p;
    s->dec.read = pm64Read;
    s->dec.seek = pm64Seek;
    s->dec.close = pm64Close;
    s->dec.rate = OUT_RATE;
    s->loop = 1;
    s->step = 1.0;
    snprintf(s->name, sizeof(s->name), "Paper Mario 64: %s", portPm64SongName(song));
    {
        const char* log = getenv("TTYD_PM64_LOG"); /* (tests) */
        if (log != NULL && log[0] != '\0' && log[0] != '0') {
            printf("[pm64] music: %s\n", s->name);
            fflush(stdout);
        }
    }
    sourceFrame(s, s->cur);
    sourceFrame(s, s->next);
    return s;
}

static void sourceClose(Source* s) {
    if (s != NULL) {
        s->dec.close(s->dec.d);
        free(s);
    }
}

/* ---- The stream channels' files (the game thread binds, the audio thread plays) ---- */

static struct {
    Source* src;
    float volume;  /* the stream's, 0-1; < 0 paused */
    float heard;   /* the volume last mixed (ramped to volume across a block) */
    int started;   /* the stream has said its volume since it started */
    int serial;    /* the binding's (gPortMusicBinding), 0 none: gPortMusicBound[chan] while they agree */
} sChan[CHANNELS];

static int sNext = -1;       /* portMusicNext's track */
static char sNextFile[512];  /* or the shuffle's file for it */
static char sFolder[512];

/* Each binding's serial (never reused): gPortMusicBound[chan] says which one a channel has, and
   gPortMusicBinding[chan] (sound.c, in the state with it) what it is, to open again after a load */
static int sSerial;

void portMusicMix(short* out, unsigned frames) {
    int* const volume = sMusicVolume;
    int* const mute = sMusicMute;
    float music;
    unsigned i;
    int c;
    if (volume == NULL || mute == NULL) {
        return; /* nothing bound yet (declare runs first) */
    }
    music = *mute ? 0.f : portVolumeGain(*volume);
    LOCK();
    for (c = 0; c < CHANNELS; c++) {
        Source* s = sChan[c].src;
        float from, to, step;
        if (s == NULL || s->ended || !sChan[c].started || sChan[c].volume < 0.f) {
            continue; /* not begun, or paused: the file waits */
        }
        from = sChan[c].heard;
        to = sChan[c].volume * music;
        step = (to - from) / (float)frames;
        for (i = 0; i < frames; i++) {
            const float g = from + step * (float)i;
            int k;
            for (k = 0; k < 2; k++) {
                const float v = (float)s->cur[k] + (float)(s->next[k] - s->cur[k]) * (float)s->frac;
                int m = out[i * 2 + k] + (int)(v * g);
                out[i * 2 + k] = (short)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
            }
            s->frac += s->step;
            while (s->frac >= 1.0) {
                s->frac -= 1.0;
                s->cur[0] = s->next[0];
                s->cur[1] = s->next[1];
                sourceFrame(s, s->next);
            }
        }
        sChan[c].heard = to;
    }
    UNLOCK();
}

static void bindAs(int chan, Source* s, int serial) {
    Source* old;
    LOCK();
    old = sChan[chan].src;
    sChan[chan].src = s;
    sChan[chan].volume = -1.f;
    sChan[chan].heard = 0.f;
    sChan[chan].started = 0;
    sChan[chan].serial = s != NULL ? serial : 0;
    UNLOCK();
    gPortMusicBound[chan] = sChan[chan].serial;
    sourceClose(old);
}

/* binds s (opened from path, or Paper Mario 64 song when song >= 0), remembered for a restore */
static void bind(int chan, Source* s, const char* path, int song, int loop) {
    int serial = 0;
    if (s != NULL) {
        PortMusicBinding* b = &gPortMusicBinding[chan];
        if (++sSerial <= 0) {
            sSerial = 1;
        }
        serial = sSerial;
        b->song = song;
        b->loop = loop;
        snprintf(b->path, sizeof(b->path), "%s", path != NULL ? path : "");
        snprintf(b->name, sizeof(b->name), "%s", s->name);
    }
    bindAs(chan, s, serial);
}

void portMusicSync(void) {
    int c;
    for (c = 0; c < CHANNELS; c++) {
        const int want = gPortMusicBound[c];
        Source* s = NULL;
        if (want == sChan[c].serial) {
            continue;
        }
        if (want > 0) {
            /* (the state's own binding, however old: restored with gPortMusicBound) */
            PortMusicBinding* b = &gPortMusicBinding[c];
            b->path[sizeof(b->path) - 1] = '\0';
            b->name[sizeof(b->name) - 1] = '\0';
            s = b->song >= 0 ? pm64SourceOpen(b->song)
                : b->path[0] != '\0' ? sourceOpen(b->path, b->name, b->loop) : NULL;
        }
        printf("[music] channel %d restored: %s\n", c, s != NULL ? s->name : "the game's stream");
        fflush(stdout);
        /* (a file no longer there: the channel stays quiet rather than playing what the state didn't have) */
        bindAs(c, s, want);
    }
}

void portMusicStreamStop(int chan) {
    if (chan >= 0 && chan < CHANNELS && (sChan[chan].src != NULL || gPortMusicBound[chan] != 0)) {
        bindAs(chan, NULL, 0);
    }
}

void portMusicStreamVolume(int chan, float volume) {
    if (chan >= 0 && chan < CHANNELS && sChan[chan].src != NULL) {
        LOCK();
        if (!sChan[chan].started && volume >= 0.f) {
            sChan[chan].started = 1;
            sChan[chan].heard = volume; /* the stream's own fade-in is in volume */
        }
        sChan[chan].volume = volume;
        UNLOCK();
    }
}

const char* portMusicPlaying(void) {
    int c;
    for (c = 0; c < CHANNELS; c++) {
        if (sChan[c].src != NULL && !sChan[c].src->ended && sChan[c].started) {
            return sChan[c].src->name;
        }
    }
    return NULL;
}

/* ---- The tracks, by kind ---- */

enum { KIND_NONE, KIND_FIELD, KIND_BATTLE, KIND_JINGLE };

static int prefix(const char* s, const char* p) {
    return strncmp(s, p, strlen(p)) == 0;
}

static int kindOf(const char* name) {
    if (prefix(name, "BGM_STG") || prefix(name, "BGM_M_STG")) {
        return KIND_FIELD;
    }
    if (prefix(name, "BGM_ZAKO_BATTLE") || prefix(name, "BGM_KOBOSS_BATTLE") || prefix(name, "BGM_CHUBOSS_BATTLE") ||
        prefix(name, "BGM_BOSS_")) {
        return KIND_BATTLE;
    }
    if (prefix(name, "BGM_FF_") || prefix(name, "BGM_BATTLE_")) {
        return KIND_JINGLE;
    }
    return KIND_NONE;
}

/* ---- The music folder ---- */

typedef struct File {
    char path[512];
    char name[64]; /* without its extension */
    int track;     /* the track it is named after, or -1 */
    int kind;      /* KIND_FIELD or KIND_BATTLE for the shuffle, else KIND_NONE */
} File;

static File* sFiles;
static int sFileCount;
static int sRoomCount; /* (the shuffle's, below: its choices index sFiles) */
static int sScanned;

static int trackNamed(const char* name) {
    int i;
    for (i = 1; i < portBgmCount(); i++) {
        const char* n = portBgmName(i);
#ifdef _WIN32
        if (n[0] != '\0' && _stricmp(n, name) == 0) {
#else
        if (n[0] != '\0' && strcasecmp(n, name) == 0) {
#endif
            return i;
        }
    }
    return -1;
}

static void addFile(const char* dir, const char* file, int battleDir) {
    File* f;
    const char* dot = strrchr(file, '.');
    size_t len = dot != NULL ? (size_t)(dot - file) : strlen(file);
    if (extOf(file) < 0 || sFileCount >= MAX_FILES || len == 0 || len >= sizeof(f->name)) {
        return;
    }
    if (extOf(file) > 0 && !(portMusicFormats() & (1 << (extOf(file) - 1)))) {
        return; /* no decoder for it in this build */
    }
    f = &sFiles[sFileCount];
    snprintf(f->path, sizeof(f->path), "%s/%s", dir, file);
    memcpy(f->name, file, len);
    f->name[len] = '\0';
    f->track = battleDir ? -1 : trackNamed(f->name);
    if (f->track > 0) {
        const int k = kindOf(portBgmName(f->track));
        f->kind = k == KIND_FIELD || k == KIND_BATTLE ? k : KIND_NONE;
    } else {
        f->kind = battleDir ? KIND_BATTLE : KIND_FIELD;
    }
    sFileCount++;
}

static void scanDir(const char* dir, int battleDir) {
#ifdef _WIN32
    wchar_t w[600];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s/*", dir);
    if (MultiByteToWideChar(CP_UTF8, 0, pattern, -1, w, 600) <= 0) {
        return;
    }
    h = FindFirstFileW(w, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        char name[260];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL) > 0) {
            addFile(dir, name, battleDir);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            addFile(dir, e->d_name, battleDir);
        }
    }
    closedir(d);
#endif
}

static void makeDir(const char* path) {
#ifdef _WIN32
    wchar_t w[600];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 600) > 0) {
        CreateDirectoryW(w, NULL);
    }
#else
    mkdir(path, 0755);
#endif
}

/* music/tracks.txt: the names a file can take, by kind */
static void writeTrackList(void) {
    static const char* const kHeads[] = {"Other music (events and scenes)", "Field music (rooms and areas)",
                                         "Battle music", "Jingles and fanfares (play once)"};
    char path[600];
    FILE* f;
    int k, i;
    snprintf(path, sizeof(path), "%s/tracks.txt", sFolder);
    f = openFile(path);
    if (f != NULL) {
        fclose(f);
        return;
    }
#ifdef _WIN32
    {
        wchar_t w[600];
        f = MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 600) > 0 ? _wfopen(w, L"w") : NULL;
    }
#else
    f = fopen(path, "w");
#endif
    if (f == NULL) {
        return;
    }
    fprintf(f, "Paper Cut: your music in place of the game's (Settings > Audio > Soundtrack: Your music).\n"
               "Name a file after the track it replaces, as below: BGM_STG1_NOK1.ogg plays in Petalburg.\n"
               "WAV always; OGG, MP3 and FLAC where this build has their decoders.\n"
               "A file loops at its LOOPSTART and LOOPLENGTH (or LOOPEND) tags, in samples,\n"
               "or a WAV's sampler loop; otherwise the whole file loops. Jingles play once.\n"
               "Soundtrack \"Yours and the game's, shuffled\" also picks files with other names\n"
               "(as field music; in a folder named battle, as battle music).\n"
               "Names starting BGM_M_ are a place's music as heard walking between its rooms.\n");
    for (k = 1; k <= 4; k++) {
        const int kind = k % 4;
        fprintf(f, "\n%s:\n", kHeads[kind]);
        for (i = 1; i < portBgmCount(); i++) {
            const char* n = portBgmName(i);
            if (n[0] != '\0' && kindOf(n) == kind) {
                fprintf(f, "%s\n", n);
            }
        }
    }
    fclose(f);
}

int portMusicRescan(void) {
    char battle[600];
    if (sFolder[0] == '\0') {
        snprintf(sFolder, sizeof(sFolder), "%s/music", portPracticeUserDir());
    }
    if (sFiles == NULL) {
        sFiles = calloc(MAX_FILES, sizeof(File));
        if (sFiles == NULL) {
            return 0;
        }
    }
    sFileCount = 0;
    sRoomCount = 0; /* the room's shuffle choices index sFiles: picked again from the new list */
    makeDir(sFolder);
    writeTrackList();
    scanDir(sFolder, 0);
    snprintf(battle, sizeof(battle), "%s/battle", sFolder);
    scanDir(battle, 1);
    sScanned = 1;
    return sFileCount;
}

const char* portMusicFolder(void) {
    if (!sScanned) {
        portMusicRescan();
    }
    return sFolder;
}

static const File* fileFor(int track) {
    int i;
    for (i = 0; i < sFileCount; i++) {
        if (sFiles[i].track == track) {
            return &sFiles[i];
        }
    }
    return NULL;
}

/* ---- The shuffle ---- */

static unsigned long long sRand;

/* its own numbers (xorshift64*), never the game's */
static unsigned randBelow(unsigned n) {
    if (sRand == 0) {
        sRand = (unsigned long long)time(NULL) * 0x9E3779B97F4A7C15ull ^ (unsigned long long)clock() ^
                (unsigned long long)(size_t)&sRand;
        if (sRand == 0) {
            sRand = 1;
        }
    }
    sRand ^= sRand >> 12;
    sRand ^= sRand << 25;
    sRand ^= sRand >> 27;
    return (unsigned)(((sRand * 0x2545F4914F6CDD1Dull) >> 32) % n);
}

/* the disc's own tracks of a kind, without the BGM_M_ ones (the same songs) */
static int discPick(int kind, int notThis, int* count) {
    int i, n = 0, pick, at = 0;
    for (i = 1; i < portBgmCount(); i++) {
        const char* name = portBgmName(i);
        n += !prefix(name, "BGM_M_") && kindOf(name) == kind && i != notThis;
    }
    *count = n;
    if (n == 0) {
        return -1;
    }
    pick = (int)randBelow((unsigned)n);
    for (i = 1; i < portBgmCount(); i++) {
        const char* name = portBgmName(i);
        if (!prefix(name, "BGM_M_") && kindOf(name) == kind && i != notThis && at++ == pick) {
            return i;
        }
    }
    return -1;
}

/* The room's choices: what each of its tracks became (the disc's track, or a file). */
static struct {
    int asked;
    int disc;
    int file; /* index in sFiles, -1 none */
} sRoom[16];
static char sRoomMap[16];

int portMusicPick(int id, int ch) {
    const char* name;
    int kind, i, discCount = 0, fileCount = 0, disc, r;
    declare();
    sNext = -1;
    sNextFile[0] = '\0';
    if (!sScanned) {
        portMusicRescan();
    }
    if (strncmp(sRoomMap, gp->currentMapName, sizeof(sRoomMap)) != 0) {
        memcpy(sRoomMap, gp->currentMapName, sizeof(sRoomMap));
        sRoomMap[sizeof(sRoomMap) - 1] = '\0';
        sRoomCount = 0;
        if (*sCustom) {
            portMusicRescan(); /* files added while playing are found at the next room */
        }
    }
    if (!portJukeboxCalling() && id > 0) {
        /* the battle theme picked for this area (postgame_extra.h): before the shuffle, which leaves it be */
        const int themed = portPgBattleTheme(id);
        if (themed != id) {
            return themed;
        }
    }
    if (!*sShuffle || portJukeboxCalling() || id <= 0 || id >= portBgmCount()) {
        return id;
    }
    name = portBgmName(id);
    kind = kindOf(name);
    if (kind != KIND_FIELD && kind != KIND_BATTLE) {
        return id; /* events, scenes, jingles and fanfares are left alone */
    }
    for (i = 0; i < sRoomCount; i++) {
        if (sRoom[i].asked == id) {
            if (sRoom[i].file >= 0) {
                snprintf(sNextFile, sizeof(sNextFile), "%s", sFiles[sRoom[i].file].path);
            }
            return sRoom[i].disc;
        }
    }
    disc = discPick(kind, id, &discCount);
    if (*sShuffleCustom && *sCustom) {
        for (i = 0; i < sFileCount; i++) {
            fileCount += sFiles[i].kind == kind;
        }
    }
    r = discCount + fileCount > 0 ? (int)randBelow((unsigned)(discCount + fileCount)) : 0;
    if (sRoomCount == (int)(sizeof(sRoom) / sizeof(sRoom[0]))) {
        sRoomCount = 0;
    }
    sRoom[sRoomCount].asked = id;
    sRoom[sRoomCount].disc = disc > 0 ? disc : id;
    sRoom[sRoomCount].file = -1;
    if (r >= discCount && fileCount > 0) {
        int at = r - discCount;
        for (i = 0; i < sFileCount; i++) {
            if (sFiles[i].kind == kind && at-- == 0) {
                sRoom[sRoomCount].disc = id; /* the game's own stream runs under the file */
                sRoom[sRoomCount].file = i;
                snprintf(sNextFile, sizeof(sNextFile), "%s", sFiles[i].path);
                break;
            }
        }
    }
    printf("[music] shuffle: %s on channel %d in %s plays as %s\n", name, ch, sRoomMap,
           sRoom[sRoomCount].file >= 0 ? sFiles[sRoom[sRoomCount].file].name : portBgmName(sRoom[sRoomCount].disc));
    fflush(stdout);
    return sRoom[sRoomCount++].disc;
}

/* a BGM channel that plays: a track on it, not fading out or held silent (psndBGMOff's PSND_FADE_OUT, _HOLD) */
static int bgmAudible(int ch) {
    const PaperSoundBGM* b = &psbgm[ch];
    return b->listId != 0xFFFFFFFFu && b->fade.state != 2 && b->fade.state != 4;
}

void portMusicBattleCheck(const char* bgmName, int battleFlags) {
    const char* const fallback = (battleFlags & NPC_BATTLE_NO_ESCAPE) ? "BGM_CHUBOSS_BATTLE1" : "BGM_ZAKO_BATTLE1";
    int ok;
    portPm64MusicBattleStarted();
    if (bgmAudible(1) || bgmAudible(0)) {
        printf("[music] battle music: %s\n",
               bgmAudible(1) ? portBgmName((int)psbgm[1].listId) : "the field's (kept)");
        fflush(stdout);
        return;
    }
    ok = psndBGMOn(PSND_CH(1), (char*)fallback);
    printf("[music] battle with no music (asked for %s): %s in its place%s\n",
           bgmName == NULL ? "none" : SFX_NAME_IS_ID(bgmName) ? "an id" : bgmName, fallback,
           ok ? "" : ", which didn't start either");
    fflush(stdout);
}

void portMusicNext(int id) {
    sNext = id;
}

int portMusicStreamStart(int chan, int cat) {
    char path[512];
    const char* track;
    const File* f;
    Source* s = NULL;
    int next = sNext, song = -1, loop = 1;
    declare();
    sNext = -1;
    snprintf(path, sizeof(path), "%s", sNextFile);
    sNextFile[0] = '\0';
    if (chan < 0 || chan >= CHANNELS) {
        return cat;
    }
    if (next > 0 && next < portBgmCount() && *sCustom) {
        track = portBgmName(next);
        if (path[0] == '\0' && (f = fileFor(next)) != NULL) {
            snprintf(path, sizeof(path), "%s", f->path);
        }
        if (path[0] != '\0') {
            const char* base = strrchr(path, '/');
            loop = kindOf(track) != KIND_JINGLE;
            s = sourceOpen(path, base != NULL ? base + 1 : path, loop);
            if (s != NULL) {
                printf("[music] %s: %s (%d Hz%s)\n", track, s->name, s->dec.rate,
                       s->dec.loopEnd != 0 ? ", its own loop" : "");
            } else {
                printf("[music] %s: %s can't be read\n", track, path);
            }
            fflush(stdout);
        }
    }
    if (s == NULL && next > 0 && next < portBgmCount()) {
        song = portPm64MusicPick(portBgmName(next), gp->currentMapName);
        if (song >= 0 && (s = pm64SourceOpen(song)) != NULL) {
            printf("[music] %s: %s\n", portBgmName(next), s->name);
            fflush(stdout);
        }
    }
    bind(chan, s, path, s != NULL && song >= 0 ? song : -1, loop);
    return s != NULL ? PORT_MIX_SILENT : cat;
}
