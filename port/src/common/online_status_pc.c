/* The connection, as the player sees it (port/include/port/online_status.h): the join's steps, the link's
 * quality, and the diagnostics for a report.
 *
 * The join: a guest's session (ssNetBegin) goes through five steps, each told here as it's done
 * (statesync_net_pc.c: the relay's room or the host's address reached, the room's key, the hellos; the last
 * two from the game side's ssJoinWorldStep: the host's whole progress, this game in the host's room). Each is
 * logged with its time ("join: step 2 key done in 120 ms (total 460 ms)"), and the whole timeline once it's
 * through. Each step has a time of its own (STEP_MS; $TTYD_JOIN_TIMEOUT_MS, tests: all the same); past it the
 * step is "taking long" (logged once), and a window says which step and what to do: Keep waiting (its time
 * again), Copy diagnostics, Leave the room. A join that fails (refused, a wrong code) says the step too, with
 * Try again, Copy diagnostics and Back, and Check for updates first when the friend's Paper Cut is newer. A
 * host that isn't in a game yet (the title, its files: the room waits for its Play or Party...) is no step's
 * time: "waiting for the host". The window is never up over the Start menu or the title's pages (they show
 * the same in their status), nor with online_end_pc.c's (the online service unreachable is its own).
 * Tests: $TTYD_JOIN_PICK=wait|copy|leave|retry|update, taken a second after the window is up (the headless
 * build has no window: its choice is made all the same, and "copy" logs the diagnostics' lines).
 *
 * The quality: each other player's round trips (their pings' answers) over the last 10 seconds, the slowest
 * tenth (one late answer doesn't make a link poor, a steady half second does), and how long nothing has come
 * from them (2.5 s quiet: poor). Good under 100 ms, fair under 250, else poor; a change holds 2 seconds before
 * it's taken (no flicker on a jittery link), down at once. Logged as it changes ("quality: player 1 good ->
 * poor (p90 320 ms, quiet 40 ms)"). Drawn on Aurora: a corner icon (three bars) in the field and in battle,
 * as the setting "Connection icon" says (port/settings/features/online_status.cpp: Always, When it's bad,
 * Never), "Reconnecting..." beside it while the link is down (a guest's to its host, a room's host's to the
 * relay), and "<name>'s connection is unstable." once when a player stays poor for 10 seconds (again only
 * after 30 good ones).
 *
 * The diagnostics: plain text for a report, made only when asked (the Multiplayer page's Copy diagnostics,
 * this window's, the settings page's) and put on the clipboard; never sent anywhere. Players' names and the
 * room code's secret half are left out of it.
 *
 * Nothing here is sent to the other games, and nothing here changes play: what's drawn keeps the game's
 * random numbers as they were (second_player_pc.c's notices). */
#include "port/online_status.h"
#include "port/online.h"
#include "port/online_end.h"
#include "port/statesync.h"
#include "port/tr.h"
#include "port/platform.h"
#include "port/versions.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "win/win_main.h"
#include "system.h"
#include "pmario_sound.h"
#include <dolphin/pad.h>
#include <demo/DEMOPad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef TTYD_BACKEND_AURORA
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/windowdrv.h"
#include "manager/fontmgr.h"
#include "port/menu_buttons.h"
#include "port/menu_tap.h"
#include "port/party.h"
#include "port/settings.h"
#include "port/together.h"
#include "port/update_check.h"
#include <SDL3/SDL_clipboard.h>
extern BinaryFontHeader* pfh; /* fontmgr.c: the font, NULL until the logos load it */
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

const char* portBuildVersion(void); /* port/settings/build_id.cpp */
void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture */

#define SFX_MOVE ((const char*)9)
#define SFX_OK ((const char*)0x12)

/* ---- the join ---- */

/* each step's time before it's "taking long" (ms): reaching the relay or the host, the key, the hellos (a
   few round trips each), the host's whole progress (a large reliable message, behind a resume's backlog),
   the host's room (a map's load) */
static const unsigned kStepMs[PORT_JOIN_DONE] = {0, 15000, 15000, 15000, 30000, 30000};

/* the window's rows */
enum { W_WAIT, W_COPY, W_LEAVE, W_RETRY, W_BACK, W_UPDATE };

static struct {
    int on;    /* a guest's join under way, or done (this session) */
    int room, watch;
    int step;  /* the step under way (PORT_JOIN_CONNECT..WORLD), PORT_JOIN_DONE once through */
    unsigned long long began, stepAt, took[PORT_JOIN_DONE];
    int slow;      /* the step's time passed */
    int failed;    /* it can't go on (refused, a wrong code...): the note's in ssNetNote */
    int lobby;     /* the host isn't in a game yet */
    int skipped;   /* the last two steps not needed (a party game, each player's own game) */
    /* the window */
    int shown, row, count, rows[5];
    unsigned at; /* frames up */
    int keysOff; /* Mario's controls taken off for it (given back as it closes) */
    const char* answer; /* a row's answer (English), under the rows a while */
    unsigned answerAt;
} J;

static const char* stepKey(int step) {
    static const char* const kKeys[PORT_JOIN_DONE + 1] = {"none", "relay", "key", "version", "progress", "world",
                                                          "done"};
    return step >= 0 && step <= PORT_JOIN_DONE ? kKeys[step] : "?";
}

const char* portOnlineJoinStepName(int step) {
    switch (step) {
        case PORT_JOIN_CONNECT:
            return J.room ? "reaching the online service" : "reaching the host's game";
        case PORT_JOIN_KEY:
            return "making a secure connection";
        case PORT_JOIN_VERSION:
            return "checking you have the same version";
        case PORT_JOIN_PROGRESS:
            return "getting the host's adventure";
        case PORT_JOIN_WORLD:
            return "going into the host's world";
        default:
            return "";
    }
}

