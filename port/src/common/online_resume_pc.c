/* Online play's session kept on disk while it plays (port/include/port/statesync.h's SsResumeFile;
 * docs/STATE_SYNC.md "Leaving and coming back"): <user>/online_resume.txt, so a game that closes
 * without a Leave (a crash, a phone's system ending the app in the background, the battery) can be back
 * in the same room, in the same seat, on its next start. Written whole through a temporary file and a
 * rename (a crash mid-write leaves the last one), gone on a Leave. Never joined by itself: the Multiplayer
 * page offers it (statesync_pc.c portStateSyncResumeOffer) for 15 minutes.
 *
 * It holds the room's whole code (as an invite link does), the session id, the relay's ticket for this
 * game's place, its player number and seat, the host's name, and a host's players' session ids and seats:
 * nothing of anyone's computer. One "key value" a line. */
#include "port/settings.h"
#include "port/statesync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define RESUME_MAX_S (15 * 60) /* offered this long after it was last written */
#define RESUME_MAGIC "papercut-online-resume 1"

static void pathOf(char* out, size_t cap, const char* ext) {
    snprintf(out, cap, "%s/online_resume.txt%s", portSettingsUserPath(), ext);
}

#ifdef _WIN32
static int widen(const char* path, wchar_t* w, int cap) {
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, w, cap) > 0;
}
#endif

static FILE* openPath(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t w[1024], m[8];
    if (!widen(path, w, 1024) || !widen(mode, m, 8)) {
        return NULL;
    }
    return _wfopen(w, m);
#else
    return fopen(path, mode);
#endif
}

static int replaceFile(const char* from, const char* to) {
#ifdef _WIN32
    wchar_t wf[1024], wt[1024];
    return widen(from, wf, 1024) && widen(to, wt, 1024) && MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING);
#else
    return rename(from, to) == 0;
#endif
}

static void removePath(const char* path) {
#ifdef _WIN32
    wchar_t w[1024];
    if (widen(path, w, 1024)) {
        DeleteFileW(w);
    }
#else
    remove(path);
#endif
}

/* a text kept on one line (a name is cleaned already: no control characters; this keeps it so) */
static void oneLine(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for (; in != NULL && *in != '\0' && n + 1 < cap; in++) {
        out[n++] = (unsigned char)*in < 0x20 ? ' ' : *in;
    }
    out[n] = '\0';
}

void ssResumeWrite(const SsResumeFile* r) {
    char path[1024], tmp[1040], where[sizeof(r->net.where)], ticket[sizeof(r->net.ticket)], name[sizeof(r->hostName)];
    FILE* f;
    int ok;
    pathOf(path, sizeof(path), "");
    pathOf(tmp, sizeof(tmp), ".tmp");
    f = openPath(tmp, "wb");
    if (f == NULL) {
        return;
    }
    oneLine(r->net.where, where, sizeof(where));
    oneLine(r->net.ticket, ticket, sizeof(ticket));
    oneLine(r->hostName, name, sizeof(name));
    ok = fprintf(f,
                 RESUME_MAGIC "\nat %lld\nhost %d\nroom %d\nport %d\nnonce %016llx\npeer %d\nwatch %d\nplayer %d\n"
                              "seat %d\npeers %016llx %016llx %016llx\nseats %d %d %d %d %d\nwhere %s\nticket %s\n"
                              "name %s\n",
                 (long long)time(NULL), r->net.host, r->net.room, r->net.port, (unsigned long long)r->net.nonce,
                 r->net.asPeer, r->watch, r->player, r->seat, (unsigned long long)r->net.peers[0],
                 (unsigned long long)r->net.peers[1], (unsigned long long)r->net.peers[2], r->lastSeat[1],
                 r->lastSeat[2], r->lastSeat[3], r->lastSeat[4], r->hostSeat, where, ticket, name) > 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || !replaceFile(tmp, path)) {
        removePath(tmp);
    }
}

