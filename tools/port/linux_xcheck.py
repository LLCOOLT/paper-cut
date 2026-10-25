#!/usr/bin/env python3
"""Compile and link the headless PC build for Linux on a machine without a
Linux sysroot (Windows), as a check: the Windows build hides MSVC-only
leniencies (a static definition after an extern declaration, the same plain
inline function in several files) and the LLP64/MSVC-ABI type differences
(long, enum signedness) that a Linux build trips over.

A stand-in for the glibc headers the port uses (LP64 types, POSIX/glibc
prototypes; declarations only) and empty C libraries are written to OUT, the
build is configured for x86_64- or aarch64-linux-gnu with clang and linked
with ld.lld leaving the C library unresolved (the result cannot run), then the
ELF layout of the RELs linked into the executable is checked.

usage: linux_xcheck.py --out DIR [--version G8MJ01|G8ME01] [--arch x86_64|aarch64] [-- -DOPTION=...]
Needs clang, ld.lld, llvm-ar, llvm-nm, cmake and ninja on PATH.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

H = {}

H["features.h"] = r"""
#pragma once
#define __GLIBC__ 2
#define __GLIBC_MINOR__ 39
#ifdef _GNU_SOURCE
#define __USE_GNU 1
#endif
#define __THROW
#define __BEGIN_DECLS
#define __END_DECLS
"""
H["bits/types.h"] = r"""
#pragma once
#include <features.h>
typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef long time_t;
typedef long clock_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int mode_t;
typedef int clockid_t;
typedef long suseconds_t;
typedef unsigned long pthread_t;
"""
H["sys/types.h"] = "#pragma once\n#include <bits/types.h>\n"
# port_prefix.h includes it so the game's `select` can be renamed after it
H["sys/select.h"] = "#pragma once\n#include <sys/types.h>\n"
H["stdio.h"] = r"""
#pragma once
#include <bits/types.h>
#include <stdarg.h>
#define NULL ((void*)0)
typedef struct _IO_FILE FILE;
extern FILE* stdin; extern FILE* stdout; extern FILE* stderr;
#define stdin stdin
#define stdout stdout
#define stderr stderr
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 8192
#define FILENAME_MAX 4096
typedef struct { long __pos; } fpos_t;
FILE* fopen(const char* restrict, const char* restrict);
FILE* fdopen(int, const char*);
FILE* freopen(const char* restrict, const char* restrict, FILE* restrict);
int fclose(FILE*);
int fflush(FILE*);
size_t fread(void* restrict, size_t, size_t, FILE* restrict);
size_t fwrite(const void* restrict, size_t, size_t, FILE* restrict);
int fseek(FILE*, long, int);
long ftell(FILE*);
void rewind(FILE*);
int feof(FILE*);
int ferror(FILE*);
int fgetc(FILE*);
int getc(FILE*);
int getchar(void);
char* fgets(char* restrict, int, FILE* restrict);
int fputc(int, FILE*);
int putc(int, FILE*);
int putchar(int);
int fputs(const char* restrict, FILE* restrict);
int puts(const char*);
int ungetc(int, FILE*);
int printf(const char* restrict, ...);
int fprintf(FILE* restrict, const char* restrict, ...);
int sprintf(char* restrict, const char* restrict, ...);
int snprintf(char* restrict, size_t, const char* restrict, ...);
int vprintf(const char* restrict, va_list);
int vfprintf(FILE* restrict, const char* restrict, va_list);
int vsprintf(char* restrict, const char* restrict, va_list);
int vsnprintf(char* restrict, size_t, const char* restrict, va_list);
int scanf(const char* restrict, ...);
int fscanf(FILE* restrict, const char* restrict, ...);
int sscanf(const char* restrict, const char* restrict, ...);
int vsscanf(const char* restrict, const char* restrict, va_list);
void perror(const char*);
int remove(const char*);
int rename(const char*, const char*);
FILE* tmpfile(void);
int setvbuf(FILE* restrict, char* restrict, int, size_t);
void setbuf(FILE* restrict, char* restrict);
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
int fileno(FILE*);
void flockfile(FILE*);
int ftrylockfile(FILE*);
void funlockfile(FILE*);
"""
H["stdlib.h"] = r"""
#pragma once
#include <bits/types.h>
#define NULL ((void*)0)
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
void* malloc(size_t);
void* calloc(size_t, size_t);
void* realloc(void*, size_t);
void free(void*);
void* aligned_alloc(size_t, size_t);
int posix_memalign(void**, size_t, size_t);
_Noreturn void abort(void);
_Noreturn void exit(int);
_Noreturn void _Exit(int);
int atexit(void (*)(void));
char* getenv(const char*);
int setenv(const char*, const char*, int);
int unsetenv(const char*);
int system(const char*);
int atoi(const char*);
long atol(const char*);
long long atoll(const char*);
double atof(const char*);
long strtol(const char* restrict, char** restrict, int);
unsigned long strtoul(const char* restrict, char** restrict, int);
long long strtoll(const char* restrict, char** restrict, int);
unsigned long long strtoull(const char* restrict, char** restrict, int);
double strtod(const char* restrict, char** restrict);
float strtof(const char* restrict, char** restrict);
int rand(void);
void srand(unsigned);
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
void qsort(void*, size_t, size_t, int (*)(const void*, const void*));
void* bsearch(const void*, const void*, size_t, size_t, int (*)(const void*, const void*));
char* realpath(const char* restrict, char* restrict);
int mkstemp(char*);
"""
H["string.h"] = r"""
#pragma once
#include <bits/types.h>
#define NULL ((void*)0)
void* memcpy(void* restrict, const void* restrict, size_t);
void* memmove(void*, const void*, size_t);
void* memset(void*, int, size_t);
int memcmp(const void*, const void*, size_t);
void* memchr(const void*, int, size_t);
char* strcpy(char* restrict, const char* restrict);
char* strncpy(char* restrict, const char* restrict, size_t);
char* strcat(char* restrict, const char* restrict);
char* strncat(char* restrict, const char* restrict, size_t);
int strcmp(const char*, const char*);
int strncmp(const char*, const char*, size_t);
int strcoll(const char*, const char*);
size_t strlen(const char*);
size_t strnlen(const char*, size_t);
char* strchr(const char*, int);
char* strrchr(const char*, int);
char* strstr(const char*, const char*);
size_t strspn(const char*, const char*);
size_t strcspn(const char*, const char*);
char* strpbrk(const char*, const char*);
char* strtok(char* restrict, const char* restrict);
char* strtok_r(char* restrict, const char* restrict, char** restrict);
char* strdup(const char*);
char* strndup(const char*, size_t);
char* strerror(int);
char* strsignal(int);
#include <strings.h>
"""
H["strings.h"] = r"""
#pragma once
#include <bits/types.h>
int strcasecmp(const char*, const char*);
int strncasecmp(const char*, const char*, size_t);
"""
H["ctype.h"] = r"""
#pragma once
int isalnum(int); int isalpha(int); int isblank(int); int iscntrl(int); int isdigit(int);
int isgraph(int); int islower(int); int isprint(int); int ispunct(int); int isspace(int);
int isupper(int); int isxdigit(int); int tolower(int); int toupper(int);
"""
H["math.h"] = r"""
#pragma once
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_SQRT2 1.41421356237309504880
#define M_E 2.7182818284590452354
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(0, 1, 4, 3, 2, x)
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define __MATH1(n) double n(double); float n##f(float); long double n##l(long double);
#define __MATH2(n) double n(double, double); float n##f(float, float); long double n##l(long double, long double);
__MATH1(acos) __MATH1(asin) __MATH1(atan) __MATH2(atan2) __MATH1(cos) __MATH1(sin) __MATH1(tan)
__MATH1(cosh) __MATH1(sinh) __MATH1(tanh) __MATH1(acosh) __MATH1(asinh) __MATH1(atanh)
__MATH1(exp) __MATH1(exp2) __MATH1(expm1) __MATH1(log) __MATH1(log10) __MATH1(log2) __MATH1(log1p)
__MATH2(pow) __MATH1(sqrt) __MATH1(cbrt) __MATH2(hypot) __MATH1(ceil) __MATH1(floor) __MATH1(fabs)
__MATH2(fmod) __MATH1(round) __MATH1(trunc) __MATH1(rint) __MATH1(nearbyint) __MATH2(fmin) __MATH2(fmax)
__MATH2(copysign) __MATH2(remainder) __MATH2(fdim)
double frexp(double, int*); float frexpf(float, int*);
double ldexp(double, int); float ldexpf(float, int);
double modf(double, double*); float modff(float, float*);
long lround(double); long lroundf(float); long long llround(double);
long lrint(double); long lrintf(float);
double fma(double, double, double); float fmaf(float, float, float);
#ifdef __USE_GNU
void sincos(double, double*, double*);
void sincosf(float, float*, float*);
#endif
"""
H["time.h"] = r"""
#pragma once
#include <bits/types.h>
#define NULL ((void*)0)
#define CLOCKS_PER_SEC 1000000l
#define TIME_UTC 1
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
struct timespec { time_t tv_sec; long tv_nsec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; const char* tm_zone; };
time_t time(time_t*);
clock_t clock(void);
double difftime(time_t, time_t);
time_t mktime(struct tm*);
struct tm* localtime(const time_t*);
struct tm* gmtime(const time_t*);
struct tm* localtime_r(const time_t* restrict, struct tm* restrict);
struct tm* gmtime_r(const time_t* restrict, struct tm* restrict);
size_t strftime(char* restrict, size_t, const char* restrict, const struct tm* restrict);
int timespec_get(struct timespec*, int);
int nanosleep(const struct timespec*, struct timespec*);
int clock_gettime(clockid_t, struct timespec*);
"""
H["wchar.h"] = r"""
#pragma once
#include <bits/types.h>
typedef int wchar_t;
typedef unsigned int wint_t;
size_t wcslen(const wchar_t*);
"""
H["assert.h"] = r"""
#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
_Noreturn void __assert_fail(const char*, const char*, unsigned int, const char*);
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__, __func__))
#endif
#define static_assert _Static_assert
"""
H["errno.h"] = r"""
#pragma once
int* __errno_location(void);
#define errno (*__errno_location())
#define EINTR 4
#define EAGAIN 11
#define ENOENT 2
#define EINVAL 22
"""
H["setjmp.h"] = r"""
#pragma once
typedef long __jmp_buf[8];
typedef struct __jmp_buf_tag { __jmp_buf __jmpbuf; int __mask_was_saved; unsigned long __saved_mask[16]; } jmp_buf[1];
typedef jmp_buf sigjmp_buf;
int setjmp(jmp_buf);
_Noreturn void longjmp(jmp_buf, int);
int sigsetjmp(sigjmp_buf, int);
_Noreturn void siglongjmp(sigjmp_buf, int);
"""
H["bits/sigset.h"] = r"""
#pragma once
typedef struct { unsigned long __val[16]; } sigset_t;
"""
H["sys/ucontext.h"] = r"""
#pragma once
#include <bits/sigset.h>
#include <bits/types.h>
typedef struct { void* ss_sp; int ss_flags; size_t ss_size; } stack_t;
#if defined(__x86_64__)
typedef long long greg_t;
#define __NGREG 23
typedef greg_t gregset_t[__NGREG];
#ifdef __USE_GNU
enum { REG_R8 = 0, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15, REG_RDI, REG_RSI, REG_RBP,
       REG_RBX, REG_RDX, REG_RAX, REG_RCX, REG_RSP, REG_RIP, REG_EFL, REG_CSGSFS, REG_ERR, REG_TRAPNO,
       REG_OLDMASK, REG_CR2 };
