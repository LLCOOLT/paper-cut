/* glibc 2.38's C23 number parsers, for a build on an older glibc (the Linux
 * release builds on Ubuntu 22.04's 2.35: tools/port/linux-release.Dockerfile).
 *
 * The prebuilt Dawn library Aurora links was compiled against glibc 2.38 or
 * newer, whose headers turn strtol and its kin into __isoc23_strtol and so on
 * (C23 also reads "0b" binary numbers). An older glibc has no such functions,
 * so the link would fail, or the program would ask for GLIBC_2.38 at start.
 * Defined here they bind inside the executable and send Dawn to the classic
 * functions (it reads decimal and hexadecimal numbers only). On glibc 2.38 and
 * newer this file is empty: the C library's own are used. */
#if defined(__linux__) && !defined(__ANDROID__)
#include <features.h>
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 38)
#include <stdlib.h>

long __isoc23_strtol(const char* s, char** end, int base);
long long __isoc23_strtoll(const char* s, char** end, int base);
unsigned long __isoc23_strtoul(const char* s, char** end, int base);
unsigned long long __isoc23_strtoull(const char* s, char** end, int base);

long __isoc23_strtol(const char* s, char** end, int base) {
    return strtol(s, end, base);
}

long long __isoc23_strtoll(const char* s, char** end, int base) {
    return strtoll(s, end, base);
}

unsigned long __isoc23_strtoul(const char* s, char** end, int base) {
    return strtoul(s, end, base);
}

unsigned long long __isoc23_strtoull(const char* s, char** end, int base) {
    return strtoull(s, end, base);
}
#endif
/* The ARM64 handheld build links against glibc 2.31 (cmake/toolchains/
 * linux-arm64.cmake), with GCC 14's C++ library and unwinder linked in from
 * archives built on a newer glibc. What they ask of it that 2.31 lacks, kept
 * inside the executable (hidden: never put in front of the C library's own
 * for the system's libraries). */
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 35)
#include <link.h>
#include <stdint.h>
#include <sys/random.h>

#define TTYD_COMPAT_HIDDEN __attribute__((visibility("hidden")))

/* glibc 2.32's "no second thread yet" flag, which lets the C++ library skip
 * atomics. Zero: always the threaded path, correct either way. */
TTYD_COMPAT_HIDDEN char __libc_single_threaded = 0;

/* glibc 2.36's arc4random (std::random_device): the kernel's random bytes */
TTYD_COMPAT_HIDDEN uint32_t arc4random(void);
TTYD_COMPAT_HIDDEN uint32_t arc4random(void) {
    uint32_t value = 0;
    while (getrandom(&value, sizeof(value), 0) != (ssize_t)sizeof(value)) {
    }
    return value;
}

/* glibc 2.35's _dl_find_object, which GCC's unwinder uses for exceptions
 * when it is there (and then never falls back): the object holding pc and
 * its .eh_frame_hdr, found with dl_iterate_phdr as GCC's older unwinder did */
struct dl_find_object {
    unsigned long long int dlfo_flags;
    void* dlfo_map_start;
    void* dlfo_map_end;
    struct link_map* dlfo_link_map;
    void* dlfo_eh_frame;
    unsigned long long int __dflo_reserved[7];
};

struct FindObject {
    uintptr_t pc;
    struct dl_find_object* out;
    int found;
};

static int findObject(struct dl_phdr_info* info, size_t size, void* data) {
    struct FindObject* find = (struct FindObject*)data;
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    void* eh = NULL;
    int inside = 0;
    size_t i;
    (void)size;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        const uintptr_t start = info->dlpi_addr + ph->p_vaddr;
        if (ph->p_type == PT_LOAD) {
            if (find->pc >= start && find->pc < start + ph->p_memsz) {
                inside = 1;
            }
            lo = start < lo ? start : lo;
            hi = start + ph->p_memsz > hi ? start + ph->p_memsz : hi;
        } else if (ph->p_type == PT_GNU_EH_FRAME) {
            eh = (void*)start;
        }
    }
    if (!inside) {
        return 0;
    }
    find->out->dlfo_flags = 0;
    find->out->dlfo_map_start = (void*)lo;
    find->out->dlfo_map_end = (void*)hi;
    find->out->dlfo_link_map = NULL;
    find->out->dlfo_eh_frame = eh;
    find->found = 1;
    return 1;
}

TTYD_COMPAT_HIDDEN int _dl_find_object(void* pc, struct dl_find_object* result);
TTYD_COMPAT_HIDDEN int _dl_find_object(void* pc, struct dl_find_object* result) {
    struct FindObject find = {(uintptr_t)pc, result, 0};
    dl_iterate_phdr(findObject, &find);
    return find.found ? 0 : -1;
}
#endif
#endif