/* what to do when a step takes long (English) */
static const char* stepAdvice(int step) {
    switch (step) {
        case PORT_JOIN_CONNECT:
            return J.room ? "The online service hasn't let this game into the room. Check your internet connection "
                            "and the code, or try again later."
                          : "The host's game hasn't answered at that address. Check the address and the port, and "
                            "that the host chose Host by address (on the same network, or with the port forwarded "
                            "or a VPN).";
        case PORT_JOIN_KEY:
            return "The host's game hasn't answered in the room. Check that your friend is still hosting, and the "
                   "code's second half.";
        case PORT_JOIN_VERSION:
            return "The host's game hasn't said which version it plays. It may be busy or still loading: wait a "
                   "little, or leave and join again.";
        case PORT_JOIN_PROGRESS:
            return "The host's adventure hasn't come yet. The host may be in a menu, a scene or loading: wait a "
                   "little, or leave and join again.";
        case PORT_JOIN_WORLD:
            return "Your game hasn't reached the host's room yet. If the screen stays black, leave and join again, "
                   "and copy the diagnostics for a report.";
        default:
            return NULL;
    }
}

static unsigned stepMs(int step) {
    static int sRead;
    static unsigned sTest;
    if (!sRead) {
        const char* v = getenv("TTYD_JOIN_TIMEOUT_MS");
        sRead = 1;
        sTest = v != NULL ? (unsigned)strtoul(v, NULL, 10) : 0;
    }
    if (sTest > 0) {
        return sTest;
    }
    return step > PORT_JOIN_NONE && step < PORT_JOIN_DONE ? kStepMs[step] : 0;
}

static void windowClose(void);

void portOnlineJoinBegin(int host, int room, int watch) {
    windowClose();
    memset(&J, 0, sizeof(J));
    if (host) {
        return; /* (the host's room just opens: its guests' joins are theirs) */
    }
    J.on = 1;
    J.room = room;
    J.watch = watch;
    J.step = PORT_JOIN_CONNECT;
    J.began = J.stepAt = ssNetMs();
    ssLog("join: begins (%s%s): %d steps", room ? "a room" : "by address", watch ? ", watching" : "",
          PORT_JOIN_DONE - 1);
}

/* the timeline, once: each step's time */
static void timeline(const char* how) {
    char line[256];
    size_t n = 0;
    int s;
    line[0] = '\0';
    for (s = PORT_JOIN_CONNECT; s < PORT_JOIN_DONE; s++) {
        if (J.took[s] != 0 && n + 32 < sizeof(line)) {
            n += (size_t)snprintf(line + n, sizeof(line) - n, "%s%s %llu", n > 0 ? ", " : "", stepKey(s),
                                  J.took[s] - 1);
        }
    }
    ssLog("join: %s after %llu ms (ms per step: %s)", how, ssNetMs() - J.began, n > 0 ? line : "none");
}

void portOnlineJoinStep(int step) {
    const unsigned long long now = ssNetMs();
    if (!J.on || J.failed || step < J.step || step >= PORT_JOIN_DONE) {
        return; /* (done already: a reconnect's key and hellos come again) */
    }
    /* (steps done at once with a later one: by address, the connection is the key's step too) */
    while (J.step <= step) {
        J.took[J.step] = (now > J.stepAt ? now - J.stepAt : 0) + 1;
        ssLog("join: step %d %s done in %llu ms (total %llu ms)", J.step, stepKey(J.step), J.took[J.step] - 1,
              now - J.began);
        J.stepAt = now;
        J.step++;
    }
    J.slow = 0;
    J.lobby = 0;
    if (J.step == PORT_JOIN_DONE) {
        timeline("done");
    }
}

void portOnlineJoinEnd(void) {
    if (J.on && J.step < PORT_JOIN_DONE) {
        char why[64];
        snprintf(why, sizeof(why), "ended at step %d %s", J.step, stepKey(J.step));
        timeline(why);
    }
    windowClose();
    memset(&J, 0, sizeof(J));
}

int portOnlineJoinStepNow(void) {
    return J.on ? J.step : PORT_JOIN_NONE;
}

int portOnlineJoinSlow(void) {
    return J.on && J.step < PORT_JOIN_DONE && J.slow;
}

int portOnlineJoinLobby(void) {
    return J.on && J.step < PORT_JOIN_DONE && J.lobby;
}

int portOnlineJoinLine(char* out, size_t cap) {
    out[0] = '\0';
    if (!J.on || J.step >= PORT_JOIN_DONE || J.failed) {
        return 0;
    }
    if (J.lobby) {
        snprintf(out, cap, "%s", portTrGame("In the room: waiting for the host to choose Play or Party..."));
        return 1;
    }
    snprintf(out, cap, portTrGame("Joining (%d/%d): %s..."), J.step, PORT_JOIN_DONE - 1,
             portTrGame(portOnlineJoinStepName(J.step)));
    return 1;
}

const char* portOnlineJoinAdvice(void) {
    return portOnlineJoinSlow() ? stepAdvice(J.step) : NULL;
}

/* midnight UTC (when the relay's free day starts again) in this computer's own time, "HH:MM" */
static void resetTime(char* out, size_t cap) {
    const time_t now = time(NULL);
    struct tm utc, here;
    time_t next;
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    /* (the seconds to the next 00:00 UTC, from now) */
    next = now + (time_t)((23 - utc.tm_hour) * 3600 + (59 - utc.tm_min) * 60 + (60 - utc.tm_sec));
#ifdef _WIN32
    localtime_s(&here, &next);
#else
    localtime_r(&next, &here);
#endif
    snprintf(out, cap, "%02d:%02d", here.tm_hour, here.tm_min);
}

int portOnlineBusyLine(char* out, size_t cap) {
    char at[16];
    out[0] = '\0';
    if (portStateSyncRelayBusy()[0] == '\0') {
        return 0;
    }
    resetTime(at, sizeof(at));
    snprintf(out, cap, "%s ", portTrGame(PORT_SYNC_RELAY_BUSY));
    snprintf(out + strlen(out), cap - strlen(out),
             portTrGame("Your room plays on. The free relay's day starts again at %s your time."), at);
    return 1;
}

