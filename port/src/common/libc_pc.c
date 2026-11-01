/* C library functions the game calls that only some hosts' C libraries have. */
#ifndef _WIN32

/* MSL's strupr: a-z to A-Z in place (its toupper's "C" locale) */
char* strupr(char* s) {
    char* p;
    for (p = s; *p != '\0'; p++) {
        if (*p >= 'a' && *p <= 'z') {
            *p = (char)(*p - 'a' + 'A');
        }
    }
    return s;
}

#else
typedef int portLibcEmptyUnit; /* Windows' C library has them all */
#endif
