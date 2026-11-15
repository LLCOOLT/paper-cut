/* Host addresses under 2 GiB (port/include/port/low_address.h): telling a
 * pointer in such memory from a number by what it points at, and the test
 * option that puts the program's memory there. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "port/low_address.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

int gPortLowAddresses;

#define TEXT_MAX 128
#define PAGE_STEP 4096u

/* up to n bytes at a into out without faulting (a chunk never crosses a 4 KiB page); returns how many were read */
static size_t readHost(uintptr_t a, unsigned char* out, size_t n) {
#if defined(_WIN32)
    size_t got = 0;
    while (got < n) {
        size_t chunk = PAGE_STEP - (size_t)((a + got) & (PAGE_STEP - 1));
        SIZE_T did = 0;
        if (chunk > n - got) {
            chunk = n - got;
        }
        if (!ReadProcessMemory(GetCurrentProcess(), (const void*)(a + got), out + got, chunk, &did) || did != chunk) {
            break;
        }
        got += chunk;
    }
    return got;
#elif defined(__EMSCRIPTEN__)
    (void)a, (void)out, (void)n;
    return 0;
#else
    /* through a pipe: write() reports an unreadable buffer (EFAULT) instead of faulting */
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static int fds[2] = {-1, -1};
    size_t got = 0;
    pthread_mutex_lock(&lock);
    if (fds[0] < 0 && pipe(fds) == 0) {
        fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
        fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
    }
    while (fds[0] >= 0 && got < n) {
        size_t chunk = PAGE_STEP - (size_t)((a + got) & (PAGE_STEP - 1));
        ssize_t w;
        if (chunk > n - got) {
            chunk = n - got;
        }
        w = write(fds[1], (const void*)(a + got), chunk);
        if (w != (ssize_t)chunk) {
            unsigned char sink[TEXT_MAX];
            while (w > 0 && read(fds[0], sink, sizeof(sink)) > 0) {
            }
            break;
        }
        if (read(fds[0], out + got, chunk) != (ssize_t)chunk) {
            break;
        }
        got += chunk;
    }
    pthread_mutex_unlock(&lock);
    return got;
#endif
}

int portLowWordIsText(uintptr_t w) {
    unsigned char buf[TEXT_MAX];
    size_t n = readHost(w, buf, sizeof(buf)), i;
    for (i = 0; i < n; i++) {
        if (buf[i] == 0) {
            return i >= 2; /* (UTF-16 text, a byte and a NUL, is not a name) */
        }
        if (buf[i] < 0x20 || buf[i] == 0x7F) {
            return 0;
        }
    }
    return 0;
}

int portLowAddressMapped(uintptr_t p) {
    unsigned char b;
    if (p < 0x10000u) {
        return 0;
    }
    return readHost(p, &b, 1) == 1;
}

/* --- $TTYD_LOW_ADDRESSES (tests) --- */
static unsigned long long sReservedMiB;
static int sAslrOff;

unsigned long long portLowAddressesReservedMiB(void) {
    return sReservedMiB;
}

int portLowAddressesAslrOff(void) {
    return sAslrOff;
}

#if defined(_WIN32)
#define LOW_CHILD_ENV "TTYD_LOW_ADDRESSES_CHILD"

/* this process runs with bottom-up and high-entropy ASLR off */
static int aslrIsOff(void) {
    PROCESS_MITIGATION_ASLR_POLICY p;
    memset(&p, 0, sizeof(p));
    if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessASLRPolicy, &p, sizeof(p))) {
        return 0;
    }
    return !p.EnableBottomUpRandomization && !p.EnableHighEntropy;
}

/* The program again, as Windows starts it with bottom-up and high-entropy ASLR off (Exploit Protection):
 * the same command line, environment and standard handles, its exit code passed on. A job ends it with this
 * process (a test's timeout); what it starts in turn breaks away, as without the option. 0: it could not. */