/* the host, as the relay first says it's busy: a notice once a session */
static void busyFrame(void) {
    static int sSaid;
    if (!portStateSyncOn() || !ssNetIsHost()) {
        sSaid = 0;
        return;
    }
    if (!sSaid && portStateSyncRelayBusy()[0] != '\0') {
        char at[16];
        sSaid = 1;
        resetTime(at, sizeof(at));
        portCoopNotice("The free relay is busy today: new rooms may be turned away. Yours plays on.");
        ssLog("quality: the relay is busy today (its day starts again at 00:00 UTC, %s here): told the player", at);
    }
}

int portOnlineRefusedNewer(void) {
    /* (the hello's refusal for another protocol, the newer side's words; a code from a newer release's) */
    return portStateSyncPhase() == PORT_SYNC_FAILED && strstr(portStateSyncNote(), "newer Paper Cut") != NULL;
}

/* each frame: the last two steps (the game side's), the step's time, a failure */
static void joinFrame(void) {
    const unsigned long long now = ssNetMs();
    int world;
    if (!J.on || J.step >= PORT_JOIN_DONE) {
        return;
    }
    if (!J.failed && portStateSyncPhase() == PORT_SYNC_FAILED) {
        J.failed = 1;
        ssLog("join: step %d %s failed: %s", J.step, stepKey(J.step), portStateSyncNote());
        return;
    }
    if (J.failed) {
        return;
    }
    world = J.step >= PORT_JOIN_PROGRESS ? ssJoinWorldStep() : -1;
    if (world == 0 && !J.skipped) {
        J.skipped = 1;
        ssLog("join: steps %d and %d not needed (a party game, or each player's own game)", PORT_JOIN_PROGRESS,
              PORT_JOIN_WORLD);
        while (J.step < PORT_JOIN_DONE) {
            J.took[J.step++] = 1;
        }
        timeline("done");
        return;
    }
    if (world == 1) {
        if (!J.lobby) {
            J.lobby = 1;
            ssLog("join: the host isn't in a game yet: waiting in the room for its Play or Party...");
        }
        J.stepAt = now; /* (no step's time runs while the host chooses) */
        J.slow = 0;
        return;
    }
    if (J.lobby && world >= 2) {
        J.lobby = 0;
        J.stepAt = now;
        ssLog("join: the host is in a game: its adventure next");
    }
    if (world == 3) {
        portOnlineJoinStep(PORT_JOIN_PROGRESS);
    } else if (world == 4) {
        portOnlineJoinStep(PORT_JOIN_WORLD);
        return;
    }
    if (!J.slow && now - J.stepAt > stepMs(J.step)) {
        J.slow = 1;
        ssLog("join: step %d %s taking long (%llu ms): %s", J.step, stepKey(J.step), now - J.stepAt,
              stepAdvice(J.step));
    }
}

/* ---- the quality ---- */

#define RTT_KEPT 64
#define WINDOW_MS 10000ull
#define QUIET_POOR_MS 2500
#define HOLD_MS 2000ull
#define FIRST_RTT_MS 6000ull /* (a room's pings go every 4 s) */
#define UNSTABLE_MS 10000ull
#define REARM_MS 30000ull

static struct {
    int rtt[RTT_KEPT];
    unsigned long long at[RTT_KEPT];
    int head, count;
    int level;                    /* PORT_LINK_* as taken */
    int want;                     /* as measured now, waiting its HOLD_MS */
    unsigned long long wantSince; /* since when */
    unsigned long long poorSince, goodSince;
    int noticed; /* "unstable" said (again only after REARM_MS good) */
    unsigned long long linkedAt; /* linked since (a first level waits for a round trip, FIRST_RTT_MS at most) */
} Q[SS_PLAYERS + 1];

static struct {
    int linkedOnce;   /* this session has been linked (a drop is a reconnect, not a join) */
    int down;         /* "Reconnecting..." now */
    unsigned long long downAt;
    int level;        /* this game's own, as the icon shows it */
    char noticeText[160];
} L;

void portOnlineQualityRtt(int player, int ms) {
    int i;
    if (player < 1 || player > SS_PLAYERS || ms < 0 || ms > 60000) {
        return;
    }
    i = (Q[player].head + Q[player].count) % RTT_KEPT;
    if (Q[player].count == RTT_KEPT) {
        Q[player].head = (Q[player].head + 1) % RTT_KEPT;
        i = (Q[player].head + RTT_KEPT - 1) % RTT_KEPT;
    } else {
        Q[player].count++;
    }
    Q[player].rtt[i] = ms;
    Q[player].at[i] = ssNetMs();
}

int portOnlineLinkRttP90(int player) {
    int v[RTT_KEPT], n = 0, i, j;
    const unsigned long long now = ssNetMs();
    if (player < 1 || player > SS_PLAYERS) {
        return -1;
    }
    for (i = 0; i < Q[player].count; i++) {
        const int k = (Q[player].head + i) % RTT_KEPT;
        if (now - Q[player].at[k] <= WINDOW_MS) {
            v[n++] = Q[player].rtt[k];
        }
    }
    if (n == 0) {
        /* (none lately: the last there was, if any) */
        return Q[player].count > 0 ? Q[player].rtt[(Q[player].head + Q[player].count - 1) % RTT_KEPT] : -1;
    }
    for (i = 1; i < n; i++) { /* (a few dozen at most) */
        const int x = v[i];
        for (j = i - 1; j >= 0 && v[j] > x; j--) {
            v[j + 1] = v[j];
        }
        v[j + 1] = x;
    }
    return v[(n * 9) / 10 < n ? (n * 9) / 10 : n - 1];
}

