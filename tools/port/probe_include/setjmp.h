#pragma once
typedef long long jmp_buf[32];
int setjmp(jmp_buf); void longjmp(jmp_buf, int) __attribute__((noreturn));