static int relaunchAslrOff(void) {
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    SIZE_T size = 0;
    DWORD64 policy = PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_OFF |
                     PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_OFF;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim;
    HANDLE job;
    DWORD code = 1;
    wchar_t* cmd;
    const wchar_t* line = GetCommandLineW();
    const DWORD stdIds[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    HANDLE std[3];
    int i, ok;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
    cmd = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (wcslen(line) + 1) * sizeof(wchar_t));
    if (si.lpAttributeList == NULL || cmd == NULL ||
        !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size)) {
        return 0;
    }
    memcpy(cmd, line, (wcslen(line) + 1) * sizeof(wchar_t));
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &policy,
                                   sizeof(policy), NULL, NULL)) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        std[i] = GetStdHandle(stdIds[i]);
        if (std[i] != NULL && std[i] != INVALID_HANDLE_VALUE) {
            SetHandleInformation(std[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        }
    }
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = std[0];
    si.StartupInfo.hStdOutput = std[1];
    si.StartupInfo.hStdError = std[2];
    SetEnvironmentVariableA(LOW_CHILD_ENV, "1");
    ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, NULL, NULL,
                        &si.StartupInfo, &pi);
    SetEnvironmentVariableA(LOW_CHILD_ENV, NULL);
    if (!ok) {
        return 0;
    }
    job = CreateJobObjectW(NULL, NULL);
    memset(&lim, 0, sizeof(lim));
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    if (job != NULL) {
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim));
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    ExitProcess(code);
    return 1;
}

/* Before main (a C initializer, ahead of the settings, Aurora, the graphics driver and the game's threads).
 * The program is started again with bottom-up and high-entropy ASLR off, as a player's Windows with those
 * settings off starts it: the heap, the stacks and VirtualAlloc's memory then begin near the bottom. Should
 * that fail, every free region from 4 GiB up is reserved instead (never committed), so later allocations find
 * room only below; Windows is slow to search past that much reserved space (the first allocations and each
 * new thread took tens of seconds), so this is only the fallback. */
static void lowAddressesAtStart(void) {
    const char* v = getenv("TTYD_LOW_ADDRESSES");
    const char* child = getenv(LOW_CHILD_ENV);
    SYSTEM_INFO si;
    uintptr_t a, top, gran;
    MEMORY_BASIC_INFORMATION mbi;
    if (v == NULL || v[0] == '\0' || strcmp(v, "0") == 0) {
        return;
    }
    if (aslrIsOff()) {
        /* (what this program starts takes the option afresh) */
        _putenv_s(LOW_CHILD_ENV, "");
        sAslrOff = 1;
        gPortLowAddresses = 1;
        return;
    }
    if ((child == NULL || child[0] == '\0') && relaunchAslrOff()) {
        return;
    }
    _putenv_s(LOW_CHILD_ENV, "");
    GetSystemInfo(&si);
    gran = si.dwAllocationGranularity;
    top = (uintptr_t)si.lpMaximumApplicationAddress;
    for (a = (uintptr_t)1 << 32; a < top && VirtualQuery((const void*)a, &mbi, sizeof(mbi)) == sizeof(mbi);) {
        const uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State == MEM_FREE) {
            const uintptr_t start = (a + gran - 1) & ~(gran - 1);
            if (end > start && VirtualAlloc((void*)start, end - start, MEM_RESERVE, PAGE_NOACCESS) != NULL) {
                sReservedMiB += (unsigned long long)(end - start) >> 20;
            }
        }
        if (end <= a) {
            break;
        }
        a = end;
    }
    if (sReservedMiB > 0) {
        gPortLowAddresses = 1;
    }
}

/* .CRT$XCT runs ahead of the C++ constructors (.CRT$XCU), so the first launch does as little as it can */
#if defined(_MSC_VER)
#if defined(__clang__)
__attribute__((section(".CRT$XCT"), used)) static void (*const sLowAddressesInit)(void) = lowAddressesAtStart;
#else
#pragma section(".CRT$XCT", read)
__declspec(allocate(".CRT$XCT")) static void (*const sLowAddressesInit)(void) = lowAddressesAtStart;
#endif
#else
__attribute__((constructor(102))) static void lowAddressesInit(void) {
    lowAddressesAtStart();
}
#endif
#endif