int portOnlineLinkQuality(int player) {
    return player >= 1 && player <= SS_PLAYERS ? Q[player].level : PORT_LINK_NONE;
}

static const char* levelName(int level) {
    static const char* const kNames[] = {"none", "good", "fair", "poor", "down"};
    return level >= 0 && level <= PORT_LINK_DOWN ? kNames[level] : "?";
}

/* the players this game measures: the host its guests, a guest its host */
static int measured(int p) {
    return ssNetIsHost() ? p >= 2 : p == 1;
}

static void forget(int p) {
    memset(&Q[p], 0, sizeof(Q[p]));
}

static void unstableNotice(int p) {
    char name[SS_NAME * 2];
    if (ssNetIsHost()) {
        portStateSyncPlayerNameShown(p, name, sizeof(name));
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "%s", portTrGame("A player"));
        }
        snprintf(L.noticeText, sizeof(L.noticeText), portTrGame("%s's connection is unstable."), name);
        portCoopNotice(L.noticeText);
    } else {
        portCoopNotice("The connection to the host is unstable.");
    }
    ssLog("quality: notice: player %d's connection is unstable (poor for %llu s)", p, UNSTABLE_MS / 1000ull);
}

static void qualityFrame(void) {
    const unsigned long long now = ssNetMs();
    const int on = portStateSyncOn() && portStateSyncPhase() != PORT_SYNC_OFF;
    int p, worst = PORT_LINK_NONE, anyLinked = 0;
    if (!on) {
        if (L.linkedOnce || L.level != PORT_LINK_NONE) {
            for (p = 1; p <= SS_PLAYERS; p++) {
                forget(p);
            }
            memset(&L, 0, sizeof(L));
        }
        return;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        int raw;
        long long quiet;
        if (!measured(p)) {
            continue;
        }
        if (!ssNetLinked(p)) {
            if (Q[p].level != PORT_LINK_NONE) {
                ssLog("quality: player %d %s -> not linked", p, levelName(Q[p].level));
            }
            /* (its samples kept: a reconnect's first ones join them) */
            Q[p].level = Q[p].want = PORT_LINK_NONE;
            Q[p].poorSince = Q[p].goodSince = 0;
            Q[p].linkedAt = 0;
            continue;
        }
        anyLinked = 1;
        quiet = ssNetQuietMs(p);
        {
            const int rtt = portOnlineLinkRttP90(p);
            if (Q[p].linkedAt == 0) {
                Q[p].linkedAt = now | 1;
            }
            if (Q[p].level == PORT_LINK_NONE && rtt < 0 && quiet <= QUIET_POOR_MS && now < Q[p].linkedAt + FIRST_RTT_MS) {
                continue; /* (just linked, no round trip measured yet: rated on the first, not guessed) */
            }
            raw = rtt < 0 ? PORT_LINK_GOOD : rtt < 100 ? PORT_LINK_GOOD : rtt < 250 ? PORT_LINK_FAIR : PORT_LINK_POOR;
            if (quiet > QUIET_POOR_MS) {
                raw = PORT_LINK_POOR;
            }
            if (raw != Q[p].want) {
                Q[p].want = raw;
                Q[p].wantSince = now;
            }
            if (Q[p].level == PORT_LINK_NONE || (raw != Q[p].level && now - Q[p].wantSince >= HOLD_MS)) {
                ssLog("quality: player %d %s -> %s (p90 %d ms, quiet %lld ms)", p, levelName(Q[p].level),
                      levelName(raw), rtt, quiet);
                Q[p].level = raw;
            }
        }
        if (Q[p].level == PORT_LINK_POOR) {
            Q[p].goodSince = 0;
            if (Q[p].poorSince == 0) {
                Q[p].poorSince = now;
            }
            if (!Q[p].noticed && now - Q[p].poorSince >= UNSTABLE_MS && gp != NULL && !gp->inBattle) {
                Q[p].noticed = 1; /* (in battle a notice isn't drawn: said once back in the field) */
                unstableNotice(p);
            }
        } else {
            Q[p].poorSince = 0;
            if (Q[p].goodSince == 0) {
                Q[p].goodSince = now;
            }
            if (Q[p].noticed && Q[p].level == PORT_LINK_GOOD && now - Q[p].goodSince >= REARM_MS) {
                Q[p].noticed = 0;
            }
        }
        if (Q[p].level > worst) {
            worst = Q[p].level;
        }
    }
    if (anyLinked) {
        L.linkedOnce = 1;
    }
    {
        /* down: this game's own way to the others gone (statesync_net_pc.c ssNetSelfDown: a room's relay
           connection, a guest's by address to its host) while it has had players, the session still on, not
           over or failed. A room's guest whose own connection is up but whose host is gone isn't reconnecting:
           that's the wait for the host's window (online_end_pc.c portOnlineWait) */
        const int down = L.linkedOnce && portStateSyncPhase() != PORT_SYNC_FAILED && !portOnlineEndUp() &&
                         ssNetSelfDown();
        if (down && !L.down) {
            L.down = 1;
            L.downAt = now;
            ssLog("quality: reconnecting (%s)", ssNetIsHost() ? "the relay's connection is down" : "the link to the host is down");
        } else if (!down && L.down) {
            L.down = 0;
            ssLog("quality: the link is back after %llu ms", now - L.downAt);
        }
    }
    L.level = L.down ? PORT_LINK_DOWN : worst;
}

/* ---- the diagnostics ---- */

#define LOG_LINES 40
static char sLog[LOG_LINES][240];
static int sLogHead, sLogCount;

