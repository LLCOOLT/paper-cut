/* TTYD_* environment values as a player types them. In cmd, `set TTYD_GPU=d3d11 && ttyd-us.exe` keeps the
 * space before && in the value ("d3d11 "), and `set TTYD_USER_DIR="C:\My saves"` keeps the quotes; read as
 * they are, such a value matched nothing and was ignored. Before main, and before anything reads one (the
 * low-address option runs right after, low_address_pc.c), every TTYD_* value loses its outer spaces and one
 * pair of outer quotes, and the values that are names (TTYD_GPU) are lower-cased. Each change is logged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <wchar.h>
#include <windows.h>
typedef wchar_t EnvChar;
#define ENV_T(s) L##s
#else
typedef char EnvChar;
#define ENV_T(s) s
extern char** environ;
#endif

#define ENV_MAX 4096

/* values that are a word from a list, compared lower-case */
static const EnvChar* const kLowerNames[] = {ENV_T("TTYD_GPU")};

static int isSpace(EnvChar c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static size_t envLen(const EnvChar* s) {
    size_t n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

static int envEq(const EnvChar* a, const EnvChar* b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* value (len n) tidied into out; 1 when it changed */
static int tidyValue(const EnvChar* name, size_t nameLen, const EnvChar* v, size_t n, EnvChar* out) {
    size_t b = 0, e = n, i, k;
    int lower = 0;
    for (;;) {
        while (b < e && isSpace(v[b])) {
            b++;
        }
        while (e > b && isSpace(v[e - 1])) {
            e--;
        }
        if (e - b >= 2 && (v[b] == '"' || v[b] == '\'') && v[e - 1] == v[b]) {
            b++;
            e--;
            continue;
        }
        break;
    }
    for (k = 0; k < sizeof(kLowerNames) / sizeof(kLowerNames[0]); k++) {
        if (envLen(kLowerNames[k]) == nameLen && envEq(kLowerNames[k], name, nameLen)) {
            lower = 1;
        }
    }
    for (i = b; i < e; i++) {
        EnvChar c = v[i];
        if (lower && c >= 'A' && c <= 'Z') {
            c = (EnvChar)(c - 'A' + 'a');
        }
        out[i - b] = c;
    }
    out[e - b] = 0;
    return e - b != n || !envEq(out, v, n);
}

static void tidyEnvironment(void) {
    /* the changes, applied after the walk (setting a value can move the block being walked) */
    static EnvChar names[16][256];
    static EnvChar values[16][ENV_MAX];
    int count = 0, i;
#if defined(_WIN32)
    EnvChar* block = GetEnvironmentStringsW();
    const EnvChar* p;
    if (block == NULL) {
        return;
    }
    for (p = block; *p != 0 && count < 16; p += envLen(p) + 1) {
        const EnvChar* entry = p;
#else
    char** ep;
    if (environ == NULL) {
        return;
    }
    for (ep = environ; *ep != NULL && count < 16; ep++) {
        const EnvChar* entry = *ep;
#endif
        const EnvChar* eq;
        size_t nameLen, valueLen;
        if (!envEq(entry, ENV_T("TTYD_"), 5)) {
            continue;
        }
        for (eq = entry; *eq != 0 && *eq != '='; eq++) {
        }
        nameLen = (size_t)(eq - entry);
        if (*eq != '=' || nameLen >= 256) {
            continue;
        }
        valueLen = envLen(eq + 1);
        if (valueLen >= ENV_MAX) {
            continue;
        }
        if (tidyValue(entry, nameLen, eq + 1, valueLen, values[count])) {
            memcpy(names[count], entry, nameLen * sizeof(EnvChar));
            names[count][nameLen] = 0;
            count++;
        }
    }
#if defined(_WIN32)
    FreeEnvironmentStringsW(block);
#endif
    for (i = 0; i < count; i++) {
#if defined(_WIN32)
        /* (both the C library's copy and Windows' own, which SDL reads) */
        _wputenv_s(names[i], values[i]);
        printf("[env] %ls read as \"%ls\"\n", names[i], values[i]);
#else
        setenv(names[i], values[i], 1);
        printf("[env] %s read as \"%s\"\n", names[i], values[i]);
#endif
    }
    if (count > 0) {
        fflush(stdout);
    }
}

/* .CRT$XCS sorts ahead of the low-address option's .CRT$XCT and the C++ constructors' .CRT$XCU */
#if defined(_MSC_VER)
#if defined(__clang__)
__attribute__((section(".CRT$XCS"), used)) static void (*const sEnvTidyInit)(void) = tidyEnvironment;
#else
#pragma section(".CRT$XCS", read)
__declspec(allocate(".CRT$XCS")) static void (*const sEnvTidyInit)(void) = tidyEnvironment;
#endif
#else
__attribute__((constructor(101))) static void envTidyInit(void) {
    tidyEnvironment();
}
#endif