#define REG_RIP REG_RIP
#endif
typedef struct { gregset_t gregs; void* fpregs; unsigned long long __reserved1[8]; } mcontext_t;
#elif defined(__aarch64__)
typedef struct { unsigned long long fault_address; unsigned long long regs[31]; unsigned long long sp, pc, pstate;
                 unsigned char __reserved[4096] __attribute__((aligned(16))); } mcontext_t;
#endif
typedef struct ucontext_t { unsigned long uc_flags; struct ucontext_t* uc_link; stack_t uc_stack; mcontext_t uc_mcontext;
                            sigset_t uc_sigmask; } ucontext_t;
"""
H["ucontext.h"] = r"""
#pragma once
#include <sys/ucontext.h>
int getcontext(ucontext_t*);
int setcontext(const ucontext_t*);
void makecontext(ucontext_t*, void (*)(void), int, ...);
int swapcontext(ucontext_t* restrict, const ucontext_t* restrict);
"""
H["signal.h"] = r"""
#pragma once
#include <bits/types.h>
#include <bits/sigset.h>
#include <sys/ucontext.h>
typedef int sig_atomic_t;
typedef void (*__sighandler_t)(int);
#define SIG_DFL ((__sighandler_t)0)
#define SIG_IGN ((__sighandler_t)1)
#define SIG_ERR ((__sighandler_t)-1)
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define FPE_INTDIV 1
#define FPE_INTOVF 2
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SA_SIGINFO 4
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000
#define SS_ONSTACK 1
#define SS_DISABLE 2
#define MINSIGSTKSZ 2048
#define SIGSTKSZ 8192
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
union sigval { int sival_int; void* sival_ptr; };
typedef struct { int si_signo, si_errno, si_code; int __pad0; void* si_addr; long __pad[13]; } siginfo_t;
struct sigaction {
    union { __sighandler_t sa_handler; void (*sa_sigaction)(int, siginfo_t*, void*); } __sigaction_handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};