void portOnlineStatusLogLine(const char* line) {
    static const char* const kKept[] = {"net:", "co-op:", "join:", "quality:", "end:", "on:", "watch:", "invite:",
                                        "launch:"};
    size_t i;
    int keep = 0;
    for (i = 0; i < sizeof(kKept) / sizeof(kKept[0]) && !keep; i++) {
        keep = strncmp(line, kKept[i], strlen(kKept[i])) == 0;
    }
    if (!keep) {
        return;
    }
    {
        /* (full: the oldest goes) */
        const int at = sLogCount == LOG_LINES ? sLogHead : (sLogHead + sLogCount) % LOG_LINES;
        snprintf(sLog[at], sizeof(sLog[0]), "%s", line);
        if (sLogCount == LOG_LINES) {
            sLogHead = (sLogHead + 1) % LOG_LINES;
        } else {
            sLogCount++;
        }
    }
}

/* every "what" in s replaced with "with" (in place, s's room cap) */
static void scrub(char* s, size_t cap, const char* what, const char* with) {
    const size_t wl = strlen(what), nl = strlen(with);
    char* at;
    if (wl < 2) {
        return; /* (a one-letter name would take letters out of every word) */
    }
    while ((at = strstr(s, what)) != NULL) {
        const size_t tail = strlen(at + wl);
        if ((size_t)(at - s) + nl + tail + 1 > cap) {
            break;
        }
        memmove(at + nl, at + wl, tail + 1);
        memcpy(at, with, nl);
    }
}

static const char* platformName(void) {
#if defined(__EMSCRIPTEN__)
    return "Browser";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return "iPhone or iPad";
#elif defined(__APPLE__)
    return "Mac";
#elif defined(_WIN32)
    return "Windows";
#else
    return "Linux";
#endif
}

static const char* phaseName(int phase) {
    switch (phase) {
        case PORT_SYNC_OFF:
            return "off";
        case PORT_SYNC_WAITING:
            return "waiting";
        case PORT_SYNC_LINKED:
            return "linked";
        case PORT_SYNC_FAILED:
            return "failed";
        default:
            return "?";
    }
}

size_t portOnlineDiagnostics(char* out, size_t cap) {
    const int phase = portStateSyncPhase();
    const int on = portStateSyncOn() && phase != PORT_SYNC_OFF;
    size_t n = 0;
    int p, i;
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
#define DIAG_ADD(...)                                                                                                       \
    do {                                                                                                               \
        if (n < cap) {                                                                                                 \
            const int w_ = snprintf(out + n, cap - n, __VA_ARGS__);                                                   \
            n = w_ < 0 ? n : (n + (size_t)w_ >= cap ? cap - 1 : n + (size_t)w_);                                      \
        }                                                                                                              \
    } while (0)
    DIAG_ADD("Paper Cut online diagnostics (made on this computer; nothing was sent anywhere)\n");
    DIAG_ADD("build %s | protocol %u | disc %.6s | %s\n", portBuildVersion(), ssNetProtocol(), portVersionSelf()->id,
        platformName());
    if (!on) {
        DIAG_ADD("online: not in a room (%s)%s%s\n", phaseName(phase), portStateSyncNote()[0] != '\0' ? ": " : "",
            portStateSyncNote());
    } else {
        DIAG_ADD("online: %s (player %d) | %s | %s%s%s\n", ssNetIsHost() ? "host" : portStateSyncWatcher() ? "watcher" : "guest",
            ssNetMe(), ssNetRoom() ? "through the relay" : "by address", phaseName(phase),
            portStateSyncNote()[0] != '\0' ? ": " : "", portStateSyncNote());
        for (p = 1; p <= SS_PLAYERS; p++) {
            if (!measured(p) || (!ssNetLinked(p) && Q[p].count == 0)) {
                continue;
            }
            DIAG_ADD("player %d: %s%s, last %d ms, p90 %d ms, quiet %lld ms, %s\n", p, ssNetLinked(p) ? "linked" : "not linked",
                ssNetDirect(p) ? " direct" : "", ssNetRttMs(p), portOnlineLinkRttP90(p), ssNetQuietMs(p),
                levelName(Q[p].level));
        }
        DIAG_ADD("connection: %s%s%s\n", levelName(L.level), L.down ? " (reconnecting)" : "",
                 portStateSyncRelayBusy()[0] != '\0' ? " | the relay says it's busy today" : "");
    }
    if (J.on || J.began != 0) {
        DIAG_ADD("join: %s, step %d %s%s%s, %llu ms in", J.step >= PORT_JOIN_DONE ? "done" : J.failed ? "failed" : "under way",
            J.step < PORT_JOIN_DONE ? J.step : PORT_JOIN_DONE - 1, stepKey(J.step < PORT_JOIN_DONE ? J.step : PORT_JOIN_WORLD),
            J.slow ? " (taking long)" : "", J.lobby ? " (the host isn't in a game yet)" : "", ssNetMs() - J.began);
        for (i = PORT_JOIN_CONNECT; i < PORT_JOIN_DONE; i++) {
            if (J.took[i] != 0) {
                DIAG_ADD("%s %s %llu ms", i == PORT_JOIN_CONNECT ? ";" : ",", stepKey(i), J.took[i] - 1);
            }
        }
        DIAG_ADD("\n");
    }
    DIAG_ADD("last online lines (%d):\n", sLogCount);
    for (i = 0; i < sLogCount; i++) {
        DIAG_ADD("  %s\n", sLog[(sLogHead + i) % LOG_LINES]);
    }
#undef DIAG_ADD
    /* names and the code's secret half out (a report is posted where anyone reads it) */
    if (on) {
        const char* code = portStateSyncCode();
        for (p = 1; p <= SS_PLAYERS; p++) {
            char with[8];
            snprintf(with, sizeof(with), "P%d", p);
            scrub(out, cap, portStateSyncPlayerName(p), with);
        }
        if (strlen(code) == PORT_ONLINE_CODE_LENGTH + 1) {
            char whole[PORT_ONLINE_CODE_LENGTH + 1];
            snprintf(whole, sizeof(whole), "%.4s%s", code, code + 5);
            scrub(out, cap, whole, "****");
            scrub(out, cap, code + 5, "****");
        }
    }
    return strlen(out);
}