/* "key rest-of-line": the rest after the key and one space ("" when none) */
static const char* valueOf(const char* line, const char* key) {
    const size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || (line[n] != ' ' && line[n] != '\0')) {
        return NULL;
    }
    return line[n] == ' ' ? line + n + 1 : line + n;
}

long long ssResumeNow(void) {
    return (long long)time(NULL);
}

long long ssResumeMaxS(void) {
    const char* maxEnv = getenv("TTYD_SYNC_RESUME_MAX");
    return maxEnv != NULL && atoi(maxEnv) > 0 ? atoi(maxEnv) : RESUME_MAX_S;
}

int ssResumeRead(SsResumeFile* r) {
    char path[1024], line[512];
    const char* v;
    const long long maxS = ssResumeMaxS();
    long long now;
    FILE* f;
    int magic = 0;
    memset(r, 0, sizeof(*r));
    pathOf(path, sizeof(path), "");
    f = openPath(path, "rb");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        unsigned long long a = 0, b = 0, c = 0;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (!strcmp(line, RESUME_MAGIC)) {
            magic = 1;
        } else if ((v = valueOf(line, "at")) != NULL) {
            r->at = strtoll(v, NULL, 10);
        } else if ((v = valueOf(line, "host")) != NULL) {
            r->net.host = atoi(v) != 0;
        } else if ((v = valueOf(line, "room")) != NULL) {
            r->net.room = atoi(v) != 0;
        } else if ((v = valueOf(line, "port")) != NULL) {
            r->net.port = atoi(v);
        } else if ((v = valueOf(line, "nonce")) != NULL) {
            r->net.nonce = (uint64_t)strtoull(v, NULL, 16);
        } else if ((v = valueOf(line, "peer")) != NULL) {
            r->net.asPeer = atoi(v) != 0;
        } else if ((v = valueOf(line, "watch")) != NULL) {
            r->watch = atoi(v) != 0;
        } else if ((v = valueOf(line, "player")) != NULL) {
            r->player = atoi(v);
        } else if ((v = valueOf(line, "seat")) != NULL) {
            r->seat = atoi(v);
        } else if ((v = valueOf(line, "peers")) != NULL && sscanf(v, "%llx %llx %llx", &a, &b, &c) == 3) {
            r->net.peers[0] = (uint64_t)a;
            r->net.peers[1] = (uint64_t)b;
            r->net.peers[2] = (uint64_t)c;
        } else if ((v = valueOf(line, "seats")) != NULL) {
            int s[5] = {0, 0, 0, 0, 0}, i;
            if (sscanf(v, "%d %d %d %d %d", &s[0], &s[1], &s[2], &s[3], &s[4]) == 5) {
                for (i = 0; i < 4; i++) {
                    r->lastSeat[i + 1] = s[i] >= 0 && s[i] <= 4 ? s[i] : 0;
                }
                r->hostSeat = s[4] >= 0 && s[4] <= 4 ? s[4] : 0;
            }
        } else if ((v = valueOf(line, "where")) != NULL) {
            snprintf(r->net.where, sizeof(r->net.where), "%s", v);
        } else if ((v = valueOf(line, "ticket")) != NULL) {
            snprintf(r->net.ticket, sizeof(r->net.ticket), "%s", v);
        } else if ((v = valueOf(line, "name")) != NULL) {
            snprintf(r->hostName, sizeof(r->hostName), "%s", v);
        }
    }
    fclose(f);
    now = (long long)time(NULL);
    if (!magic || r->net.nonce == 0 || (!r->net.host && r->net.where[0] == '\0') ||
        (r->net.room && strlen(r->net.where) != 8) || r->player < 0 || r->player > SS_PLAYERS || r->seat < 0 ||
        r->seat > 4) {
        return 0; /* (not one of these, or half of one) */
    }
    if (r->at > now + 60 || now - r->at > maxS) {
        return 0; /* (too old to come back to: the others have long gone on) */
    }
    return 1;
}

void ssResumeDelete(void) {
    char path[1024];
    pathOf(path, sizeof(path), "");
    removePath(path);
}