#define sa_handler __sigaction_handler.sa_handler
#define sa_sigaction __sigaction_handler.sa_sigaction
__sighandler_t signal(int, __sighandler_t);
int raise(int);
int kill(pid_t, int);
int sigaction(int, const struct sigaction* restrict, struct sigaction* restrict);
int sigemptyset(sigset_t*);
int sigfillset(sigset_t*);
int sigaddset(sigset_t*, int);
int sigdelset(sigset_t*, int);
int sigprocmask(int, const sigset_t* restrict, sigset_t* restrict);
int sigaltstack(const stack_t* restrict, stack_t* restrict);
int pthread_kill(pthread_t, int);
int pthread_sigmask(int, const sigset_t* restrict, sigset_t* restrict);
"""
H["pthread.h"] = r"""
#pragma once
#include <bits/types.h>
#include <time.h>
typedef union { char __size[40]; long __align; } pthread_mutex_t;
typedef union { char __size[4]; int __align; } pthread_mutexattr_t;
typedef union { char __size[48]; long long __align; } pthread_cond_t;
typedef union { char __size[4]; int __align; } pthread_condattr_t;
typedef union { char __size[56]; long __align; } pthread_attr_t;
typedef unsigned int pthread_key_t;
typedef int pthread_once_t;
#define PTHREAD_MUTEX_INITIALIZER { { 0 } }
#define PTHREAD_COND_INITIALIZER { { 0 } }
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
int pthread_create(pthread_t* restrict, const pthread_attr_t* restrict, void* (*)(void*), void* restrict);
int pthread_join(pthread_t, void**);
_Noreturn void pthread_exit(void*);
int pthread_detach(pthread_t);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
int pthread_attr_init(pthread_attr_t*);
int pthread_attr_destroy(pthread_attr_t*);
int pthread_attr_setdetachstate(pthread_attr_t*, int);
int pthread_attr_setstacksize(pthread_attr_t*, size_t);
int pthread_mutex_init(pthread_mutex_t* restrict, const pthread_mutexattr_t* restrict);
int pthread_mutex_destroy(pthread_mutex_t*);
int pthread_mutex_lock(pthread_mutex_t*);
int pthread_mutex_trylock(pthread_mutex_t*);
int pthread_mutex_unlock(pthread_mutex_t*);
int pthread_mutexattr_init(pthread_mutexattr_t*);
int pthread_mutexattr_destroy(pthread_mutexattr_t*);
int pthread_mutexattr_settype(pthread_mutexattr_t*, int);
int pthread_cond_init(pthread_cond_t* restrict, const pthread_condattr_t* restrict);
int pthread_cond_destroy(pthread_cond_t*);
int pthread_cond_wait(pthread_cond_t* restrict, pthread_mutex_t* restrict);
int pthread_cond_timedwait(pthread_cond_t* restrict, pthread_mutex_t* restrict, const struct timespec* restrict);
int pthread_cond_signal(pthread_cond_t*);
int pthread_cond_broadcast(pthread_cond_t*);
int pthread_once(pthread_once_t*, void (*)(void));
"""
H["dlfcn.h"] = r"""
#pragma once
#include <features.h>
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0
void* dlopen(const char*, int);
void* dlsym(void* restrict, const char* restrict);
int dlclose(void*);
char* dlerror(void);
#ifdef __USE_GNU
#define RTLD_DEFAULT ((void*)0)
typedef struct { const char* dli_fname; void* dli_fbase; const char* dli_sname; void* dli_saddr; } Dl_info;
int dladdr(const void*, Dl_info*);
#endif
"""
H["sys/mman.h"] = r"""
#pragma once
#include <bits/types.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void*)-1)
#define MADV_DONTNEED 4
void* mmap(void*, size_t, int, int, int, off_t);
int munmap(void*, size_t);
int mprotect(void*, size_t, int);
int madvise(void*, size_t, int);
"""

H["unistd.h"] = r"""
#pragma once
#include <bits/types.h>
#define NULL ((void*)0)
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
ssize_t read(int, void*, size_t);
ssize_t write(int, const void*, size_t);
ssize_t pread(int, void*, size_t, off_t);
ssize_t pwrite(int, const void*, size_t, off_t);
int close(int);
int fsync(int);
off_t lseek(int, off_t, int);
ssize_t readlink(const char* restrict, char* restrict, size_t);
int access(const char*, int);
unsigned sleep(unsigned);
int usleep(unsigned);
pid_t getpid(void);
char* getcwd(char*, size_t);
int chdir(const char*);
int unlink(const char*);
int rmdir(const char*);
long sysconf(int);
int isatty(int);
_Noreturn void _exit(int);
"""
H["fcntl.h"] = r"""
#pragma once
#include <bits/types.h>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_CLOEXEC 02000000
int open(const char*, int, ...);
"""
H["sys/stat.h"] = r"""
#pragma once
#include <bits/types.h>
struct stat { unsigned long st_dev, st_ino, st_nlink; unsigned int st_mode, st_uid, st_gid; int __pad0;
              unsigned long st_rdev; long st_size, st_blksize, st_blocks; struct { long tv_sec, tv_nsec; } st_atim, st_mtim, st_ctim; long __unused[3]; };
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
int stat(const char* restrict, struct stat* restrict);
int lstat(const char* restrict, struct stat* restrict);
int fstat(int, struct stat*);
int mkdir(const char*, mode_t);
"""
H["dirent.h"] = r"""
#pragma once
#include <bits/types.h>
typedef struct __dirstream DIR;
struct dirent { unsigned long d_ino; long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
DIR* opendir(const char*);
struct dirent* readdir(DIR*);
int closedir(DIR*);
"""
H["elf.h"] = r"""
#pragma once
#include <stdint.h>
typedef uint16_t Elf64_Half; typedef uint32_t Elf64_Word; typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword; typedef int64_t Elf64_Sxword; typedef uint64_t Elf64_Addr; typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Section;
#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4
typedef struct { unsigned char e_ident[EI_NIDENT]; Elf64_Half e_type, e_machine; Elf64_Word e_version;
                 Elf64_Addr e_entry; Elf64_Off e_phoff, e_shoff; Elf64_Word e_flags;
                 Elf64_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } Elf64_Ehdr;
typedef struct { Elf64_Word sh_name, sh_type; Elf64_Xword sh_flags; Elf64_Addr sh_addr; Elf64_Off sh_offset;
                 Elf64_Xword sh_size; Elf64_Word sh_link, sh_info; Elf64_Xword sh_addralign, sh_entsize; } Elf64_Shdr;
typedef struct { Elf64_Word st_name; unsigned char st_info, st_other; Elf64_Section st_shndx;
                 Elf64_Addr st_value; Elf64_Xword st_size; } Elf64_Sym;
typedef struct { Elf64_Word p_type, p_flags; Elf64_Off p_offset; Elf64_Addr p_vaddr, p_paddr;
                 Elf64_Xword p_filesz, p_memsz, p_align; } Elf64_Phdr;
#define SHT_SYMTAB 2
#define SHT_DYNSYM 11
#define SHN_UNDEF 0
#define SHN_LORESERVE 0xff00
#define STT_OBJECT 1
#define STT_FUNC 2
#define PT_LOAD 1
"""
H["link.h"] = r"""
#pragma once
#include <features.h>
#include <elf.h>
#include <bits/types.h>
#define ElfW(type) Elf64_##type
#ifdef __USE_GNU
struct dl_phdr_info { ElfW(Addr) dlpi_addr; const char* dlpi_name; const ElfW(Phdr)* dlpi_phdr; ElfW(Half) dlpi_phnum;
                      unsigned long long dlpi_adds, dlpi_subs; size_t dlpi_tls_modid; void* dlpi_tls_data; };
int dl_iterate_phdr(int (*)(struct dl_phdr_info*, size_t, void*), void*);
#endif
"""
H["semaphore.h"] = r"""
#pragma once
typedef union { char __size[32]; long __align; } sem_t;
int sem_init(sem_t*, int, unsigned);
int sem_destroy(sem_t*);
int sem_wait(sem_t*);
int sem_post(sem_t*);
"""
H["inttypes.h"] = r"""
#pragma once
#include <stdint.h>
#define PRId64 "ld"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#define PRIxPTR "lx"
"""


TOOLCHAIN = """# Generated by tools/port/linux_xcheck.py: compile for {triple} against the
# stand-in C library headers, link with ld.lld leaving the C library unresolved.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR {arch})
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET {triple})
set(CMAKE_CXX_COMPILER_TARGET {triple})
set(CMAKE_SYSROOT {sysroot})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(_ttyd_ld "-fuse-ld=lld -nostdlib -L{sysroot}/usr/lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${{_ttyd_ld}} -pie -Wl,--unresolved-symbols=ignore-all -Wl,-e,main")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${{_ttyd_ld}}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${{_ttyd_ld}}")
"""


def tool(name):
    found = shutil.which(name)
    if found is None:
        sys.exit(f"linux_xcheck: {name} not found on PATH")
    return found


def write_sysroot(sysroot):
    inc = os.path.join(sysroot, "usr", "include")
    for name, text in H.items():
        path = os.path.join(inc, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", newline="\n") as f:
            f.write(text.lstrip("\n"))
    lib = os.path.join(sysroot, "usr", "lib")
    os.makedirs(lib, exist_ok=True)
    for name in ("c", "m", "dl", "pthread", "pthreads", "rt"):
        path = os.path.join(lib, f"lib{name}.a")
        if not os.path.exists(path):
            subprocess.run([tool("llvm-ar"), "rcs", path], check=True)


def check_rels(exe):
    """Every renamed REL data/bss symbol lies inside its REL's ELF section."""
    out = subprocess.run([tool("llvm-nm"), "-S", exe], capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4:
            syms.append((int(p[0], 16), int(p[1], 16), p[2], p[3]))
        elif len(p) == 3:
            syms.append((int(p[0], 16), 0, p[1], p[2]))
    # each REL's sections, from the section headers (the linker's __start_/
    # __stop_ bounds are hidden, so not in the symbol table)
    bounds = {}
    heads = subprocess.run([tool("llvm-objdump"), "-h", exe], capture_output=True, text=True, check=True).stdout
    for m in re.finditer(r"^\s*\d+\s+rel_(\w+)_(data|bss)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", heads, re.M):
        start, size = int(m.group(4), 16), int(m.group(3), 16)
        bounds[(m.group(1), m.group(2))] = {"start": start, "stop": start + size}
    rels = {k[0] for k in bounds}
    bad = checked = 0
    for addr, size, t, name in syms:
        if t not in "dDbB" or not name.startswith("rel_"):
            continue
        cands = [r for r in rels if name.startswith(f"rel_{r}__")]
        if not cands:
            continue
        rel = max(cands, key=len)
        b = bounds[(rel, "data" if t in "dD" else "bss")]
        checked += 1
        if not (b["start"] <= addr and addr + size <= b["stop"]):
            print(f"  {name} ({t}) at {addr:#x} is outside rel_{rel}")
            bad += 1
    print(f"REL sections: {len(rels)} RELs, {checked} data/bss symbols, {bad} outside their section")
    # nothing found is a broken check (or link), not a pass
    return bad == 0 and len(rels) > 0 and checked > 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="work directory (sysroot, toolchain, build)")
    ap.add_argument("--version", default="G8MJ01")
    ap.add_argument("--arch", default="x86_64", choices=["x86_64", "aarch64"])
    ap.add_argument("--backend", default="headless", choices=["headless"])
    ap.add_argument("cmake_args", nargs="*", help="extra -D options for cmake")
    a = ap.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out = os.path.abspath(a.out)
    sysroot = os.path.join(out, "sysroot").replace("\\", "/")
    write_sysroot(sysroot)
    triple = f"{a.arch}-linux-gnu"
    tc = os.path.join(out, f"toolchain-{a.arch}.cmake")
    with open(tc, "w", newline="\n") as f:
        f.write(TOOLCHAIN.format(triple=triple, arch=a.arch, sysroot=sysroot))
    build = os.path.join(out, f"build-{a.arch}-{a.version}")
    subprocess.run([tool("cmake"), "-S", root, "-B", build, "-G", "Ninja", "--toolchain", tc,
                    "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    f"-DTTYD_VERSION={a.version}", f"-DTTYD_BACKEND={a.backend}", *a.cmake_args], check=True)
    ok = subprocess.run([tool("ninja"), "-C", build, "-k", "0"]).returncode == 0
    # the host unit tests (EXCLUDE_FROM_ALL) too
    ok = subprocess.run([tool("ninja"), "-C", build, "-k", "0", "bedecode_test", "anim_test", "loaders_test",
                         "map_test", "dtk_test", "musyx_swap_test", "musyx_mixer_test", "disc_sweep",
                         "musyx_seq_sweep"]).returncode == 0 and ok
    exe = os.path.join(build, "ttyd-us" if a.version == "G8ME01" else "ttyd-jp")
    if ok and os.path.exists(exe):
        ok = check_rels(exe) and ok
    print("linux_xcheck: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