/* the diagnostics to the clipboard (Aurora), or to the log (the headless build, tests); 1 done */
int portOnlineCopyDiagnostics(void) {
    static char sText[12288];
    const size_t n = portOnlineDiagnostics(sText, sizeof(sText));
#ifdef TTYD_BACKEND_AURORA
    const int ok = n > 0 && SDL_SetClipboardText(sText);
#else
    const int ok = n > 0;
#endif
    ssLog("join: diagnostics %s (%u bytes)", ok ? "copied" : "not copied", (unsigned)n);
    if (getenv("TTYD_JOIN_PICK") != NULL || getenv("TTYD_SYNC_WAIT_PICK") != NULL) {
        /* (tests: what was copied, a line each, to check nothing of the names or the code's secret is in it) */
        char* line = sText;
        while (line != NULL && *line != '\0') {
            char* nl = strchr(line, '\n');
            if (nl != NULL) {
                *nl = '\0';
            }
            ssLog("diag: %s", line);
            line = nl != NULL ? nl + 1 : NULL;
        }
    }
    return ok;
}

/* ---- the window: a join that takes long, or failed ---- */

static const char* rowLabel(int r) {
    switch (r) {
        case W_WAIT:
            return "Keep waiting";
        case W_COPY:
            return "Copy diagnostics";
        case W_LEAVE:
            return "Leave the room";
        case W_RETRY:
            return "Try again";
        case W_UPDATE:
            return "Check for updates";
        default:
            return "Back";
    }
}

static void windowClose(void) {
    if (J.shown) {
        ssLog("join: the window closes");
    }
    J.shown = 0;
    J.answer = NULL;
#ifdef TTYD_BACKEND_AURORA
    if (J.keysOff && gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && !marioChkKey() &&
        !portStateSyncCoopGuest()) {
        marioKeyOn();
    }
#endif
    J.keysOff = 0;
}

static int updatesHere(void) {
#if defined(__EMSCRIPTEN__) || !defined(TTYD_BACKEND_AURORA)
    return getenv("TTYD_JOIN_PICK") != NULL; /* (tests: the row's there to be chosen) */
#else
    return 1;
#endif
}

static void windowOpen(void) {
    J.count = 0;
    J.row = 0;
    J.at = 0;
    J.answer = NULL;
    if (J.failed) {
        if (portOnlineRefusedNewer() && updatesHere()) {
            J.rows[J.count++] = W_UPDATE;
        }
        J.rows[J.count++] = W_RETRY;
        J.rows[J.count++] = W_COPY;
        J.rows[J.count++] = W_BACK;
    } else {
        J.rows[J.count++] = W_WAIT;
        J.rows[J.count++] = W_COPY;
        J.rows[J.count++] = W_LEAVE;
    }
    J.shown = 1;
    ssLog("join: the window is up (step %d %s %s)", J.step, stepKey(J.step), J.failed ? "failed" : "taking long");
}

static void choose(int r) {
    ssLog("join: chose \"%s\"", rowLabel(r));
    switch (r) {
        case W_WAIT:
            windowClose();
            J.slow = 0;
            J.stepAt = ssNetMs(); /* (the step's whole time again) */
            return;
        case W_COPY:
            J.answer = portOnlineCopyDiagnostics() ? "Copied: paste it into your report. Nothing was sent anywhere."
                                         : "Couldn't copy it.";
            J.answerAt = J.at;
            return;
        case W_UPDATE:
#ifdef TTYD_BACKEND_AURORA
            portUpdateCheckNow();
#endif
            J.answer = "Asking for the newest Paper Cut...";
            J.answerAt = J.at;
            return;
        case W_RETRY:
            windowClose();
            if (!portStateSyncRetry()) {
                ssLog("join: nothing to try again (%s)", portStateSyncNote());
            }
            return;
        default: /* W_LEAVE, W_BACK */
            windowClose();
            portStateSyncLeave();
            return;
    }
}

/* the window can be up: never over the Start menu or the title's pages (their status says the same), nor
   with the "where now" window, nor for the online service unreachable (that one's own) */
static int windowAllowed(void) {
    if (!J.on || J.step >= PORT_JOIN_DONE || !(J.slow || J.failed) || portOnlineEndUp() ||
        !strcmp(portStateSyncNote(), PORT_SYNC_NO_SERVICE) || gp == NULL) {
        return 0;
    }
    if (seqGetSeq() == SEQ_GAME && winCheck()) {
        return 0;
    }
#ifdef TTYD_BACKEND_AURORA
    if (portTitleOnPages()) {
        return 0;
    }
#endif
    return 1;
}

#ifdef TTYD_BACKEND_AURORA
#define WIN_W 480.0f
#define WIN_X (-WIN_W / 2.0f)
#define WIN_Y 150.0f
#define WIN_H 290.0f
#define ROW_X (WIN_X + 110.0f)
#define ROW_Y (WIN_Y - 168.0f)
#define ROW_STEP 26.0f

/* The one window for an online wait (port/include/port/online_status.h portOnlineNoticeShow): the join's that
   takes long or failed, and online_end_pc.c's wait for a lost host. What it says, as this frame left it. */
#define NOTICE_ROWS 6
static struct {
    char line[400], advice[400], answer[300];
    const char* rows[NOTICE_ROWS];
    char values[NOTICE_ROWS][96];
    int count, row;
} N;

static void noticeDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    int i, lines;
    float y;
    (void)camId;
    (void)param;
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x70}, -400.0f, 260.0f, 800.0f, 520.0f, 4.0f);
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x50}, WIN_X + 5.0f, WIN_Y - 5.0f, WIN_W, WIN_H, 20.0f);
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, WIN_X, WIN_Y, WIN_W, WIN_H, 20.0f);
    lines = portPartyText(WIN_X + 24.0f, WIN_Y - 16.0f, 0.68f, 0x202020FFu, WIN_W - 48.0f, N.line);
    y = WIN_Y - 20.0f - (float)lines * 24.0f * 0.68f - 6.0f;
    portPartyText(WIN_X + 24.0f, y, 0.55f, 0x505050FFu, WIN_W - 48.0f, N.advice);
    for (i = 0; i < N.count; i++) {
        const float ry = ROW_Y - (float)i * ROW_STEP;
        portPartyText(ROW_X, ry, 0.62f, 0x202020FFu, WIN_W - 150.0f, portTrGame(N.rows[i]));
        if (N.values[i][0] != '\0') {
            portPartyText(WIN_X + WIN_W - 24.0f - portPartyTextWidth(N.values[i], 0.55f), ry, 0.55f, 0x505050FFu,
                          200.0f, N.values[i]);
        }
        if (i == N.row) {
            portPartyCursor(ROW_X - 22.0f, ry - 10.0f, 0.8f);
        }
    }
    if (N.answer[0] != '\0') {
        portPartyText(WIN_X + 24.0f, WIN_Y - WIN_H + 30.0f, 0.5f, 0x305030FFu, WIN_W - 48.0f, N.answer);
    }
    portRandSetState(seed);
}

void portOnlineNoticeShow(const char* line, const char* advice, const char* const* rows, const char* const* values,
                          int count, int row, const char* answer) {
    int i;
    snprintf(N.line, sizeof(N.line), "%s", line != NULL ? line : "");
    snprintf(N.advice, sizeof(N.advice), "%s", advice != NULL ? advice : "");
    snprintf(N.answer, sizeof(N.answer), "%s", answer != NULL ? answer : "");
    N.count = count < NOTICE_ROWS ? count : NOTICE_ROWS;
    N.row = row;
    for (i = 0; i < N.count; i++) {
        N.rows[i] = rows[i];
        snprintf(N.values[i], sizeof(N.values[i]), "%s", values != NULL && values[i] != NULL ? values[i] : "");
    }
    if (pfh != NULL) { /* (a join from the command line can fail before the logos load the font: drawn from then) */
        dispEntry(CAMERA_2D, 961.0f, 1, noticeDisp, NULL);
    }
    portMenuMark(1); /* (B backs out on every controller: port/include/port/menu_buttons.h) */
}

unsigned portOnlineNoticePointer(int count, int* row) {
    float gx, gy;
    int i;
    const int kind = portMenuPoint(&gx, &gy);
    if (kind == 0 || gx < WIN_X || gx > WIN_X + WIN_W) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const float y = ROW_Y - (float)i * ROW_STEP;
        if (gy <= y + 4.0f && gy > y - ROW_STEP + 4.0f) {
            if (*row != i) {
                *row = i;
                psndSFXOn(SFX_MOVE);
            }
            return kind == 1 ? PAD_BUTTON_A : 0;
        }
    }
    return 0;
}

/* the join's window, this frame */
static void windowShow(void) {
    char line[400], advice[400], answer[300];
    const char* labels[NOTICE_ROWS];
    const char* values[NOTICE_ROWS];
    char value[NOTICE_ROWS][96];
    int i, st = 0;
    char v[48];
    snprintf(line, sizeof(line), portTrGame(J.failed ? "Joining stopped at step %d of %d: %s."
                                                     : "Joining is taking a while at step %d of %d: %s."),
             J.step, PORT_JOIN_DONE - 1, portTrGame(portOnlineJoinStepName(J.step)));
    if (J.failed) {
        const char *theirs, *mine;
        snprintf(advice, sizeof(advice), "%s", portTrGame(portStateSyncNote()));
        if (portStateSyncRefusedBuilds(&theirs, &mine)) {
            const size_t n = strlen(advice);
            snprintf(advice + n, sizeof(advice) - n, " ");
            snprintf(advice + n + 1, sizeof(advice) - n - 1, portTrGame("Your friend's: Paper Cut %s. Yours: Paper Cut %s."),
                     theirs, mine);
        }
    } else {
        snprintf(advice, sizeof(advice), "%s", portTrGame(stepAdvice(J.step)));
    }
    for (i = 0; i < J.count && i < NOTICE_ROWS; i++) {
        labels[i] = rowLabel(J.rows[i]);
        values[i] = value[i];
        value[i][0] = '\0';
        if (J.rows[i] == W_UPDATE) {
            st = portUpdateCheckState(v, sizeof(v));
            if (st == 1) {
                snprintf(value[i], sizeof(value[i]), "%s", portTrGame("Checking..."));
            } else if (st == 2) {
                snprintf(value[i], sizeof(value[i]), "%s", portTrGame("None found"));
            } else if (st == 3) {
                snprintf(value[i], sizeof(value[i]), portTrGame("Paper Cut %s is out"), v);
            }
        }
    }
    answer[0] = '\0';
    if (J.answer != NULL) {
        snprintf(answer, sizeof(answer), "%s", portTrGame(J.answer));
    } else if (st == 3) {
        snprintf(answer, sizeof(answer),
                 portTrGame("Paper Cut %s is out: get it from the Start menu's Settings, General, New version."), v);
    }
    portOnlineNoticeShow(line, advice, labels, values, i, J.row, answer);
}
#endif

static void windowFrame(void) {
    unsigned trg = 0, dir = 0;
    if (!windowAllowed()) {
        if (J.shown) {
            windowClose();
        }
        return;
    }
    if (!J.shown) {
        windowOpen();
    }
    J.at++;
    if (J.answer != NULL && J.at - J.answerAt > 240) {
        J.answer = NULL;
    }
    {
        /* tests: the choice by name ($TTYD_JOIN_PICK), a second after it's up; nothing chosen by itself otherwise
           (a join that's slow may still go through: the window then goes by itself) */
        const char* want = getenv("TTYD_JOIN_PICK");
        if (want != NULL && J.at == 60) {
            int i, pick = -1;
            for (i = 0; i < J.count; i++) {
                const int r = J.rows[i];
                if ((r == W_WAIT && !strcmp(want, "wait")) || (r == W_COPY && !strcmp(want, "copy")) ||
                    ((r == W_LEAVE || r == W_BACK) && !strcmp(want, "leave")) ||
                    (r == W_RETRY && !strcmp(want, "retry")) || (r == W_UPDATE && !strcmp(want, "update"))) {
                    pick = r;
                }
            }
            if (pick >= 0) {
                choose(pick);
                if (!J.shown) {
                    return;
                }
            }
        }
    }
#ifdef TTYD_BACKEND_AURORA
    if (seqGetSeq() == SEQ_GAME && marioChkKey()) {
        marioKeyOff(); /* (the window has the controller: Mario stands meanwhile) */
        J.keysOff = 1;
    }
    windowShow();
    trg = keyGetButtonTrg(0);
    dir = keyGetDirRep(0);
    if (trg == 0 && dir == 0) {
        trg = portOnlineNoticePointer(J.count, &J.row);
    }
#endif
    if (J.count == 0) {
        return;
    }
    if (dir & DEMO_STICK_UP) {
        J.row = (J.row + J.count - 1) % J.count;
        psndSFXOn(SFX_MOVE);
    } else if (dir & DEMO_STICK_DOWN) {
        J.row = (J.row + 1) % J.count;
        psndSFXOn(SFX_MOVE);
    } else if (trg & PAD_BUTTON_A) {
        psndSFXOn(SFX_OK);
        choose(J.rows[J.row]);
    } else if (trg & PAD_BUTTON_B) {
        /* B (Escape, the touch controls' B, a right click): Keep waiting, or Back */
        psndSFXOn(SFX_OK);
        choose(J.failed ? W_BACK : W_WAIT);
    }
}

/* ---- what's drawn over the picture (Aurora) ---- */

#ifdef TTYD_BACKEND_AURORA
/* the corner: right, under the status bar's row, inside the 4:3 picture (a wider screen's HUD stays there) */
#define ICON_X 268.0f
#define ICON_Y 178.0f

static void iconDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    static const GXColor kColour[] = {{0x80, 0x80, 0x80, 0xFF}, {0x3C, 0xC8, 0x50, 0xFF}, {0xF0, 0xC0, 0x20, 0xFF},
                                      {0xE8, 0x40, 0x30, 0xFF}, {0x90, 0x90, 0x90, 0xFF}};
    const GXColor off = {0x30, 0x30, 0x30, 0xA0};
    const int level = L.level;
    const int lit = level == PORT_LINK_GOOD ? 3 : level == PORT_LINK_FAIR ? 2 : level == PORT_LINK_POOR ? 1 : 0;
    int b;
    (void)camId;
    (void)param;
    /* a dark plate, then three bars from short to tall */
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x60}, ICON_X - 4.0f, ICON_Y + 4.0f, 34.0f, 26.0f, 4.0f);
    for (b = 0; b < 3; b++) {
        const float h = 6.0f + 6.0f * (float)b;
        windowDispGX_Waku_col(0, b < lit ? kColour[level] : off, ICON_X + 2.0f + 9.0f * (float)b,
                              ICON_Y - 18.0f + h, 6.0f, h, 1.0f);
    }
    if (level == PORT_LINK_DOWN) {
        const char* text = portTrGame("Reconnecting...");
        const float w = (float)FontGetMessageWidth(text) * 0.6f;
        FontDrawStart_alpha(0xFF);
        FontDrawEdge();
        FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        FontDrawScale(0.6f);
        FontDrawString(ICON_X - 10.0f - w, ICON_Y - 2.0f, text);
    }
    portRandSetState(seed);
}

static char sJoinLine[200];

static void joinLineDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const float maxW = 540.0f;
    float scale = 0.6f, w;
    (void)camId;
    (void)param;
    w = (float)FontGetMessageWidth(sJoinLine) * scale;
    if (w > maxW) {
        scale *= maxW / w;
        w = maxW;
    }
    FontDrawStart_alpha(0xFF);
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    FontDrawScale(scale);
    /* (under the picture; over it at the title, whose buttons are along the bottom) */
    FontDrawString(-w * 0.5f, seqGetSeq() == SEQ_TITLE || seqGetSeq() == SEQ_LOGO ? 214.0f : -196.0f, sJoinLine);
    portRandSetState(seed);
}

/* the setting: 0 Always, 1 When it's bad, 2 Never */
static int iconSetting(void) {
    static int* sV;
    if (sV == NULL) {
        sV = portSettingsFind("online_quality_icon");
    }
    return sV != NULL ? *sV : 1;
}

static void drawFrame(void) {
    const int seq = seqGetSeq();
    const int show = iconSetting();
    if (pfh == NULL || gp == NULL || J.shown) {
        return;
    }
    if ((seq == SEQ_GAME && !winCheck()) || seq == SEQ_BATTLE) {
        if (show != 2 && L.level != PORT_LINK_NONE && (show == 0 || L.level >= PORT_LINK_FAIR)) {
            dispEntry(CAMERA_2D, 950.0f, 1, iconDisp, NULL);
        }
    }
    /* the join's step under the picture while it runs (a black screen still says where it is), but over the
       Start menu or the title's pages, which show it in their status */
    if (portOnlineJoinLine(sJoinLine, sizeof(sJoinLine)) && !(seq == SEQ_GAME && winCheck()) && !portTitleOnPages()) {
        dispEntry(CAMERA_2D, 950.0f, 1, joinLineDisp, NULL);
    }
}
#endif

void portOnlineStatusFrame(void) {
    joinFrame();
    qualityFrame();
    busyFrame();
    windowFrame();
#ifdef TTYD_BACKEND_AURORA
    drawFrame();
#endif
}
